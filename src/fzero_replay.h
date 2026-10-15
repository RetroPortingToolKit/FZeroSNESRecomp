#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "fzero_video.h"

/* Optional deterministic validation input. Empty strings preserve live input.
 * Input: FIRST[-LAST]:MASK,...; viewport: FRAME:ASPECT[@WIDTHxHEIGHT],...
 * Window sizes make Fit changes reproducible, applied before the same tick. */
bool FzeroReplayConfigure(const char *inputs, const char *viewports);
bool FzeroReplayConfigurePlayers(const char *inputs,const char *viewports,unsigned players);
bool FzeroReplayHasInput(void);
uint32_t FzeroReplayInput(unsigned frame);
bool FzeroReplayViewport(unsigned frame, FzeroVideoSettings *settings);
bool FzeroReplayWindow(unsigned frame, int *width, int *height);
