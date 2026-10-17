/*
 * Headless F-Zero bring-up / soak host.
 *
 * No window or launcher: it boots the recompiled game, runs a fixed
 * number of frames and reports whether the run showed the activity a real boot
 * produces (WRAM churn, a non-uniform framebuffer that keeps changing, and
 * audible DSP output). Used for standup validation and for harvesting the
 * interpreter-tier coverage profile that feeds the next regeneration.
 */

#include "fzero_runtime.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay.h"
#include "fzero_vehicles.h"
#include "fzero_tracks.h"
#include "fzero_course_runtime.h"
#include "fzero_msu.h"
#include "fzero_state_mode.h"
#include "fzero_replay.h"
#include "fzero_capture.h"
#include "fzero_local.h"
#include "fzero_local_mode.h"
#include "fzero_vs_menu.h"
#include "snapshot_guard.h"
#include "desktop/sdl_compat.h"
#include "../tests/fzero_local_driver.h"

#include "audio_trace.h"
#include "common_rtl.h"
#include "save_state_slots.h"
#include "cpu_state.h"
#include "sha256.h"
#include "snes_rewind.h"
#include "snes/apu.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include "snes/dsp.h"
#include "snes/ppu.h"
#include "snes/snes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* F-Zero (USA), 512 KiB LoROM, headerless. */
static const uint8_t kFzeroSha256[32] = {
    0xbf, 0x16, 0xc3, 0xc8, 0x67, 0xc5, 0x8e, 0x2a,
    0xb0, 0x61, 0xc7, 0x0d, 0xe9, 0x29, 0x5b, 0x69,
    0x30, 0xd6, 0x3f, 0x29, 0xf8, 0x1c, 0xc9, 0x86,
    0xf5, 0xec, 0xae, 0x03, 0xe0, 0xad, 0x18, 0xd2,
};

enum { kFzeroRomSize = 0x80000u, kMaxInputSpans = 128 };

typedef struct AttractStats {
  uint64_t logic_hash;
  uint64_t video_hash;
  uint64_t logic_changes;
  uint64_t video_changes;
  uint64_t video_active_frames;
  uint64_t audio_active_frames;
  uint32_t audio_peak;
  uint64_t audio_underruns;
} AttractStats;

typedef struct WavWriter {
  FILE *stream;
  uint32_t data_bytes;
} WavWriter;

typedef struct InputSpan {
  long first;
  long last;
  uint32_t mask;
} InputSpan;

void headless_install_exception_filter(void);
void FzeroActorLiveInstall(void);
int FzeroActorLiveFinish(void);

/* Private-ROM replay fixtures. Entries are: first last WRAM-address hex-bytes.
 * Kept in the headless host: no memory editing interface in the shipped app. */
typedef struct WramEvent {
  long first, last;
  unsigned address, size;
  uint8_t bytes[128];
} WramEvent;
static WramEvent wram_events[128];
static unsigned wram_event_count;
static FILE *wram_trace;
static bool replay_wram_init(void) {
  const char *path = getenv("FZERO_TEST_WRAM_SCRIPT");
  if (path) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[512], hex[257], extra;
    while (fgets(line, sizeof(line), f)) {
      if (line[0] == '#' || line[0] == '\n') continue;
      if (wram_event_count == 128) { fclose(f); return false; }
      WramEvent *e = &wram_events[wram_event_count];
      if (sscanf(line, "%ld %ld %x %256s %c", &e->first, &e->last,
                 &e->address, hex, &extra) != 4 || e->first < 0 || e->last < e->first ||
          strlen(hex) % 2 || !strlen(hex)) { fclose(f); return false; }
      e->size = (unsigned)strlen(hex) / 2;
      if (e->address >= sizeof(g_ram) || e->size > sizeof(g_ram) - e->address) {
        fclose(f); return false;
      }
      for (unsigned i = 0; i < e->size; ++i) {
        char pair[3] = {hex[i*2], hex[i*2+1], 0}, *end;
        unsigned long value = strtoul(pair, &end, 16);
        if (*end) { fclose(f); return false; }
        e->bytes[i] = (uint8_t)value;
      }
      ++wram_event_count;
    }
    bool ok = !ferror(f); fclose(f); if (!ok) return false;
  }
  path = getenv("FZERO_TEST_WRAM_TRACE");
  if (path && !(wram_trace = fopen(path, "wb"))) return false;
  return true;
}
static void replay_wram_before(long frame) {
  for (unsigned i = 0; i < wram_event_count; ++i) {
    const WramEvent *e = &wram_events[i];
    if (frame >= e->first && frame <= e->last)
      memcpy(g_ram + e->address, e->bytes, e->size);
  }
}
static void finish_rank_fixture(CpuState *cpu, uint32_t pc) {
  (void)pc;
  unsigned rank = (unsigned)strtoul(getenv("FZERO_TEST_FINISH_RANK"), NULL, 10);
  if (rank >= 1 && rank <= 3) cpu_write_a_m(cpu, (uint16_t)rank);
}
static void stock_landing_probe(CpuState *cpu, uint32_t pc) {
  (void)cpu; (void)pc; /* Run the native instructions as a negative control. */
}

static uint64_t fnv1a_update(uint64_t hash, const void *data, size_t size) {
  const uint8_t *bytes = (const uint8_t *)data;
  for (size_t i = 0; i < size; i++) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t logic_hash(void) {
  if(FzeroLocalSession())return FzeroVersusHash(FzeroLocalSession());
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = fnv1a_update(hash, g_ram, sizeof(g_ram));
  if (g_snes->cart->ram && g_snes->cart->ramSize)
    hash = fnv1a_update(hash, g_snes->cart->ram, g_snes->cart->ramSize);
  return hash;
}
#include "../tests/fzero_local_catalog_test.h"

static uint8_t *read_rom(const char *path, size_t *size_out,
                         uint8_t hash[32]) {
  FILE *stream = fopen(path, "rb");
  if (!stream) return NULL;
  if (fseek(stream, 0, SEEK_END) != 0) {
    fclose(stream);
    return NULL;
  }
  long length = ftell(stream);
  if (length <= 0 || fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    return NULL;
  }
  uint8_t *rom = (uint8_t *)malloc((size_t)length);
  if (!rom || fread(rom, 1, (size_t)length, stream) != (size_t)length) {
    free(rom);
    fclose(stream);
    return NULL;
  }
  fclose(stream);
  size_t skip = (size_t)length % 1024u == 512u ? 512u : 0u;
  *size_out = (size_t)length - skip;
  if (skip) memmove(rom, rom + skip, *size_out);
  sha256_compute(rom, *size_out, hash);
  return rom;
}

static void print_hash(const uint8_t hash[32]) {
  for (unsigned i = 0; i < 32; i++) fprintf(stderr, "%02x", hash[i]);
}

static void write_u16(FILE *stream, uint16_t value) {
  fputc(value & 0xff, stream);
  fputc(value >> 8, stream);
}

static void write_u32(FILE *stream, uint32_t value) {
  write_u16(stream, value & 0xffff);
  write_u16(stream, value >> 16);
}

static int wav_open(WavWriter *writer, const char *path) {
  memset(writer, 0, sizeof(*writer));
  if (!path || !path[0]) return 1;
  writer->stream = fopen(path, "wb");
  if (!writer->stream) return 0;
  fwrite("RIFF", 1, 4, writer->stream);
  write_u32(writer->stream, 0);
  fwrite("WAVEfmt ", 1, 8, writer->stream);
  write_u32(writer->stream, 16);
  write_u16(writer->stream, 1);
  write_u16(writer->stream, 2);
  write_u32(writer->stream, 32040);
  write_u32(writer->stream, 32040 * 4);
  write_u16(writer->stream, 4);
  write_u16(writer->stream, 16);
  fwrite("data", 1, 4, writer->stream);
  write_u32(writer->stream, 0);
  return ferror(writer->stream) == 0;
}

static int wav_append(WavWriter *writer, const int16_t *audio, int frames) {
  if (!writer->stream) return 1;
  size_t bytes = (size_t)frames * 2u * sizeof(audio[0]);
  if (writer->data_bytes > UINT32_MAX - bytes) return 0;
  if (fwrite(audio, 1, bytes, writer->stream) != bytes) return 0;
  writer->data_bytes += (uint32_t)bytes;
  return 1;
}

static int wav_close(WavWriter *writer) {
  if (!writer->stream) return 1;
  fseek(writer->stream, 4, SEEK_SET);
  write_u32(writer->stream, writer->data_bytes + 36u);
  fseek(writer->stream, 40, SEEK_SET);
  write_u32(writer->stream, writer->data_bytes);
  int ok = ferror(writer->stream) == 0 && fclose(writer->stream) == 0;
  writer->stream = NULL;
  return ok;
}

static int write_ppm(const char *path, const uint8_t *pixels, int width) {
  if (!path || !path[0]) return 1;
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  fprintf(stream, "P6\n%d %d\n255\n", width,FzeroFrameHeight());
  for (size_t i = 0; i < (size_t)width * FzeroFrameHeight(); i++) {
    uint8_t rgb[3] = {pixels[i * 4u + 2u], pixels[i * 4u + 1u],
                      pixels[i * 4u]};
    if (fwrite(rgb, 1, sizeof(rgb), stream) != sizeof(rgb)) {
      fclose(stream);
      return 0;
    }
  }
  return fclose(stream) == 0;
}

static int write_wram_dump(const char *path) {
  if (!path || !path[0]) return 1;
  FILE *stream = fopen(path, "wb");
  if (!stream) return 0;
  int ok = fwrite(g_ram, 1, sizeof(g_ram), stream) == sizeof(g_ram);
  if (fclose(stream) != 0) ok = 0;
  return ok;
}

static void collect_video(AttractStats *stats, const uint8_t *pixels,
                          long frame, int width) {
  uint64_t hash = fnv1a_update(UINT64_C(14695981039346656037), pixels,
                               (size_t)width * FzeroFrameHeight() * 4u);
  if (frame && hash != stats->video_hash) stats->video_changes++;
  stats->video_hash = hash;
  const uint32_t *words = (const uint32_t *)pixels;
  uint32_t first = words[0] & 0xffffffu;
  for (size_t i = 1; i < (size_t)width * FzeroFrameHeight(); i++) {
    if ((words[i] & 0xffffffu) != first) {
      stats->video_active_frames++;
      break;
    }
  }
}

static void collect_audio(AttractStats *stats, const int16_t *audio,
                          int frames) {
  int active = 0;
  for (int i = 0; i < frames * 2; i++) {
    int64_t value = audio[i];
    uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
    if (magnitude) active = 1;
    if (magnitude > stats->audio_peak) stats->audio_peak = magnitude;
  }
  if (active) stats->audio_active_frames++;
}

static int parse_input_script(InputSpan spans[kMaxInputSpans],
                              size_t *count_out,bool versus) {
  const char *cursor = getenv("SNESRECOMP_INPUT_SCRIPT");
  *count_out = 0;
  if (!cursor || !cursor[0]) return 1;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') cursor++;
    if (!*cursor) break;
    if (*count_out >= kMaxInputSpans) return 0;
    char *end = NULL;
    long first = strtol(cursor, &end, 0);
    if (end == cursor || first < 0) return 0;
    cursor = end;
    long last = first;
    if (*cursor == '-') {
      cursor++;
      last = strtol(cursor, &end, 0);
      if (end == cursor || last < first) return 0;
      cursor = end;
    }
    if (*cursor++ != ':') return 0;
    unsigned long mask = strtoul(cursor, &end, 0);
    if (end == cursor || mask > (versus?0xffffffu:0xfffu)) return 0;
    cursor = end;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor && *cursor != ',') return 0;
    spans[*count_out].first = first;
    spans[*count_out].last = last;
    spans[*count_out].mask = (uint32_t)mask;
    (*count_out)++;
  }
  return 1;
}

static uint32_t scripted_input(const InputSpan *spans, size_t count,
                               long frame) {
  uint32_t input = 0;
  for (size_t i = 0; i < count; i++)
    if (frame >= spans[i].first && frame <= spans[i].last)
      input |= spans[i].mask;
  return input;
}

static bool local_state_atomic_test(void) {
  size_t size=RtlSaveSnapshotToMemory(NULL,0);
  uint8_t *before=malloc(size),*bad=malloc(size),*after=malloc(size);
  bool ok=before && bad && after && RtlSaveSnapshotToMemory(before,size)==size;
  uint64_t hash=FzeroVersusHash(FzeroLocalSession()),master=g_cpu.master_cycles;
  for(unsigned test=0;ok && test<6;++test) {
    memcpy(bad,before,size);size_t length=size;
    if(test==0)bad[16]^=1; /* namespace identity */
    if(test==1)bad[256]^=1; /* native guest */
    if(test==2)bad[size-1]^=1; /* integrity */
    if(test==3)--length;
    if(test==4)length=8;
    if(test==5) {
      /* Even a correctly resealed envelope cannot carry an unsupported
       * course in the host prefix. Validate before touching the guest. */
      size_t prefix=SNES_SNAPSHOT_GUARD_PREFIX+RtlSnapshotGuestSize(NULL);
      bad[prefix+24]=255;bad[prefix+25]=255;
      ok=snes_snapshot_guard_finish(bad,size,size-SNES_SNAPSHOT_GUARD_OVERHEAD,
          FzeroLocalGameInfo()->snapshot_guard_identity())==size;
    }
    ok=ok && !RtlLoadSnapshotFromMemory(bad,length) &&
        RtlSaveSnapshotToMemory(after,size)==size && !memcmp(before,after,size) &&
        hash==FzeroVersusHash(FzeroLocalSession()) && master==g_cpu.master_cycles;
  }
  free(before);free(bad);free(after);
  if(ok)fputs("local-state: corrupt, truncated, wrong-identity and invalid-course loads rejected before live mutation\n",stderr);
  return ok;
}

int main(int argc, char **argv) {
  /* Preserve the last guest-frame diagnostic if a soak terminates abnormally
   * while stderr is redirected to a qualification log. */
  setvbuf(stderr, NULL, _IONBF, 0);
  headless_install_exception_filter();
  bool versus=argc==7 && !strcmp(argv[3],"--versus");
  if (argc < 2 || (argc > 3 && !versus)) {
    fprintf(stderr, "usage: FZeroSNESRecompHeadless <fzero.sfc> [frames [--versus COURSE P1 P2]]\n");
    return 2;
  }
  FzeroVersusConfig versus_config={0};
  if(versus) {
    unsigned values[3];
    for(unsigned i=0;i<3;++i) {
      char *end;unsigned long value=strtoul(argv[4+i],&end,10);
      if(!argv[4+i][0] || *end || value>=(i?12u:UINT16_MAX)) {fputs("Invalid versus configuration\n",stderr);return 2;}
      values[i]=(unsigned)value;
    }
    versus_config=(FzeroVersusConfig){(uint16_t)values[0],{(uint8_t)values[1],(uint8_t)values[2]}};
  }
  long frame_limit = argc >= 3 ? strtol(argv[2], NULL, 10) : 3600;
  if (frame_limit < 1 || frame_limit > 1000000) {
    fprintf(stderr, "frames must be between 1 and 1000000\n");
    return 2;
  }

  size_t rom_size = 0;
  uint8_t rom_hash[32];
  uint8_t *rom = read_rom(argv[1], &rom_size, rom_hash);
  if (!rom) {
    fprintf(stderr, "unable to read ROM: %s\n", argv[1]);
    return 2;
  }
  if (rom_size != kFzeroRomSize ||
      memcmp(rom_hash, kFzeroSha256, sizeof(rom_hash)) != 0) {
    fprintf(stderr, "unsupported F-Zero ROM (size=%zu sha256=", rom_size);
    print_hash(rom_hash);
    fputs(")\n", stderr);
    free(rom);
    return 2;
  }

  const char *track_root = getenv("FZERO_TRACK_PACKS");
  if (!FzeroTracksInit(track_root ? track_root : "mods/track-packs",
#ifdef FZERO_HAS_DELUXE
                       true
#else
                       false
#endif
                       )) { fprintf(stderr, "%s\n", FzeroTracksError()); free(rom); return 2; }
  const char *deluxe_data = getenv("FZERO_DELUXE_DATA");
  FzeroGameplayHeadless(deluxe_data && *deluxe_data);
  if(versus) {
    bool bs_tracks=FzeroBsTracks();
    FzeroGameplaySettings stock={0};FzeroGameplayConfigure(&stock,false,bs_tracks);
    if(!FzeroLocalStockTables(rom,rom_size))return 12;
  }
  FzeroVideoSettings capture_video;
  FzeroVideoStock(&capture_video);
  const char *replay_config=getenv("FZERO_REPLAY_VIDEO");
  if (replay_config && *replay_config) {
    if (!FzeroVideoLoad(&capture_video,replay_config)) return 11;
    if(!versus)FzeroGameplayConfigure(&capture_video.gameplay,capture_video.bs_deluxe,capture_video.bs_tracks);
  } else {
    capture_video.gameplay=*FzeroGameplaySettingsCurrent();
    capture_video.bs_deluxe=FzeroBsCars(); capture_video.bs_tracks=FzeroBsTracks();
  }
  const char *music=getenv("SNESRECOMP_MSU1");
  bool music_requested=music && *music && strcmp(music,"off") && strcmp(music,"0");
  bool configured_music=versus?FzeroMsuConfigureVersus(music,
      !FzeroMsuHasLegacyPatch(music),argv[1]):
      FzeroMsuConfigure(music,FzeroRuleEnabled(FZERO_RULE_MSU),argv[1]);
  if(versus && music_requested)
    capture_video.gameplay.enabled|=1u<<FZERO_RULE_MSU;
  if (!configured_music) {
    fprintf(stderr,"[fzero-msu1] %s\n",FzeroMsuError());free(rom);return 2;
  }
  bool mode_test=getenv("FZERO_TEST_VS_FLOW")!=NULL;
  if(mode_test) {
    const char *root=getenv("SNESRECOMP_SAVE_ROOT");if(root)RtlSetSaveRoot(root);
    if(!FzeroLocalModeInit(rom,rom_size,argv[1],deluxe_data,getenv("SNESRECOMP_MSU1"),FzeroRuleEnabled(FZERO_RULE_MSU)))return 12;
  }
  if (!FzeroTracksPrepare(&rom, &rom_size, deluxe_data && *deluxe_data, deluxe_data)) {
    fprintf(stderr, "[content] %s %s\n", FzeroTracksError(), FzeroDeluxeError());
    free(rom);
    return 2;
  }
  RtlRegisterGame(versus?FzeroLocalGameInfo():FzeroGameInfo());
  if (!SnesInit(rom, (int)rom_size)) {
    fputs("failed to initialize the F-Zero cartridge\n", stderr);
    free(rom);
    return 3;
  }
  const char *save_root = getenv("SNESRECOMP_SAVE_ROOT");
  if (save_root && save_root[0]) RtlSetSaveRoot(save_root);
  if(versus && (!FzeroLocalConfigure(versus_config) || !FzeroLocalSelectSaveRoot())) {
    fputs("Cannot initialize stock local versus/save namespace\n",stderr);return 12;
  }
  if (!versus && !FzeroTracksSelectSaveRoot()) {
    fprintf(stderr, "%s\n", FzeroDeluxeError());
    free(rom);
    return 3;
  }
  if (!versus && !FzeroMsuSelectSaveRoot()) {
    fprintf(stderr, "%s\n", FzeroMsuError());
    free(rom);
    return 3;
  }
  RtlReadSram();
  FzeroVsMenuConfigure(capture_video.local_two_player || mode_test || getenv("FZERO_TEST_VS_MENU")!=NULL);
  if (!FzeroCaptureReplayLoad()) return 11;
  const char *capture_directory=getenv("FZERO_CAPTURE_DIR");
  if (capture_directory && !FzeroCaptureStart(true,capture_directory,&capture_video)) return 11;
  /* Reproduce a reported transition from a private, mode-checked snapshot. */
  const char *initial_state = getenv("FZERO_STATE_LOAD");
  if (initial_state && *initial_state) {
    if (!FzeroStateFileAcceptable(initial_state) || !RtlLoadSnapshot(initial_state)) {
      fprintf(stderr, "unable to load compatible state: %s\n", initial_state);
      free(rom);
      return 3;
    }
  }

  InputSpan input_spans[kMaxInputSpans];
  size_t input_span_count = 0;
  if (!parse_input_script(input_spans, &input_span_count,versus)) {
    fputs("invalid SNESRECOMP_INPUT_SCRIPT; expected FIRST[-LAST]:MASK "
          "entries\n",
          stderr);
    free(rom);
    return 2;
  }
  if (!FzeroReplayConfigure(NULL, getenv("FZERO_VIEWPORT_SCRIPT"))) {
    fputs("invalid FZERO_VIEWPORT_SCRIPT\n", stderr);
    free(rom); return 2;
  }
  FzeroVideoSettings replay_video;
  FzeroVideoStock(&replay_video); /* headless baseline is stock; FZERO_ASPECT opts in */
  const char *initial_aspect = getenv("FZERO_ASPECT");
  if (initial_aspect && FzeroParseAspect(initial_aspect, &replay_video.aspect)) replay_video.enhanced = true;

  int frame_width = FzeroFrameWidth();
  int drawable_width = 768, drawable_height = 576;
  FzeroSetViewport(FzeroCalculateViewport(&replay_video, drawable_width, versus?drawable_height/2:drawable_height));
  frame_width = FzeroFrameWidth();
  static uint8_t pixels[FZERO_MAX_WIDTH * 448u * 4u];
  int16_t audio[600 * 2];
  FzeroBeginDrawing(pixels, (size_t)frame_width * 4u);
  const char *present_text=getenv("FZERO_TEST_PRESENT_EVERY");
  unsigned present_every=present_text?(unsigned)strtoul(present_text,NULL,10):1;
  if(!present_every || present_every>120)return 2;
  if(versus && present_every>1)FzeroSetDeferredPresentation(true);

  AttractStats stats = {0};
  WavWriter wav;
  if (!wav_open(&wav, getenv("SNESRECOMP_WAV"))) {
    fputs("unable to open WAV capture\n", stderr);
    free(rom);
    return 4;
  }
  double audio_accumulator = 0.0;
  const char *dsp_trace_path=getenv("FZERO_TEST_DSP_TRACE");
  FILE *dsp_trace=dsp_trace_path?fopen(dsp_trace_path,"wb"):NULL;
  if(dsp_trace_path && !dsp_trace)return 10;
  /* Private validation: replay ten frames across an actual disk snapshot. */
  const char *lifecycle = getenv("FZERO_LIFECYCLE_TEST");
  static uint8_t replay_expected[0x20000];
  static uint8_t replay_apu[0x10000];
  uint64_t replay_master = 0;
  uint64_t replay_versus = 0;
  const char *progress_test = getenv("FZERO_LIBRARY_PROGRESS_TEST");
  long next_completed_result = 1450;
  const char *save_frame_text = getenv("FZERO_TEST_SAVE_FRAME");
  long save_frame = save_frame_text ? strtol(save_frame_text, NULL, 10) : 1500;
  bool teleport_fixture=versus && getenv("FZERO_TEST_LOCAL_TELEPORT")!=NULL;
  bool local_driver=versus && (getenv("FZERO_TEST_LOCAL_DRIVER")!=NULL || teleport_fixture);
  if(teleport_fixture)fputs("qa-teleport: lifecycle fixture; forced poses/health, real gates/laps/finish/results; NOT natural driving validation\n",stderr);
  bool driver_lifecycle=local_driver && getenv("FZERO_LOCAL_DRIVER_LIFECYCLE")!=NULL;
  unsigned driver_checked=0,driver_laps[2]={0,0},driver_status[2]={0,0};
  if(driver_lifecycle)lifecycle="1";
  /* Private records QA: cross each course's actual finish line on the final
   * lap. Native finish, record-writing and cup-completion code still runs. */
  bool records_test = getenv("FZERO_TEST_RECORDS_CUP") != NULL;
  const char *finish_rank_text = getenv("FZERO_TEST_FINISH_RANK");
  unsigned finish_rank = finish_rank_text ? (unsigned)strtoul(finish_rank_text, NULL, 10) : 1;
  if (finish_rank < 1 || finish_rank > 3) finish_rank = 1;
  unsigned records_course = 99, records_race_frames = 0;
  if (!replay_wram_init()) { fputs("Invalid WRAM replay fixture\n", stderr); return 10; }
  if (getenv("FZERO_TEST_STOCK_LANDING"))
    interp_bridge_set_pre_opcode_hook(0x009c9a, stock_landing_probe);
  if (records_test && finish_rank_text)
    interp_bridge_set_pre_opcode_hook(0x1ebb3b, finish_rank_fixture);
  FzeroActorLiveInstall();

  bool ui_cache_written=false;
  if(!catalog_init())return 12;
  unsigned mode_stage=0;static uint8_t menu_ram[0x20000];char menu_root[96];
  FzeroGameplaySettings menu_rules={0};
  /* Recorder stress tests can yield to the bounded background writer instead
   * of submitting thousands of emulated frames per second without a limit. */
  const char *pace = getenv("FZERO_TEST_CAPTURE_PACE");
  for (long frame = 0; frame < frame_limit; frame++) {
    if(pace && frame%8==0)SDL_Delay(2);
    int catalog_result=catalog_step();
    if(catalog_result<0) {fprintf(stderr,"catalog-test: FAIL course=%u\n",catalog_course);return 12;}
    if(catalog_result>0) {frame_limit=frame;break;}
    if(mode_test && !mode_stage && FzeroVsMenuRequested()) {
      /* Start from current vehicle metadata. A virgin Practice workspace
       * otherwise exercises legacy rival-sentinel migration on load too. */
      FzeroVehiclesLoaded();
      memcpy(menu_ram,g_ram,sizeof(menu_ram));snprintf(menu_root,sizeof(menu_root),"%s",RtlSaveRoot());
      menu_rules=*FzeroGameplaySettingsCurrent();
      if(!FzeroLocalModeEnter((FzeroVersusConfig){0,{0,1}}))return 12;
      mode_stage=1;FzeroSetViewport(FzeroCalculateViewport(&replay_video,768,288));
      frame_width=FzeroFrameWidth();FzeroBeginDrawing(pixels,(size_t)frame_width*4);
      fprintf(stderr,"mode-flow: entered VS at frame=%ld\n",frame);
    }
    if(mode_test && (mode_stage==1 || mode_stage==2) && FzeroLocalSession() && FzeroVersusCompleted(FzeroLocalSession())->tick>=200) {
      if(mode_stage==1) {
        if(!FzeroLocalModeSetup() || !FzeroLocalConfigure((FzeroVersusConfig){1,{2,3}}))return 12;
        mode_stage=2;fprintf(stderr,"mode-flow: changed course/machines at frame=%ld\n",frame);
      } else {
        if(!FzeroLocalModeReturn() || memcmp(menu_ram,g_ram,sizeof(menu_ram)) || strcmp(menu_root,RtlSaveRoot()) ||
            memcmp(&menu_rules,FzeroGameplaySettingsCurrent(),sizeof(menu_rules)) || FzeroLocalEnabled()) {
          size_t at=0;while(at<sizeof(menu_ram) && menu_ram[at]==g_ram[at])++at;
          fprintf(stderr,"mode-flow: return context mismatch at=%zx expected=%02x actual=%02x root=%s expected-root=%s rules=%x expected-rules=%x enabled=%u\n",
              at,at<sizeof(menu_ram)?menu_ram[at]:0,at<sizeof(menu_ram)?g_ram[at]:0,
              RtlSaveRoot(),menu_root,FzeroGameplaySettingsCurrent()->enabled,menu_rules.enabled,FzeroLocalEnabled());return 12;
        }
        mode_stage=3;FzeroSetViewport(FzeroCalculateViewport(&replay_video,768,576));
        frame_width=FzeroFrameWidth();FzeroBeginDrawing(pixels,(size_t)frame_width*4);
        fprintf(stderr,"mode-flow: returned exact single-player RAM/profile/root at frame=%ld\n",frame);
      }
    }
    if(driver_lifecycle) {
      save_frame=-1;
      const FzeroVersusSnapshot *race=FzeroVersusCompleted(FzeroLocalSession());
      int event=-1;
      if(race) {
        if(race->phase==FZERO_VS_COUNTDOWN && race->tick>=30)event=0;
        if(race->phase==FZERO_VS_RACING && race->race_tick>=200)event=1;
        if(race->phase==FZERO_VS_RACING && race->contact)event=2;
        if((race->racers[0].status==FZERO_VS_FINISHED)!=(race->racers[1].status==FZERO_VS_FINISHED))event=3;
        if(race->phase==FZERO_VS_RESULTS)event=4;
      }
      if(event>=0 && !(driver_checked&(1u<<event))) {
        driver_checked|=1u<<event;save_frame=frame;
        fprintf(stderr,"local-driver: lifecycle event=%d frame=%ld\n",event,frame);
        const char *directory=getenv("FZERO_LOCAL_DRIVER_CHECKPOINTS");
        if(directory) {
          char path[1200];
          if(snprintf(path,sizeof(path),"%s/phase-%d.sav",directory,event)>=(int)sizeof(path) || !RtlSaveSnapshot(path))return 8;
        }
      }
    }
    replay_wram_before(frame);
    bool records_racing = records_test && g_ram[0x54] == 2 && g_ram[0x55] == 3;
    /* Skip only the final winner's fly-away animation after native record
     * writes. A teleported fixture has no physically driven approach path. */
    if (records_racing && g_ram[0xc3] == 0x11 &&
        !getenv("FZERO_TEST_KEEP_FINISH_ANIMATION")) {
      g_ram[0x54] = 3;
      g_ram[0x55] = 1;
      g_ram[0x56] = 0;
      records_racing = false;
    }
    if (!records_racing) records_race_frames = 0;
    if (records_racing && ++records_race_frames == 120 && records_course != g_ram[0x53]) {
      records_course = g_ram[0x53];
      static const uint8_t times[] = {0,0x20,0x52, 0,0x41,0x27, 1,0,0x59,
                                      1,0x20,0x40, 0xff,0xff,0xff};
      memcpy(g_ram + 0xe90, times, sizeof(times));
      memset(g_ram + 0xf30, (int)finish_rank, 6);
      g_ram[0xc0] = 1; g_ram[0xc1] = 0x30; g_ram[0xc2] = 0x32;
      g_ram[0xd40] = 4; g_ram[0xd41] = 0x80; g_ram[0xd00] = 0;
      g_ram[0xb70] = 0x90; g_ram[0xb71] = g_ram[0xae];
      g_ram[0xb90] = 0x80; g_ram[0xb91] = g_ram[0xaf];
      g_ram[0xdc8] = (uint8_t)finish_rank;
      fprintf(stderr, "records-fixture: finish crossing course=%u frame=%ld\n",
              records_course, frame);
    }
    /* Export a private replay fixture without the lifecycle test's reset or
     * speculative execution. The normal mode/signature guard still applies
     * when this snapshot is loaded by either host. */
    const char *export_state = getenv("FZERO_STATE_SAVE");
    if(versus && getenv("FZERO_LOCAL_STATE_ATOMIC_TEST") && frame==save_frame && !local_state_atomic_test())return 8;
    if (export_state && *export_state && frame == save_frame) {
      if (!RtlSaveSnapshot(export_state)) {
        fputs("fixture: snapshot export failed\n", stderr);
        return 8;
      }
      fprintf(stderr, "fixture: saved frame %ld to %s\n", frame, export_state);
    }
    if (getenv("FZERO_REWIND_TEST") && frame == save_frame) {
      size_t cap = 2u * 1024u * 1024u;
      uint8_t *initial = malloc(cap);
      size_t length = initial ? RtlSaveSnapshotToMemory(initial, cap) : 0;
      if (!length) return 8;
      static uint8_t initial_ram[0x20000];
      memcpy(initial_ram, g_ram, sizeof(initial_ram));
      uint64_t initial_master = g_cpu.master_cycles;
      uint64_t initial_versus=FzeroVersusHash(FzeroLocalSession());
      snes_rewind_set_defaults(1, 16, 1);
      snes_rewind_configure();
      snes_rewind_note_frame();
      for (long n = frame; n < frame + 10; ++n) {
        (void)RtlRunFrame(scripted_input(input_spans, input_span_count, n));
        if (g_fail || !FzeroLastLleResult()) return 8;
        FzeroDrawPpuFrame();
        snes_rewind_note_frame();
      }
      memcpy(replay_expected, g_ram, sizeof(replay_expected));
      memcpy(replay_apu,g_snes->apu->ram,sizeof(replay_apu));
      replay_master = g_cpu.master_cycles;
      replay_versus=FzeroVersusHash(FzeroLocalSession());
      if (getenv("FZERO_VEHICLE_CROSS_STATE") && FzeroVehicleCount()>4) {
        g_ram[0x14dff]=FzeroVehicleSelected()>=4?0:4;
        FzeroVehiclesLoaded();
      }
      if (!snes_rewind_open()) {fputs("rewind: ring open failed\n",stderr);return 8;}
      for (unsigned i=0;i<10;++i) snes_rewind_step(-1);
      snes_rewind_commit();
      if (memcmp(initial_ram,g_ram,sizeof(initial_ram)) || initial_master!=g_cpu.master_cycles ||
          initial_versus!=FzeroVersusHash(FzeroLocalSession())) {
        fprintf(stderr,"rewind: restored master=%llu/%llu versus=%016llx/%016llx\n",
            (unsigned long long)g_cpu.master_cycles,(unsigned long long)initial_master,
            (unsigned long long)FzeroVersusHash(FzeroLocalSession()),(unsigned long long)initial_versus);
        for (unsigned i=0;i<sizeof(initial_ram);++i) if(initial_ram[i]!=g_ram[i]) {
          fprintf(stderr,"rewind restore mismatch $%05x: %02x -> %02x\n",i,initial_ram[i],g_ram[i]); break;
        }
        return 8;
      }
      for (long n = frame; n < frame + 10; ++n) {
        (void)RtlRunFrame(scripted_input(input_spans, input_span_count, n));
        if (g_fail || !FzeroLastLleResult()) return 8;
        FzeroDrawPpuFrame();
      }
      if (memcmp(replay_expected,g_ram,sizeof(replay_expected)) || replay_master!=g_cpu.master_cycles ||
          replay_versus!=FzeroVersusHash(FzeroLocalSession()) || memcmp(replay_apu,g_snes->apu->ram,sizeof(replay_apu))) {
        fprintf(stderr,"rewind: resimulated master=%llu/%llu versus=%016llx/%016llx\n",
            (unsigned long long)g_cpu.master_cycles,(unsigned long long)replay_master,
            (unsigned long long)FzeroVersusHash(FzeroLocalSession()),(unsigned long long)replay_versus);
        for (unsigned i=0;i<sizeof(replay_expected);++i) if(replay_expected[i]!=g_ram[i]) {
          fprintf(stderr,"rewind replay mismatch $%05x: %02x -> %02x\n",i,replay_expected[i],g_ram[i]); break;
        }
        return 8;
      }
      if (!RtlLoadSnapshotFromMemory(initial,length)) return 8;
      free(initial);
      snes_rewind_shutdown();
      fputs("rewind: actual ring restore and ten-frame resimulation identical (RAM, SPC RAM, master clock, versus hash)\n",stderr);
    }
    /* Private integration check: inject a completed results state, then let
     * the unmodified GP transition routine advance/load/finish the cup.
     * This tests queue boundaries, not driving or finish-line detection. */
    if(progress_test && FzeroTracksActive() && frame>=next_completed_result &&
       g_ram[0x54]==2 && g_ram[0x55]==3) {
      fprintf(stderr,"library-progress: completed result ordinal=%u at frame=%ld\n",g_ram[0x53],frame);
      g_ram[0x54]=3;g_ram[0x55]=1;g_ram[0x56]=4;g_ram[0x60]=0;
      next_completed_result=frame+1000;
    }

    if (lifecycle && frame == save_frame) {
      RtlEnsureSaveDir();
      char path[1024]; RtlSaveSlotPath(11, path, sizeof(path));
      if (!RtlSaveSnapshot(path)) { fputs("lifecycle: save failed\n", stderr); return 8; }
      for (long n = frame; n < frame + 10; ++n) {
        (void)RtlRunFrame(scripted_input(input_spans, input_span_count, n));
        if (g_fail || !FzeroLastLleResult()) return 8;
        FzeroDrawPpuFrame();
      }
      memcpy(replay_expected, g_ram, sizeof(replay_expected));
      memcpy(replay_apu,g_snes->apu->ram,sizeof(replay_apu));
      replay_master = g_cpu.master_cycles;
      replay_versus=FzeroVersusHash(FzeroLocalSession());
      if (getenv("FZERO_VEHICLE_CROSS_STATE") && FzeroVehicleCount()>4) {
        g_ram[0x14dff]=FzeroVehicleSelected()>=4?0:4;
        FzeroVehiclesLoaded();
        fputs("lifecycle: switched vehicle cohort before restoring snapshot\n",stderr);
      }
      if (!RtlLoadSnapshot(path)) { fputs("lifecycle: load failed\n", stderr); return 8; }
      /* Compare identical replay paths. The normal host loop drains audio;
       * the speculative pass above does not, so comparing against that loop
       * also measured audio-service scheduling instead of snapshot fidelity. */
      for (long n = frame; n < frame + 10; ++n) {
        (void)RtlRunFrame(scripted_input(input_spans, input_span_count, n));
        if (g_fail || !FzeroLastLleResult()) return 8;
        FzeroDrawPpuFrame();
      }
      if (memcmp(replay_expected, g_ram, sizeof(replay_expected)) || replay_master != g_cpu.master_cycles ||
          replay_versus!=FzeroVersusHash(FzeroLocalSession()) || memcmp(replay_apu,g_snes->apu->ram,sizeof(replay_apu))) {
        fputs("lifecycle: resimulation differs after load\n", stderr);
        int reported = 0;
        for (size_t i = 0; i < sizeof(replay_expected) && reported < 12; ++i)
          if (replay_expected[i] != g_ram[i]) {
            fprintf(stderr, "  RAM %05zx: expected %02x got %02x\n", i, replay_expected[i], g_ram[i]);
            ++reported;
          }
        fprintf(stderr, "  master: expected %llu got %llu\n",
                (unsigned long long)replay_master, (unsigned long long)g_cpu.master_cycles);
        return 8;
      }
      fputs("lifecycle: save/load ten-frame resimulation identical (RAM, SPC RAM, master clock, versus hash)\n", stderr);
      if (!RtlLoadSnapshot(path)) return 8;
    }
    if (lifecycle && !versus && frame == 1800) {
      uint64_t before_reset = g_cpu.master_cycles;
      FzeroTracksSavesFinish();
      RtlReset(1); FzeroGameInfo()->session_reset();
      FzeroSetViewport(FzeroCalculateViewport(&replay_video, drawable_width, drawable_height));
      FzeroBeginDrawing(pixels, (size_t)frame_width * 4u);
      if (g_cpu.master_cycles != before_reset) return 8;
      fputs("lifecycle: soft reset, SRAM retained\n", stderr);
    }
    if (FzeroReplayViewport((unsigned)frame, &replay_video)) {
      FzeroReplayWindow((unsigned)frame, &drawable_width, &drawable_height);
      FzeroViewport viewport = FzeroCalculateViewport(&replay_video, drawable_width, versus?drawable_height/2:drawable_height);
      FzeroSetViewport(viewport);
      frame_width = viewport.width;
      FzeroBeginDrawing(pixels, (size_t)frame_width * 4u);
      fprintf(stderr, "[fzero-viewport] frame=%ld width=%d\n", frame, frame_width);
    }
    uint32_t frame_input = scripted_input(input_spans, input_span_count, frame);
    if(teleport_fixture)frame_input=FzeroLocalTeleportFixture(FzeroLocalSession());
    else if(local_driver)frame_input=FzeroLocalDriverInput(FzeroLocalSession());
    if (records_racing) frame_input |= 1;
    if (records_test && g_ram[0x54] == 3 && g_ram[0x55] == 1 &&
        g_ram[0x56] == 3 && !(frame & 15))
      frame_input |= 8;
    if (FzeroCaptureReplayActive()) {
      FzeroViewport viewport;
      int result=FzeroCaptureReplayNext(&frame_input,&viewport);
      if (result<0) return 11;
      if (!result) break;
      FzeroSetViewport(viewport);
      frame_width=viewport.width;
      FzeroBeginDrawing(pixels,(size_t)frame_width*4u);
    }
    /* Private UI fixture: normal public End Race transition, then native
     * explosion playback and results; never used by the race qualification. */
    const char *end_frame=getenv("FZERO_TEST_LOCAL_END_FRAME");
    if(end_frame && frame==strtol(end_frame,NULL,10) && FzeroLocalSession()) {
      FzeroVersusEndRace(FzeroLocalSession());FzeroLocalControlChanged();
    }
    const char *resume_frame=getenv("FZERO_TEST_LOCAL_RESUME_FRAME");
    if(resume_frame && frame==strtol(resume_frame,NULL,10) && FzeroLocalSession()) {
      FzeroVersusResume(FzeroLocalSession());FzeroLocalControlChanged();
      fprintf(stderr,"audio-fixture: resumed VS at frame=%ld\n",frame);
    }
    FzeroCaptureBefore(frame_input,FzeroCalculateViewport(&replay_video,drawable_width,versus?drawable_height/2:drawable_height));
    (void)RtlRunFrame(frame_input);
    if (wram_trace && fwrite(g_ram, 1, 0x2000, wram_trace) != 0x2000) return 10;
    if (getenv("FZERO_SCENE_TRACE"))
      fprintf(stderr, "scene %ld state=%02x,%02x,%02x training=%02x scenery=%02x sound=%02x,%02x,%02x,%02x,%02x msu=%02x,%02x,%02x,%02x brightness=%02x\n",
              frame, g_ram[0x54], g_ram[0x55], g_ram[0x56], g_ram[0x58], g_ram[0x81],
              g_ram[0x45], g_ram[0x46], g_ram[0x47], g_ram[0x48], g_ram[0x49],
              g_ram[0x180], g_ram[0x181], g_ram[0x182], g_ram[0x183], g_snes->ppu->inidisp);
    if (records_test && g_ram[0x54] == 0 && g_ram[0x55] == 2)
      fprintf(stderr, "records-fixture: browser SRAM=%02x%02x%02x mirror=%02x%02x%02x root=%s\n",
              g_sram[5], g_sram[6], g_sram[7], g_ram[0x14805], g_ram[0x14806],
              g_ram[0x14807], RtlSaveRoot());
    if (g_fail || !FzeroLastLleResult()) {
      fprintf(stderr, "fzero_native: runtime failure frame=%ld pc=$%06x bus_fault=%d execution=%d state=%02x,%02x,%02x car=%02x\n",
              frame, (unsigned)FzeroResumePc(), g_fail, FzeroLastLleResult(),
              g_ram[0x54], g_ram[0x55], g_ram[0x56], g_ram[0x52]);
      write_wram_dump(getenv("SNESRECOMP_WRAM_DUMP"));
      wav_close(&wav);
      free(rom);
      return 5;
    }

    uint64_t next_logic = logic_hash();
    if (frame && next_logic != stats.logic_hash) stats.logic_changes++;
    stats.logic_hash = next_logic;

    FzeroDrawPpuFrame();
    if(mode_test || getenv("FZERO_TEST_VS_MENU"))FzeroPresent(1);
    if(versus && present_every>1 && frame%present_every==0)FzeroPresent(1);
    FzeroMsuUpdateVersus();
    FzeroCaptureAfter();
    if(FzeroCaptureReplayFailed())return 11;
    /* Private UI regressions need the final composed frame, including host
     * course/records labels which are absent from the raw PPU captures. */
    const char *frame_dir = getenv("FZERO_TEST_FRAME_DIR");
    if (frame_dir && *frame_dir) {
      char path[1024];
      if (snprintf(path, sizeof(path), "%s/%06ld.ppm", frame_dir, frame) >= (int)sizeof(path) ||
          !write_ppm(path, pixels, frame_width)) return 10;
    }
    if(!ui_cache_written && getenv("FZERO_TEST_UI_CACHE") && FzeroLocalUi()) {
      FILE *cache=fopen(getenv("FZERO_TEST_UI_CACHE"),"wb");
      if(!cache || fwrite(FzeroLocalUi(),sizeof(*FzeroLocalUi()),1,cache)!=1 || fclose(cache))return 10;
      ui_cache_written=true;
    }
    collect_video(&stats, pixels, frame, frame_width);

    audio_accumulator += 32040.0 / 60.098811862;
    int audio_frames = (int)audio_accumulator;
    audio_accumulator -= audio_frames;
    memset(audio, 0, sizeof(audio));
    RtlRenderAudio(audio, audio_frames, 2);
    if(dsp_trace) {
      uint8_t registers[128];
      for(unsigned i=0;i<128;++i)registers[i]=dsp_read(g_snes->apu->dsp,(uint8_t)i);
      if(fwrite(registers,1,sizeof(registers),dsp_trace)!=sizeof(registers))return 10;
    }
    collect_audio(&stats, audio, audio_frames);
    if (!wav_append(&wav, audio, audio_frames)) {
      fputs("unable to write WAV capture\n", stderr);
      FzeroCaptureStop();
      wav_close(&wav);
      free(rom);
      return 7;
    }
    if(local_driver && FzeroLocalSession()) {
      const FzeroVersusSnapshot *race=FzeroVersusCompleted(FzeroLocalSession());
      for(unsigned i=0;i<2;++i)if(driver_laps[i]!=race->racers[i].completed_laps || driver_status[i]!=race->racers[i].status) {
        driver_laps[i]=race->racers[i].completed_laps;driver_status[i]=race->racers[i].status;
        fprintf(stderr,"local-driver: frame=%ld player=%u laps=%u status=%u race=%llu\n",frame,i+1,
            driver_laps[i],driver_status[i],(unsigned long long)race->race_tick);
      }
      if(!catalog_output && race->phase==FZERO_VS_RESULTS && (!driver_lifecycle || (driver_checked&16))) {
        frame_limit=frame+1;break;
      }
    }
  }

  if (wram_trace && fclose(wram_trace)) return 10;
  if (dsp_trace && fclose(dsp_trace)) return 10;
  if (getenv("FZERO_TEST_QUICK_SLOT")) {
    /* Quick load must still find the saved race after the host returns to
     * a title or selects a different course's battery-record namespace. */
    char original_root[96], path[256], next_path[256];
    snprintf(original_root, sizeof(original_root), "%s", RtlSaveRoot());
    RtlEnsureSaveDir();
    RtlSaveSlotPath(SNES_SAVESTATE_QUICK_SLOT, path, sizeof(path));
    if (!RtlSaveSnapshot(path)) return 8;
    static uint8_t saved_ram[0x20000];
    memcpy(saved_ram, g_ram, sizeof(saved_ram));
    uint64_t saved_versus=FzeroVersusHash(FzeroLocalSession());
    RtlSetSaveRoot("quick-root-test-unused");
    RtlSaveSlotPath(SNES_SAVESTATE_QUICK_SLOT, next_path, sizeof(next_path));
    if (strcmp(path, next_path) || !RtlLoadSnapshot(next_path) ||
        memcmp(saved_ram, g_ram, sizeof(saved_ram)) || saved_versus!=FzeroVersusHash(FzeroLocalSession())) return 8;
    RtlSetSaveRoot(original_root);
    fprintf(stderr, "quick-slot: saved/loaded across course namespaces at %s\n", path);
  }
  /* Private verification artifact: inspect the actual composed cartridge,
   * including the selected Practice rival's relocated art and metadata. */
  const char *cart_dump = getenv("FZERO_CART_DUMP");
  if (cart_dump && *cart_dump) {
    FILE *f = fopen(cart_dump, "wb");
    if (!f) return 10;
    bool ok = fwrite(g_snes->cart->rom, 1, g_snes->cart->romSize, f) == g_snes->cart->romSize;
    if (fclose(f) || !ok) return 10;
  }
  int output_ok = wav_close(&wav) &&
                  write_ppm(getenv("SNESRECOMP_FRAME_DUMP"), pixels,
                            frame_width) &&
                  write_wram_dump(getenv("SNESRECOMP_WRAM_DUMP"));
  const char *sram_dump = getenv("FZERO_TEST_SRAM_DUMP");
  /* Inspect the actual SPC upload, independently of course metadata/logs. */
  const char *apu_dump = getenv("FZERO_TEST_APURAM_DUMP");
  if (apu_dump && *apu_dump) {
    FILE *f = fopen(apu_dump, "wb");
    if (!f) return 10;
    bool ok = fwrite(g_snes->apu->ram, 1, 0x10000, f) == 0x10000;
    if (fclose(f) || !ok) return 10;
  }
  if (sram_dump && *sram_dump) {
    FILE *f = fopen(sram_dump, "wb");
    if (!f) return 10;
    bool ok = fwrite(g_sram, 1, g_sram_size, f) == (size_t)g_sram_size;
    if (fclose(f) || !ok) return 10;
  }
  const char *dsp_dump = getenv("FZERO_TEST_DSP_DUMP");
  if (dsp_dump && *dsp_dump) {
    FILE *f = fopen(dsp_dump, "wb");
    if (!f) return 10;
    uint8_t registers[128];
    for(unsigned i=0;i<128;++i)registers[i]=dsp_read(g_snes->apu->dsp,(uint8_t)i);
    bool ok=fwrite(registers,1,sizeof(registers),f)==sizeof(registers);
    if(fclose(f) || !ok)return 10;
  }
  uint32_t audio_samples = g_snes->apu->dsp->sampleWrite;
  AudioTraceStats audio_stats;
  audio_trace_get_stats(&audio_stats);
  stats.audio_underruns = audio_stats.output_underflows;

  int qualified =
      FzeroCaptureReplayActive() || frame_limit < 600 ||
      (stats.logic_changes >= (uint64_t)(frame_limit / 20) &&
       stats.video_active_frames >= (uint64_t)(frame_limit / 4) &&
       stats.video_changes >= (uint64_t)(frame_limit / 600) &&
       stats.audio_active_frames >= (uint64_t)(frame_limit / 10) &&
       stats.audio_peak > 0 && audio_samples > 0);
  qualified = qualified && FzeroActorLiveFinish();
  if(versus) {
    const FzeroVersusSnapshot *v=FzeroVersusCompleted(FzeroLocalSession());
    qualified=qualified && (v!=NULL || (FzeroOnlineEnabled() && FzeroCaptureReplayActive()));
    if(local_driver)qualified=qualified && v && v->phase==FZERO_VS_RESULTS &&
        v->racers[0].status==FZERO_VS_FINISHED && v->racers[1].status==FZERO_VS_FINISHED;
    if(v)fprintf(stderr,"local-versus: tick=%llu phase=%u race=%llu hash=%016llx p1=%u,%u status=%u p2=%u,%u status=%u\n",
        (unsigned long long)v->tick,v->phase,(unsigned long long)v->race_tick,
        (unsigned long long)FzeroVersusHash(FzeroLocalSession()),v->racers[0].actor.x,v->racers[0].actor.y,
        v->racers[0].status,v->racers[1].actor.x,v->racers[1].actor.y,v->racers[1].status);
    if(teleport_fixture && qualified) {
      FzeroVersus *race=FzeroLocalSession();uint64_t result_hash=FzeroVersusHash(race);
      size_t size=RtlSaveSnapshotToMemory(NULL,0);uint8_t *result=size?malloc(size):NULL;
      bool ok=result && RtlSaveSnapshotToMemory(result,size)==size && FzeroVersusRematch(race);
      const FzeroVersusSnapshot *fresh=FzeroVersusCompleted(race);
      ok=ok && fresh->phase==FZERO_VS_READY && !fresh->tick && !fresh->race_tick &&
          !fresh->racers[0].completed_laps && !fresh->racers[1].completed_laps &&
          fresh->racers[0].status==FZERO_VS_ACTIVE && fresh->racers[1].status==FZERO_VS_ACTIVE;
      FzeroVersusReady(race,0,true);FzeroVersusReady(race,1,true);
      for(unsigned tick=0;ok && tick<FZERO_VERSUS_COUNTDOWN;++tick)ok=FzeroVersusTick(race,(uint16_t[2]){0,0});
      ok=ok && fresh->phase==FZERO_VS_RACING && !fresh->race_tick &&
          RtlLoadSnapshotFromMemory(result,size) && FzeroVersusHash(FzeroLocalSession())==result_hash;
      free(result);qualified=qualified && ok;
      fprintf(stderr,"qa-teleport: rematch countdown and full results restore %s\n",ok?"PASS":"FAIL");
    }
  }

  fprintf(stderr,
          "fzero_native: %s frames=%ld resume=%06x master=%llu "
          "logic_changes=%llu video_active=%llu video_changes=%llu "
          "audio_samples=%u audio_active=%llu audio_peak=%u "
          "audio_underruns=%llu\n",
          qualified && output_ok ? "PASS" : "FAIL", frame_limit,
          (unsigned)FzeroResumePc(), (unsigned long long)g_cpu.master_cycles,
          (unsigned long long)stats.logic_changes,
          (unsigned long long)stats.video_active_frames,
          (unsigned long long)stats.video_changes, audio_samples,
          (unsigned long long)stats.audio_active_frames, stats.audio_peak,
          (unsigned long long)stats.audio_underruns);
  FzeroTracksSavesFinish();
  FzeroCaptureStop();
  /* Opt-in persistence for private records QA, matching the SDL host's exit.
   * The caller must choose an isolated SNESRECOMP_SAVE_ROOT. */
  if (getenv("FZERO_TEST_SAVE_SRAM")) RtlWriteSram();
  if (getenv("FZERO_PRACTICE_PROBE")) {
    extern bool FzeroPracticeProbe(const char *path);
    if (!FzeroPracticeProbe(getenv("FZERO_PRACTICE_PROBE"))) { free(rom); return 9; }
  }
  if (getenv("FZERO_RULE_PROBE")) {
    extern bool FzeroRulesProbe(void);
    if (!FzeroRulesProbe()) { free(rom); return 9; }
  }
  free(rom);
  FzeroLocalShutdown();
  return qualified && output_ok ? 0 : 8;
}
