#pragma once
#include "fzero_versus.h"

/* Presentation commands for the one stock SPC program. No hardware access,
 * guest writes or simulation mutation. Keep this state in the host snapshot. */
typedef struct FzeroSoundState {uint8_t music,landing,phase,lap[2],lap_beep;} FzeroSoundState;
typedef struct FzeroSoundCommands {uint8_t ports[4];} FzeroSoundCommands;
/* Native $F8DD engine and $F999/$F975 effect encoders, without their global
 * RAM side effects. Returned commands use native SPC port bit assignments. */
FzeroSoundCommands FzeroSoundActor(const FzeroActor *actor);
void FzeroSoundInit(FzeroSoundState *state,uint8_t music);
FzeroSoundCommands FzeroSoundTick(FzeroSoundState *state,const FzeroVersusSnapshot *race,bool advanced);
