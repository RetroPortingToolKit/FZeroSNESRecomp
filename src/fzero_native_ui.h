#pragma once
#include "fzero_actor.h"
#include "fzero_machine.h"

/* Cartridge assets, decoded once on the presentation side. No guest VRAM,
 * simulation pointers or generated/redistributed ROM artwork. */
typedef struct FzeroNativeUi {
  uint8_t tiles[256][64], hud[56][64], machines[FZERO_MACHINE_COUNT][24*16];
  uint8_t menu_machines[FZERO_MACHINE_COUNT][40*24];
  uint8_t menu_letters[26][128];
  uint8_t numbers[10][128], objects[256][64];
  uint16_t hud_map[128];
  uint32_t hud_palette[4][4];
  uint32_t boost_palette[3][16];
  uint32_t exhaust_palette[FZERO_MACHINE_COUNT][16];
  uint8_t power_colors[5][4];
  uint32_t power_warning;
  struct {int x,y;unsigned tile,attr;} contact[14][3];
  uint8_t pit_objects[3][48];
  struct {int x,y;unsigned tile,attr;} explosion[10][64];
  unsigned explosion_count[10], explosion_frame[20], explosion_duration[20];
  uint8_t title_letters[26][128];
  uint8_t title_v[16*9];
  uint32_t title_palette[2][16], effect_palette[16];
  uint32_t crash_palette[16];
  uint8_t crash_text[32];
  unsigned crash_length;
  uint8_t lap_text[2][24],lap_length[2],lap_x[2];
  struct {int x,y;unsigned tile,attr;} sparks[8];
  uint32_t palette[4][8], machine_palette[2][FZERO_MACHINE_COUNT][16];
  uint16_t information[4][1024];
  uint32_t information_palette[FZERO_MACHINE_COUNT][8];
  uint8_t cards[FZERO_MACHINE_COUNT][44][64];
  uint16_t machines_available;
  FzeroActorTables tables;
  bool valid,information_valid,cgp_hud;
} FzeroNativeUi;
typedef struct FzeroUiCanvas {
  uint32_t *pixels;
  int width, height;
  unsigned scale;
} FzeroUiCanvas;
typedef enum FzeroUiInk { FZERO_UI_BLUE, FZERO_UI_YELLOW, FZERO_UI_CYAN, FZERO_UI_GREY } FzeroUiInk;
typedef enum FzeroUiFont { FZERO_UI_SMALL, FZERO_UI_MENU, FZERO_UI_HUD } FzeroUiFont;
bool FzeroNativeUiInit(FzeroNativeUi *ui,FzeroActorTables tables);
bool FzeroNativeUiLoadMachines(FzeroNativeUi *ui,FzeroActorTables tables);
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
void FzeroUiMenuMachine(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned machine,bool selected);
void FzeroUiPit(const FzeroNativeUi *ui,FzeroUiCanvas c,int offset_x,const FzeroActor *actor);

void FzeroUiExplosion(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned distance,unsigned age);
void FzeroUiCrashOut(const FzeroNativeUi *ui,FzeroUiCanvas c,int y);
void FzeroUiSpark(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned distance,uint8_t phase);
void FzeroUiContact(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned distance,uint8_t phase);
void FzeroUiExhaust(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const FzeroActor *actor);
void FzeroUiPlayerTag(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned player,FzeroUiInk ink);
void FzeroUiRaceClock(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *clock);
void FzeroUiRaceInstruments(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,uint16_t health,unsigned speed,uint64_t tick);
void FzeroUiBoosts(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned count,bool active);
/* Original remaining-laps/final-lap banner and S OK award, in simulation ticks
 * since the validated crossing. No wall clock or renderer-owned event state. */
void FzeroUiLapNotice(const FzeroNativeUi *ui,FzeroUiCanvas c,unsigned completed,
    uint64_t age,bool boost_awarded);
void FzeroUiTitleText(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *text,bool dim);

/* A complete native machine pane, independent of participant count/layout.
 * Local play supplies one canvas per seat; online supplies its own full view. */
void FzeroUiMachineSelect(const FzeroNativeUi *ui,FzeroUiCanvas canvas,unsigned machine,
    uint64_t age,unsigned player,bool joined,bool ready);

void FzeroUiMachineSelectScroll(const FzeroNativeUi *ui,FzeroUiCanvas canvas,unsigned machine,
    uint64_t age,unsigned player,bool joined,bool ready,int slide);
