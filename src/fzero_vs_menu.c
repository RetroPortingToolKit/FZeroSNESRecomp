#include "fzero_vs_menu.h"
#include "fzero_gameplay.h"
#include "snes/cart.h"
#include "snes/snes.h"

extern Snes *g_snes;
static bool available,selected,requested;
static uint16_t previous;
static FzeroNativeUi ui;
static uint8_t read_rom(void *context,uint32_t address) {
  (void)context;
  const uint8_t *p=cart_getRomPtr(g_snes->cart,address>>16,(uint16_t)address);
  return p?*p:0;
}
void FzeroVsMenuConfigure(bool enabled) {
  available=enabled;selected=requested=false;previous=0;
  if(enabled)FzeroNativeUiInit(&ui,(FzeroActorTables){read_rom,NULL});
}
static bool title(const uint8_t *ram) {return available && !ram[0x54] && ram[0x55]==1 && !ram[0xc3];}
static unsigned last(const uint8_t *ram) {
  return ram[0xf2]!=15 || FzeroGameplaySettingsCurrent()->always_records?2:1;
}
static void cursor(uint8_t *ram,unsigned choice) {
  ram[0x2cd]=(uint8_t)(104+24*choice);ram[0x2d1]=ram[0x2cd]+8;
}
uint16_t FzeroVsMenuInput(uint16_t input,uint8_t *ram) {
  uint16_t pressed=input&~previous;previous=input;
  if(!title(ram)) {selected=false;return input;}
  if(selected) {
    if(pressed&16) {selected=false;cursor(ram,last(ram));return 0;}
    if(pressed&0x109) {
      selected=false;requested=true;ram[0x5a]=0;cursor(ram,0);
      for(unsigned i=0x65;i<=0x6a;++i)ram[i]=0;
    }
    return 0;
  }
  if((pressed&32) && ram[0x5a]==last(ram)) {selected=true;cursor(ram,last(ram)+1);return 0;}
  return input;
}
void FzeroVsMenuTick(uint8_t *ram) {if(selected && title(ram))cursor(ram,last(ram)+1);}
bool FzeroVsMenuPending(void) {return requested;}
bool FzeroVsMenuRequested(void) {bool value=requested;requested=false;return value;}
void FzeroVsMenuDraw(FzeroUiCanvas canvas,int extra,const uint8_t *ram) {
  if(title(ram))FzeroUiTitleText(&ui,canvas,84+extra,104+24*(last(ram)+1),"VS MODE");
}
bool FzeroVsMenuSelected(void) {return selected;}
void FzeroVsMenuRestore(bool value) {selected=available && value;requested=false;previous=0;}
