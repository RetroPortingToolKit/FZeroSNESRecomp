#pragma once
#include "fzero_local.h"
#include "desktop/sdl_compat.h"
typedef struct FzeroLocalControls FzeroLocalControls;
enum {FZERO_LOCAL_START=1,FZERO_LOCAL_CHANGE=2,FZERO_LOCAL_MAIN=4};
FzeroLocalControls *FzeroLocalControlsCreate(FzeroVersusConfig config,bool scripted);
void FzeroLocalControlsDestroy(FzeroLocalControls *controls);
void FzeroLocalControlsEvent(FzeroLocalControls *controls,const SDL_Event *event);
unsigned FzeroLocalControlsUpdate(FzeroLocalControls *controls,uint32_t keyboard);
FzeroVersusConfig FzeroLocalControlsConfig(const FzeroLocalControls *controls);
uint32_t FzeroLocalControlsInput(const FzeroLocalControls *controls);
bool FzeroLocalControlsSetup(const FzeroLocalControls *controls);
/* Use the assigned keyboard seat's bindings, or the next empty seat's Start
 * binding while joining. At most one seat may own the keyboard. */
unsigned FzeroLocalControlsKeyboardPlayer(const FzeroLocalControls *controls);
void FzeroLocalControlsLoaded(FzeroLocalControls *controls);
void FzeroLocalControlsReopen(FzeroLocalControls *controls);
void FzeroLocalControlsBack(FzeroLocalControls *controls);
void FzeroLocalControlsDraw(const FzeroLocalControls *controls,uint32_t *pixels,int width,int height);
