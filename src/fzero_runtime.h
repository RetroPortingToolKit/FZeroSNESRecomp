#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common_cpu_infra.h"
#include "fzero_video.h"
#include "fzero_state_mode.h"
#include "snes/hd_jobs.h"

const RtlGameInfo *FzeroGameInfo(void);
const RtlGameInfo *FzeroLocalGameInfo(void);

/* Which cartridge this session is running, and what a slot file holds. */
FzeroStateMode FzeroStateModeCurrent(void);
/* 1 + *out when `path` carries a readable F-Zero snapshot trailer. */
int FzeroStateFileMode(const char *path, FzeroStateMode *out);
/* 1 when `path` may be handed to RtlLoadSnapshot in this session. Check this
 * before every load the host makes itself: the engine applies the guest blob
 * before the game's trailer is read, so a refusal after the fact costs a
 * whole-machine restore. */
int FzeroStateFileAcceptable(const char *path);

/* Arm / disarm the undo snapshot used when a load the host did not make is
 * refused (the save-state browser loads through the engine directly). Arm it
 * while the guest is frozen; one snapshot covers every load attempted until
 * it is re-armed. */
void FzeroStateGuardArm(void);
void FzeroStateGuardDisarm(void);
/* 1 once per refusal, then 0 — the host reports it and the flag clears. */
int FzeroStateGuardTripped(void);
void FzeroBeginDrawing(uint8_t *pixels, size_t pitch);
void FzeroDrawPpuFrame(void);
int FzeroFrameWidth(void);
int FzeroFrameHeight(void);
uint32_t FzeroResumePc(void);
int FzeroLastLleResult(void);
void FzeroSetViewport(FzeroViewport viewport);
void FzeroPresent(double alpha);
void FzeroSetDeferredPresentation(bool deferred);
void FzeroSetMode7Hd(unsigned scale, uint32_t *pixels, size_t capacity);
void FzeroSetGpuHd(bool enabled);
const SnesHdFrame *FzeroGpuHdFrame(void);
const uint32_t *FzeroHdFrame(void);
unsigned FzeroHdScale(void);
