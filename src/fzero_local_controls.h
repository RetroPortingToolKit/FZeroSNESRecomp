#pragma once
#include "fzero_local.h"
#include "desktop/sdl_compat.h"
typedef struct FzeroLocalControls FzeroLocalControls;
enum {FZERO_LOCAL_START=1,FZERO_LOCAL_CHANGE=2};
FzeroLocalControls *FzeroLocalControlsCreate(FzeroVersusConfig config,bool scripted);
void FzeroLocalControlsDestroy(FzeroLocalControls *controls);
void FzeroLocalControlsEvent(FzeroLocalControls *controls,const SDL_Event *event);
unsigned FzeroLocalControlsUpdate(FzeroLocalControls *controls,uint32_t keyboard);
FzeroVersusConfig FzeroLocalControlsConfig(const FzeroLocalControls *controls);
uint32_t FzeroLocalControlsInput(const FzeroLocalControls *controls);
bool FzeroLocalControlsSetup(const FzeroLocalControls *controls);
void FzeroLocalControlsDraw(const FzeroLocalControls *controls,uint32_t *pixels,int width,int height);
