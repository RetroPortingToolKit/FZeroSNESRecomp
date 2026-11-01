#pragma once
#include "fzero_local.h"

/* Deterministic online setup/control state. Only admitted pad rows enter it;
 * device handles, transport, wall clocks and the local camera stay outside. */
enum {FZERO_ONLINE_TITLE,FZERO_ONLINE_MACHINES,FZERO_ONLINE_CUP,
      FZERO_ONLINE_COURSE,FZERO_ONLINE_LOADING,FZERO_ONLINE_RACE,FZERO_ONLINE_EXIT};
enum {FZERO_ONLINE_STATE_SIZE=128,FZERO_ONLINE_PROTOCOL=4};
typedef struct FzeroOnlineState {
  uint64_t tick,exit_tick;
  uint64_t selection_tick[FZERO_MAX_PLAYERS];
  FzeroVersusConfig config;
  uint16_t previous[FZERO_MAX_PLAYERS],cup;
  uint8_t phase,ready,aspect,failed,release,slide[FZERO_MAX_PLAYERS];
} FzeroOnlineState;
void FzeroOnlineBegin(unsigned aspect,unsigned local_slot);
void FzeroOnlineEnd(void);
/* Called by the native VS title action on an admitted simulation tick. */
void FzeroOnlineEnterVs(void);
bool FzeroOnlineEnabled(void);
int FzeroOnlineView(void);
const FzeroOnlineState *FzeroOnlineCompleted(void);
/* True consumes the tick in setup; false runs native loading / shared race. */
bool FzeroOnlineTick(uint32_t *packed);
void FzeroOnlineDraw(uint32_t *pixels,FzeroViewport viewport,unsigned scale);
void FzeroOnlineSave(uint8_t out[FZERO_ONLINE_STATE_SIZE]);
bool FzeroOnlineValidate(const uint8_t in[FZERO_ONLINE_STATE_SIZE]);
bool FzeroOnlineRestore(const uint8_t in[FZERO_ONLINE_STATE_SIZE]);
/* Native cold course setup, executed by every peer on the same admitted tick. */
bool FzeroOnlineLoadCourse(FzeroVersusConfig config);

void FzeroOnlineBeginPlayers(unsigned aspect,unsigned local_slot,unsigned count);
bool FzeroOnlineTickInputs(uint16_t inputs[]);
