#pragma once
#include "fzero_versus_video.h"
#include "fzero_native_ui.h"

/* Bridge from native course setup to the authoritative CGP-profile VS race. */
bool FzeroLocalConfigure(FzeroVersusConfig config);
bool FzeroLocalStockTables(const uint8_t *rom,size_t size);
unsigned FzeroLocalCupCount(void);
unsigned FzeroLocalCourseCount(unsigned cup);
unsigned FzeroLocalCourseId(unsigned cup,unsigned order);
bool FzeroLocalCourseLocation(unsigned id,unsigned *cup,unsigned *order);
const char *FzeroLocalCupName(unsigned cup);
const char *FzeroLocalCourseName(unsigned id);
void FzeroLocalShutdown(void);
bool FzeroLocalEnabled(void);
FzeroVersus *FzeroLocalSession(void);
bool FzeroLocalTick(uint32_t packed_runner_inputs);
/* Logical native command, independent of the local PCM output preference. */
uint8_t FzeroLocalMusicCommand(void);
uint16_t FzeroLocalSetupInput(void);
void FzeroLocalSetupTick(unsigned host_frame);
void FzeroLocalPrepareCourse(void);
void FzeroLocalCapture(void);
void FzeroLocalPublish(void);
bool FzeroLocalDraw(uint32_t *output,size_t capacity,FzeroViewport viewport,double alpha,unsigned scale);
bool FzeroLocalDrawCombined(uint32_t *native,uint32_t *hd,size_t capacity,FzeroViewport viewport,double alpha,unsigned scale);
void FzeroLocalResetViews(void);
bool FzeroLocalSelectSaveRoot(void);
FzeroVersusConfig FzeroLocalConfig(void);
const FzeroNativeUi *FzeroLocalUi(void);
unsigned FzeroLocalMenuSelection(void);
/* Results become interactive after both native crash animations finish. */
bool FzeroLocalMenuReady(void);
/* 0 = closed; 1/2 = racer operating the live menu. Simulation keeps running. */
unsigned FzeroLocalMenuOwner(void);
void FzeroLocalSetMenuOwner(unsigned owner);
void FzeroLocalSetMenuSelection(unsigned selection);
/* Fixed-size host bridge image, including native setup before handoff.
 * Validation is read-only; restore replaces the session atomically. Asset
 * captures are rebuilt from the accompanying native PPU snapshot afterwards. */
size_t FzeroLocalSaveSize(void);
bool FzeroLocalSave(void *output,size_t size);
bool FzeroLocalValidate(const void *input,size_t size);
bool FzeroLocalRestore(const void *input,size_t size);
/* External control transitions create a new crash-replay checkpoint. Device
 * handles remain host-owned; the resulting complete session is captured. */
uint64_t FzeroLocalEpoch(void);
void FzeroLocalControlChanged(void);
