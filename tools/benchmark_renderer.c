/* Replay immutable captures without simulation, pacing, SDL or disk I/O in
 * the timed region. Render cost only; this is not a whole-game FPS estimate. */
#define _POSIX_C_SOURCE 200809L
#include "fzero_renderer.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static double seconds(void) {
#ifdef _WIN32
  LARGE_INTEGER value, frequency;
  QueryPerformanceCounter(&value); QueryPerformanceFrequency(&frequency);
  return (double)value.QuadPart / frequency.QuadPart;
#else
  struct timespec value;
  clock_gettime(CLOCK_MONOTONIC, &value);
  return value.tv_sec + value.tv_nsec * 1e-9;
#endif
}

static uint32_t native[FZERO_MAX_WIDTH * 224];
static uint32_t hd[FZERO_MAX_WIDTH * 224 * FZERO_HD_SCALE_MAX * FZERO_HD_SCALE_MAX];

static bool draw(FzeroViewport viewport, double alpha, unsigned scale) {
#ifdef FZERO_RENDERER_COMBINED_PRESENTATION
  if (scale > 1)
    {
      const char *split=getenv("FZERO_BENCHMARK_SPLIT_PASSES");
      bool old_split=split && !strcmp(split,"1");
      return FzeroRendererDrawPresentation(old_split?NULL:native,
          hd,sizeof(hd)/sizeof(*hd),viewport,alpha,scale) &&
          (!old_split || FzeroRendererDraw(native,viewport,alpha));
    }
#else
  if (!FzeroRendererDraw(native, viewport, alpha)) return false;
  if (scale > 1)
    return FzeroRendererDrawHd(hd, sizeof(hd) / sizeof(*hd), viewport, alpha, scale);
#endif
#ifdef FZERO_RENDERER_COMBINED_PRESENTATION
  return FzeroRendererDraw(native, viewport, alpha);
#endif
  return true;
}

static uint64_t digest(const uint32_t *p, size_t count) {
  uint64_t result = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < count; ++i) {
    result ^= p[i]; result *= UINT64_C(1099511628211);
  }
  return result;
}

int main(int argc, char **argv) {
  atexit(FzeroRendererShutdown);
  if (argc < 3 || argc > 4) {
    fputs("usage: FZeroRendererBenchmark capture.bin iterations [next-capture.bin]\n", stderr);
    return 2;
  }
  char *end;
  long requested = strtol(argv[2], &end, 10);
  if (*end || requested < 1 || requested > 10000 || !FzeroRendererLoadCapture(argv[1])) return 2;
  int iterations = (int)requested;
  if (argc == 4 && !FzeroRendererLoadCapture(argv[3])) return 2;
  puts("enhanced,aspect,width,scale,alpha,median_ms,native_digest,hd_digest");
  unsigned max_scale=4;const char *maximum=getenv("FZERO_BENCHMARK_MAX_SCALE");
  if(maximum) {max_scale=(unsigned)strtoul(maximum,NULL,10);if(!FzeroValidHdScale(max_scale))return 2;}
  for (int setting = 0; setting < 4; ++setting) {
    static const char *aspects[] = {"4:3", "16:9", "21:9", "32:9"};
    FzeroVideoSettings settings;
    FzeroVideoStock(&settings);
    settings.enhanced = setting != 0;
    const char *aspect = aspects[setting];
    if (!FzeroParseAspect(aspect, &settings.aspect)) return 2;
    FzeroViewport viewport = FzeroCalculateViewport(&settings, 5120, 1440);
    for (unsigned scale = 1; scale <= max_scale; scale=scale==1?2:scale+2) {
      for (int blend = 0; blend < (argc == 4 ? 2 : 1); ++blend) {
        double alpha = blend ? 0.5 : 1;
        for (int i = 0; i < 3; ++i) if (!draw(viewport, alpha, scale)) return 3;
        double samples[3];
        for (int repeat = 0; repeat < 3; ++repeat) {
          double start = seconds();
          for (int i = 0; i < iterations; ++i) if (!draw(viewport, alpha, scale)) return 3;
          samples[repeat] = (seconds() - start) * 1000 / iterations;
        }
        for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j)
          if (samples[i] > samples[j]) { double t = samples[i]; samples[i] = samples[j]; samples[j] = t; }
        size_t count = (size_t)viewport.width * 224;
        printf("%d,%s,%d,%u,%.1f,%.6f,%016" PRIx64 ",%016" PRIx64 "\n",
            viewport.enhanced, aspect, viewport.width, scale, alpha, samples[1],
            digest(native, count),
            scale > 1 ? digest(hd, count * scale * scale) : 0);
        fflush(stdout);
      }
    }
  }
  return 0;
}
