#pragma once
#include "fzero_native_ui.h"

/* Availability is a host preference; choosing VS is an ordinary title-menu
 * action. Guest menu state remains native for GP, Practice and Records. */
void FzeroVsMenuConfigure(bool available);
uint16_t FzeroVsMenuInput(uint16_t input,uint8_t *ram);
void FzeroVsMenuTick(uint8_t *ram);
bool FzeroVsMenuRequested(void);
bool FzeroVsMenuPending(void);
void FzeroVsMenuDraw(FzeroUiCanvas canvas,int extra,const uint8_t *ram);
bool FzeroVsMenuSelected(void);
void FzeroVsMenuRestore(bool selected);

/* Online rollback restores the admitted title input edge with the guest. */
void FzeroVsMenuRestoreInput(uint16_t input);
