#pragma once
#include "fzero_video.h"
#include "fzero_diagnostics.h"
#include <stdio.h>

/* Main-thread recorder. Checkpoints are taken before a real guest frame;
 * never attempt to serialize a machine inside a crash handler. */
bool FzeroCaptureStart(bool enabled, const char *directory,
                       const FzeroVideoSettings *settings);
void FzeroCaptureBefore(uint32_t input, FzeroViewport viewport);
void FzeroCaptureAfter(void);
void FzeroCaptureStop(void);
void FzeroCaptureReport(FILE *file);
const char *FzeroCaptureDirectory(void);
void FzeroCaptureHost(const FzeroDiagnosticSession *session,
                      int output_width, int output_height, int aspect_index);

/* Matching-build replay, driven by the support tool's explicit file paths.
 * Returns 1 with the next input, 0 at EOF, -1 on a malformed/diverged route. */
bool FzeroCaptureReplayLoad(void);
int FzeroCaptureReplayNext(uint32_t *input, FzeroViewport *viewport);
bool FzeroCaptureReplayActive(void);
