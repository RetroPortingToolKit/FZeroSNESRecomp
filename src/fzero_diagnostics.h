#pragma once

#include "fzero_video.h"
#include <stdint.h>
#include "desktop/hd_gpu.h"

/* Main-thread wall timings. No GPU fences or per-pixel instrumentation. */
typedef enum FzeroDiagnosticStage {
  FZERO_DIAG_SIMULATION, FZERO_DIAG_PPU, FZERO_DIAG_COMPOSITION,
  FZERO_DIAG_UPLOAD, FZERO_DIAG_DRAW, FZERO_DIAG_PRESENT,
  FZERO_DIAG_WAIT, FZERO_DIAG_PAUSED, FZERO_DIAG_STAGE_COUNT
} FzeroDiagnosticStage;

typedef struct FzeroDiagnosticSession {
  const char *version, *revision, *framework_revision, *ui_revision;
  const char *build_type, *compiler;
  const char *backend, *gpu, *gpu_vendor, *driver, *shader;
  bool shader_loaded, linear_filter, audio_enabled, rewind_enabled;
  int vsync; /* actual interval; -99 means unavailable */
  unsigned allocated_scale;
} FzeroDiagnosticSession;

typedef struct FzeroDiagnosticFrame {
  uint64_t simulation, presentations, missed;
  FzeroVideoSettings settings;
  FzeroViewport viewport;
  int output_width, output_height;
  unsigned effective_scale;
  unsigned render_workers;
  double target_hz, refresh_hz;
  bool fullscreen, suspended;
  unsigned scene, subscene;
  int controller_id,controller_x,controller_y;
  uint32_t controller_buttons;
  bool controller_attached;
  unsigned source_height;
  SnesHdGpuStats hd_gpu;
} FzeroDiagnosticFrame;

/* directory is resolved by the host (beside the executable/AppImage). */
bool FzeroDiagnosticsStart(bool enabled, const char *directory,
                           const FzeroDiagnosticSession *session);
uint64_t FzeroDiagnosticsBegin(void);
void FzeroDiagnosticsEnd(FzeroDiagnosticStage stage, uint64_t start);
void FzeroDiagnosticsPresented(void);
void FzeroDiagnosticsEvent(const char *event, int detail);
void FzeroDiagnosticsSample(const FzeroDiagnosticFrame *frame, bool final);
void FzeroDiagnosticsStop(void);
