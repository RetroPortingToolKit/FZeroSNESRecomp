#include "fzero_diagnostics.h"
#include "desktop/sdl_compat.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#endif

/* Like MPH's diagnostics: one timestamped JSONL per run, settings/hardware
 * first, then interval deltas. Disabled means no directory, file or timers.
 * Deliberately excludes paths, ROM data, save data and player identity. */
static FILE *log_file;
static uint64_t frequency, started, sampled, last_present;
static uint64_t previous_simulation, previous_presentations, previous_missed;
typedef struct Timing { uint64_t ticks, maximum, calls; } Timing;
static Timing timings[FZERO_DIAG_STAGE_COUNT];
static const char *const stage_names[] = {
  "simulation", "ppu", "composition", "upload", "draw_submit", "present",
  "pacing_wait", "paused"
};
/* 0.25 ms bins through 128 ms, then overflow. Bounded even in long sessions. */
static uint64_t intervals[513], interval_count, interval_max;
static unsigned sequence;
static bool first_sample;

static void json_string(const char *text) {
  fputc('"', log_file);
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; ++p) {
    if (*p == '"' || *p == '\\') fprintf(log_file, "\\%c", *p);
    else if (*p < 32) fprintf(log_file, "\\u%04x", *p);
    else fputc(*p, log_file);
  }
  fputc('"', log_file);
}
static void string_field(const char *name, const char *value) {
  fprintf(log_file, ",\"%s\":", name); json_string(value);
}
static double ms(uint64_t ticks) { return (double)ticks * 1000.0 / (double)frequency; }
static void stamp(const char *kind) {
  fprintf(log_file, "{\"kind\":"); json_string(kind);
  fprintf(log_file, ",\"unix_seconds\":%lld,\"elapsed_ms\":%.3f",
          (long long)time(NULL), ms(SDL_GetPerformanceCounter() - started));
}
static void flush_log(void) {
  if (fflush(log_file) || ferror(log_file)) {
    fprintf(stderr, "[fzero-diagnostics] Write failed; disabling diagnostics for this session\n");
    fclose(log_file); log_file = NULL;
  }
}
static void host_fields(void) {
  char brand[49] = {0}, os[128] = {0};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
  int regs[4];
  __cpuid(regs, 0x80000000);
  if ((unsigned)regs[0] >= 0x80000004)
    for (int i = 0; i < 3; ++i) {
      __cpuid(regs, 0x80000002 + i); memcpy(brand + i * 16, regs, 16);
    }
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
  unsigned regs[4];
  if (__get_cpuid(0x80000000, &regs[0], &regs[1], &regs[2], &regs[3]) && regs[0] >= 0x80000004)
    for (int i = 0; i < 3; ++i)
      if (__get_cpuid(0x80000002u + i, &regs[0], &regs[1], &regs[2], &regs[3]))
        memcpy(brand + i * 16, regs, 16);
#endif
#ifdef _WIN32
  typedef LONG (WINAPI *VersionFn)(OSVERSIONINFOW *);
  VersionFn get_version = (VersionFn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
  OSVERSIONINFOW version = {0}; version.dwOSVersionInfoSize = sizeof(version);
  if (get_version && !get_version(&version))
    snprintf(os, sizeof(os), "Windows %lu.%lu.%lu", (unsigned long)version.dwMajorVersion,
             (unsigned long)version.dwMinorVersion, (unsigned long)version.dwBuildNumber);
#else
  struct utsname version;
  if (!uname(&version)) snprintf(os, sizeof(os), "%.40s %.40s %.20s", version.sysname, version.release, version.machine);
#endif
  string_field("cpu", brand); string_field("os", os);
#if SNESRECOMP_SDL3
  fprintf(log_file, ",\"logical_cpus\":%d,\"ram_mb\":%d,\"sdl_version\":%d",
          SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM(), SDL_GetVersion());
#else
  SDL_version version_sdl; SDL_GetVersion(&version_sdl);
  fprintf(log_file, ",\"logical_cpus\":%d,\"ram_mb\":%d,\"sdl_version\":%d",
          SDL_GetCPUCount(), SDL_GetSystemRAM(), version_sdl.major * 1000000 + version_sdl.minor * 1000 + version_sdl.patch);
#endif
  string_field("video_driver", SDL_GetCurrentVideoDriver());
  string_field("audio_driver", SDL_GetCurrentAudioDriver());
#ifdef _WIN32
  /* SDL's D3D backend does not expose a portable adapter-name query. Keep
   * attached adapters separate from the GL renderer actually in use. */
  fputs(",\"attached_display_adapters\":[", log_file);
  unsigned count = 0;
  DISPLAY_DEVICEA device = {0}; device.cb = sizeof(device);
  for (DWORD i = 0; EnumDisplayDevicesA(NULL, i, &device, 0); ++i) {
    if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
    if (count++) fputc(',', log_file);
    json_string(device.DeviceString);
  }
  fputc(']', log_file);
#endif
}

bool FzeroDiagnosticsStart(bool enabled, const char *directory,
                           const FzeroDiagnosticSession *s) {
  FzeroDiagnosticsStop();
  if (!enabled) return true;
  if (!directory || !*directory || !s) return false;
#ifdef _WIN32
  int result = _mkdir(directory), pid = _getpid();
#else
  int result = mkdir(directory, 0755), pid = (int)getpid();
#endif
  if (result && errno != EEXIST) goto failed;
  time_t now = time(NULL);
  struct tm date;
#ifdef _WIN32
  if (gmtime_s(&date, &now)) goto failed;
#else
  if (!gmtime_r(&now, &date)) goto failed;
#endif
  char date_text[32], path[2048];
  strftime(date_text, sizeof(date_text), "%Y%m%d-%H%M%SZ", &date);
  if (snprintf(path, sizeof(path), "%s/performance-%s-%d-%u.jsonl", directory, date_text, pid, sequence++) >= (int)sizeof(path)) goto failed;
  log_file = fopen(path, "w");
  if (!log_file) goto failed;
  frequency = SDL_GetPerformanceFrequency();
  started = sampled = SDL_GetPerformanceCounter(); last_present = 0;
  previous_simulation = previous_presentations = previous_missed = 0;
  first_sample = true;
  memset(timings, 0, sizeof(timings)); memset(intervals, 0, sizeof(intervals));
  interval_count = interval_max = 0;
  stamp("session"); fprintf(log_file, ",\"schema\":1");
  string_field("version", s->version); string_field("revision", s->revision);
  string_field("framework_revision", s->framework_revision); string_field("ui_revision", s->ui_revision);
  string_field("build_type", s->build_type); string_field("compiler", s->compiler);
  string_field("backend", s->backend); string_field("gpu", s->gpu);
  string_field("gpu_vendor", s->gpu_vendor); string_field("graphics_driver", s->driver);
  const char *shader = s->shader ? s->shader : "";
  for (const char *p = shader; *p; ++p) if (*p == '/' || *p == '\\') shader = p + 1;
  string_field("shader", shader);
  fprintf(log_file, ",\"shader_loaded\":%d,\"vsync\":%d,\"allocated_scale\":%u,"
          "\"linear_filter\":%d,\"audio_enabled\":%d,\"rewind_enabled\":%d",
          s->shader_loaded, s->vsync, s->allocated_scale, s->linear_filter, s->audio_enabled, s->rewind_enabled);
  host_fields();
  fputs("}\n", log_file); flush_log();
  if (!log_file) return false;
  fprintf(stderr, "[fzero-diagnostics] Writing %s\n", path);
  return true;
failed:
  fprintf(stderr, "[fzero-diagnostics] Cannot create log in %s; gameplay will continue\n", directory);
  return false;
}

uint64_t FzeroDiagnosticsBegin(void) { return log_file ? SDL_GetPerformanceCounter() : 0; }
void FzeroDiagnosticsEnd(FzeroDiagnosticStage stage, uint64_t start) {
  if (!log_file || !start || stage >= FZERO_DIAG_STAGE_COUNT) return;
  uint64_t elapsed = SDL_GetPerformanceCounter() - start;
  Timing *t = &timings[stage]; t->ticks += elapsed; ++t->calls;
  if (elapsed > t->maximum) t->maximum = elapsed;
}
void FzeroDiagnosticsPresented(void) {
  if (!log_file) return;
  uint64_t now = SDL_GetPerformanceCounter();
  if (last_present) {
    uint64_t elapsed = now - last_present;
    double bin = ms(elapsed) * 4;
    ++intervals[bin < 512 ? (unsigned)bin : 512]; ++interval_count;
    if (elapsed > interval_max) interval_max = elapsed;
  }
  last_present = now;
}
void FzeroDiagnosticsEvent(const char *event, int detail) {
  if (!log_file) return;
  stamp("event"); string_field("event", event);
  fprintf(log_file, ",\"detail\":%d}\n", detail); flush_log();
  /* Do not label a menu/pause gap as a slow rendered frame. */
  last_present = 0;
}
static double percentile(unsigned percent) {
  if (!interval_count) return 0;
  uint64_t count = 0, target = (interval_count * percent + 99) / 100;
  for (unsigned i = 0; i < 513; ++i) {
    count += intervals[i];
    if (count >= target) return i == 512 ? ms(interval_max) : (i + 1) * 0.25;
  }
  return 0;
}
void FzeroDiagnosticsSample(const FzeroDiagnosticFrame *f, bool final) {
  if (!log_file || !f) return;
  uint64_t now = SDL_GetPerformanceCounter();
  if (!final && !first_sample && now - sampled < frequency * 2) return;
  double elapsed = ms(now - sampled);
  uint64_t sim = f->simulation - previous_simulation, presents = f->presentations - previous_presentations;
  stamp(final ? "final" : first_sample ? "settings" : "sample");
  first_sample = false;
  fprintf(log_file, ",\"interval_ms\":%.3f,\"simulation_total\":%llu,\"presentations_total\":%llu,"
          "\"simulation_delta\":%llu,\"presentations_delta\":%llu,\"missed_delta\":%llu,"
          "\"simulation_fps\":%.3f,\"presentation_fps\":%.3f,\"target_hz\":%.3f,\"display_hz\":%.3f,"
          "\"frame_interval_p95_ms\":%.3f,\"frame_interval_p99_ms\":%.3f,\"frame_interval_max_ms\":%.3f",
          elapsed, (unsigned long long)f->simulation, (unsigned long long)f->presentations,
          (unsigned long long)sim, (unsigned long long)presents, (unsigned long long)(f->missed - previous_missed),
          elapsed > 0 ? sim * 1000.0 / elapsed : 0, elapsed > 0 ? presents * 1000.0 / elapsed : 0,
          f->target_hz, f->refresh_hz, percentile(95), percentile(99), ms(interval_max));
  const FzeroVideoSettings *v = &f->settings;
  fprintf(log_file,",\"controller\":{\"instance\":%d,\"attached\":%d,\"left_x\":%d,\"left_y\":%d,\"buttons\":%u}",
      f->controller_id,f->controller_attached,f->controller_x,f->controller_y,(unsigned)f->controller_buttons);
  string_field("aspect", FzeroAspectName(v->aspect));
  fprintf(log_file, ",\"enhanced\":%d,\"viewport_enhanced\":%d,\"bs_deluxe\":%d,"
          "\"hd_enabled\":%d,\"requested_scale\":%u,\"effective_scale\":%u,"
          "\"fps_enabled\":%d,\"requested_fps\":%u,\"source_width\":%d,\"source_height\":%d,"
          "\"output_width\":%d,\"output_height\":%d,\"fullscreen\":%d,\"suspended\":%d,"
          "\"scene\":%u,\"subscene\":%u,\"render_workers\":%u,\"stages\":{",
          v->enhanced, f->viewport.enhanced, v->bs_deluxe, v->hd_mode7, v->hd_scale, f->effective_scale,
          v->fps_enabled, v->fps, f->viewport.width * (int)f->effective_scale,
          (int)(f->source_height ? f->source_height : FZERO_HEIGHT) * (int)f->effective_scale,
          f->output_width, f->output_height, f->fullscreen, f->suspended, f->scene, f->subscene, f->render_workers);
  uint64_t accounted = 0;
  for (unsigned i = 0; i < FZERO_DIAG_STAGE_COUNT; ++i) {
    Timing *t = &timings[i]; accounted += t->ticks;
    fprintf(log_file, "%s\"%s\":{\"calls\":%llu,\"total_ms\":%.3f,\"mean_ms\":%.3f,\"max_ms\":%.3f}",
            i ? "," : "", stage_names[i], (unsigned long long)t->calls, ms(t->ticks),
            t->calls ? ms(t->ticks) / t->calls : 0, ms(t->maximum));
  }
  const SnesHdGpuStats *gpu = &f->hd_gpu;
  fprintf(log_file, "},\"unattributed_ms\":%.3f,\"hd_gpu\":{\"frames\":%llu,\"timed_frame\":%llu,"
      "\"upload_bytes\":%llu,\"upload_ms\":%.6f,\"draw_ms\":%.6f,\"source_bytes\":%llu,"
      "\"target_bytes\":%llu,\"requested_width\":%u,\"requested_height\":%u,\"width\":%u,\"height\":%u}}\n",
      fmax(0, elapsed - ms(accounted)), (unsigned long long)gpu->frames,
      (unsigned long long)gpu->timed_frame, (unsigned long long)gpu->uploaded_bytes,
      gpu->upload_gpu_ns / 1e6, gpu->draw_gpu_ns / 1e6, (unsigned long long)gpu->source_gpu_bytes,
      (unsigned long long)gpu->target_bytes, gpu->requested_width, gpu->requested_height,
      gpu->output_width, gpu->output_height);
  flush_log();
  sampled = now; previous_simulation = f->simulation;
  previous_presentations = f->presentations; previous_missed = f->missed;
  memset(timings, 0, sizeof(timings)); memset(intervals, 0, sizeof(intervals));
  interval_count = interval_max = 0;
}
void FzeroDiagnosticsStop(void) {
  if (log_file) { fclose(log_file); log_file = NULL; }
}
