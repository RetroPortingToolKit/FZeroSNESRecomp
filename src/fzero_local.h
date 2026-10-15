#pragma once
#include "fzero_versus_video.h"

/* Bridge from native asset setup to the authoritative stock race. */
bool FzeroLocalConfigure(FzeroVersusConfig config);
void FzeroLocalShutdown(void);
bool FzeroLocalEnabled(void);
FzeroVersus *FzeroLocalSession(void);
bool FzeroLocalTick(uint32_t packed_runner_inputs);
uint16_t FzeroLocalSetupInput(void);
void FzeroLocalSetupTick(unsigned host_frame);
void FzeroLocalPrepareCourse(void);
void FzeroLocalCapture(void);
void FzeroLocalPublish(void);
bool FzeroLocalDraw(uint32_t *output,size_t capacity,FzeroViewport viewport,double alpha,unsigned scale);
void FzeroLocalResetViews(void);
