#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "fzero_gameplay.h"
#include "snes/hd_jobs.h"

enum { FZERO_HEIGHT = 224, FZERO_STOCK_WIDTH = 256, FZERO_MAX_WIDTH = 684 };
enum { FZERO_HD_SCALE_MIN = 1, FZERO_HD_SCALE_MAX = 10 };
enum { FZERO_GPU_HD_SCALE_MAX = 64 };
#define FZERO_SIMULATION_HZ 60.098811862

typedef enum FzeroAspect {
  FZERO_ASPECT_STOCK,
  FZERO_ASPECT_16_9,
  FZERO_ASPECT_21_9,
  FZERO_ASPECT_32_9,
  FZERO_ASPECT_FIT,
  FZERO_ASPECT_COUNT
} FzeroAspect;

typedef struct FzeroVideoSettings {
  bool enhanced;
  FzeroAspect aspect;
  unsigned fps; /* 0 = display refresh (Auto). */
  bool fps_enabled;
  bool bs_deluxe; /* BS vehicles; legacy field name retained for callers. */
  bool bs_tracks; /* Original BS courses, exclusive with CGP's corrected set. */
  FzeroGameplaySettings gameplay; /* Author's rules are all opt-in. */
  bool hd_mode7; /* Shared GPU enhancement, default on. */
  unsigned hd_scale; /* Integer 1..64; retained while disabled. */
  bool diagnostics; /* Opt-in local performance reports; off by default. */
  bool crash_replay; /* Automatic local crash/replay capture; defaults on. */
  bool local_two_player; /* Launcher game mode; stock rules apply per session. */
} FzeroVideoSettings;

typedef struct FzeroViewport {
  int width, extra;
  double aspect;
  bool enhanced;
} FzeroViewport;

typedef struct FzeroRect { int x, y, w, h; } FzeroRect;

void FzeroVideoDefaults(FzeroVideoSettings *settings); /* shipped: Fit; GPU Mode 7 on at 8x */
void FzeroVideoStock(FzeroVideoSettings *settings);    /* stock 4:3, no mods */
const char *FzeroAspectName(FzeroAspect aspect);
bool FzeroParseAspect(const char *text, FzeroAspect *aspect);
bool FzeroValidFps(unsigned fps);
bool FzeroValidHdScale(unsigned scale);
bool FzeroParseHdScale(const char *text, unsigned *scale);
double FzeroPresentationHz(unsigned fps, double refresh);
FzeroViewport FzeroCalculateViewport(const FzeroVideoSettings *settings,
                                     int drawable_width, int drawable_height);
FzeroRect FzeroDestination(FzeroViewport viewport, int width, int height);
int FzeroHudAnchorX(FzeroViewport viewport, int x, int anchor);
bool FzeroVideoLoad(FzeroVideoSettings *settings, const char *path);
bool FzeroVideoSave(const FzeroVideoSettings *settings, const char *path);

/* Monotonic time in seconds. Simulation debt is never discarded here.
 * Hosts explicitly reset on pause/minimize/load, and bound catch-up batches
 * to keep pumping events when a machine cannot sustain the original rate. */
typedef struct FzeroClock {
  double next_simulation, next_presentation, presentation_hz;
  uint64_t simulation_frames, presentations, missed_presentations;
} FzeroClock;
void FzeroClockReset(FzeroClock *clock, double now, double presentation_hz);
bool FzeroClockSimulationDue(const FzeroClock *clock, double now);
void FzeroClockSimulationDone(FzeroClock *clock);
bool FzeroClockPresentationDue(const FzeroClock *clock, double now);
void FzeroClockPresentationDone(FzeroClock *clock, double now);
double FzeroClockAlpha(const FzeroClock *clock, double now);
double FzeroClockNextDeadline(const FzeroClock *clock);
