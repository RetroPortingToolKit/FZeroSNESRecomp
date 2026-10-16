#pragma once
#include "fzero_actor.h"

/* Cartridge assets, decoded once on the presentation side. No guest VRAM,
 * simulation pointers or generated/redistributed ROM artwork. */
typedef struct FzeroNativeUi {
  uint8_t tiles[256][64], hud[56][64], machines[4][24*16];
  uint8_t numbers[10][128], objects[256][64];
  struct {int x,y;unsigned tile,attr;} explosion[10][64];
  unsigned explosion_count[10], explosion_frame[20], explosion_duration[20];
  uint32_t title_palette[16], effect_palette[16];
  uint32_t palette[4][8], machine_palette[2][4][16];
  bool valid;
} FzeroNativeUi;
typedef struct FzeroUiCanvas {
  uint32_t *pixels;
  int width, height;
  unsigned scale;
} FzeroUiCanvas;
typedef enum FzeroUiInk { FZERO_UI_BLUE, FZERO_UI_YELLOW, FZERO_UI_CYAN, FZERO_UI_GREY } FzeroUiInk;
typedef enum FzeroUiFont { FZERO_UI_SMALL, FZERO_UI_MENU, FZERO_UI_HUD } FzeroUiFont;
bool FzeroNativeUiInit(FzeroNativeUi *ui,FzeroActorTables tables);
const char *FzeroUiCourseName(unsigned course);
const char *FzeroUiMachineName(unsigned machine);
void FzeroUiRect(FzeroUiCanvas c,int x,int y,int w,int h,uint32_t color);
void FzeroUiTile(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned tile,
                  FzeroUiInk ink,bool flip_x,bool flip_y);
void FzeroUiHudTile(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned tile,
                     FzeroUiInk ink,bool flip_y);
void FzeroUiText(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *text,
                  FzeroUiFont font,FzeroUiInk ink);
void FzeroUiCenter(const FzeroNativeUi *ui,FzeroUiCanvas c,int y,const char *text,
                    FzeroUiFont font,FzeroUiInk ink);
void FzeroUiFrame(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,int width,int height);
void FzeroUiMachine(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned machine,bool selected);

void FzeroUiExplosion(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned distance,unsigned age);
void FzeroUiTitleText(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *text);
