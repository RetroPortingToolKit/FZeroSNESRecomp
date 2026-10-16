#pragma once
#include "fzero_local.h"

/* Main-thread cartridge/profile transition. Caller pauses and locks audio.
 * The return point is the actual single-player menu, never a canned save. */
bool FzeroLocalModeInit(const uint8_t *stock,size_t size,const char *rom_path,
                        const char *deluxe_path,const char *music,bool cgp);
bool FzeroLocalModeEnter(FzeroVersusConfig config);
bool FzeroLocalModeReturn(void);
bool FzeroLocalModeSetup(void);
void FzeroLocalModeShutdown(void);
