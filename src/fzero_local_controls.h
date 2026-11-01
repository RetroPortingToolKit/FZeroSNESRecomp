#pragma once
#include "fzero_local.h"
#include "desktop/sdl_compat.h"
typedef struct FzeroLocalControls FzeroLocalControls;
enum {FZERO_LOCAL_START=1,FZERO_LOCAL_CHANGE=2,FZERO_LOCAL_MAIN=4};
typedef enum FzeroLocalPage {FZERO_LOCAL_MACHINES,FZERO_LOCAL_CUP,FZERO_LOCAL_COURSE,FZERO_LOCAL_COUNT} FzeroLocalPage;
FzeroLocalPage FzeroLocalControlsPage(const FzeroLocalControls *controls);
FzeroLocalControls *FzeroLocalControlsCreate(FzeroVersusConfig config,bool scripted);
void FzeroLocalControlsAssign(FzeroLocalControls *controls,const int sources[2],
                              const char guids[2][40]);
void FzeroLocalControlsDestroy(FzeroLocalControls *controls);
void FzeroLocalControlsEvent(FzeroLocalControls *controls,const SDL_Event *event);
unsigned FzeroLocalControlsUpdate(FzeroLocalControls *controls,uint32_t keyboard);
/* Explicit keyboard seats use their own keybinds.ini player layout; matching
 * layouts intentionally mirror controls to multiple racers. */
unsigned FzeroLocalControlsUpdateKeyboards(FzeroLocalControls *controls,const uint32_t keyboards[FZERO_MAX_PLAYERS]);
FzeroVersusConfig FzeroLocalControlsConfig(const FzeroLocalControls *controls);
uint32_t FzeroLocalControlsInput(const FzeroLocalControls *controls);
bool FzeroLocalControlsSetup(const FzeroLocalControls *controls);
/* Use the assigned keyboard seat's bindings, or the next empty seat's Start
 * binding while joining. Explicit assignments may share a physical keyboard. */
unsigned FzeroLocalControlsKeyboardPlayer(const FzeroLocalControls *controls);
void FzeroLocalControlsLoaded(FzeroLocalControls *controls);
void FzeroLocalControlsReopen(FzeroLocalControls *controls);
void FzeroLocalControlsBack(FzeroLocalControls *controls);
void FzeroLocalControlsDraw(const FzeroLocalControls *controls,uint32_t *pixels,int width,int height);

void FzeroLocalControlsInputs(const FzeroLocalControls *controls,uint16_t inputs[FZERO_MAX_PLAYERS]);

void FzeroLocalControlsAssignPlayers(FzeroLocalControls *controls,const int sources[],const char guids[][40],unsigned count);
