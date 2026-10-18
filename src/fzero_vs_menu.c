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
static bool title(const uint8_t *ram) {return available && !ram[0x54] && ram[0x55]==1;}
static bool title_visible(const uint8_t *ram) {
  return available && ((!ram[0x54] && (ram[0x55]==1 || ram[0x55]==2)) ||
      (ram[0x54]==1 && !ram[0x55]));
}
static unsigned last(const uint8_t *ram) {
  return ram[0xf2]!=15 || FzeroGameplaySettingsCurrent()->always_records?2:1;
}
static void cursor(uint8_t *ram,unsigned choice) {
  ram[0x2cd]=(uint8_t)(104+20*choice);ram[0x2d1]=ram[0x2cd]+8;
}
uint16_t FzeroVsMenuInput(uint16_t input,uint8_t *ram) {
  uint16_t pressed=input&~previous;previous=input;
  if(!title(ram) || ram[0xc3]) {selected=false;return input;}
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
void FzeroVsMenuTick(uint8_t *ram) {
  if(!title_visible(ram))return;
  /* Four rows need equal spacing and breathing room above the copyright.
   * Keep the original OBJ words; only their Y coordinates change. */
  for(unsigned slot=24;slot<=36;++slot) {
    unsigned row=slot<29?0:slot<33?1:2;
    ram[0x201+slot*4]=(uint8_t)(104+20*row);
    if(selected)ram[0x203+slot*4]=(ram[0x203+slot*4]&0xf1)|2;
  }
  cursor(ram,selected?last(ram)+1:ram[0x5a]);
}
bool FzeroVsMenuPending(void) {return requested;}
bool FzeroVsMenuRequested(void) {bool value=requested;requested=false;return value;}
void FzeroVsMenuDraw(FzeroUiCanvas canvas,int extra,const uint8_t *ram) {
  if(!title_visible(ram))return;
  const Ppu *ppu=g_snes->ppu;unsigned brightness=ppu->inidisp&15;
  if((ppu->inidisp&128) || !brightness)return;
  /* Match the native OBJ row through title fades and palette changes, rather
   * than placing a full-bright overlay above a fading native menu. */
  for(unsigned p=0;p<2;++p)for(unsigned i=0;i<16;++i) {
    unsigned w=ppu->cgram[144+p*16+i];uint32_t color=0xff000000;
    for(unsigned component=0;component<3;++component) {
      unsigned c=(w>>(component*5))&31;c=((c<<3)|(c>>2))*brightness/15;
      color|=c<<(16-component*8);
    }
    ui.title_palette[p][i]=color;
  }
  FzeroUiTitleText(&ui,canvas,84+extra,104+20*(last(ram)+1),"VS MODE",selected && (ram[0x51]&4));
}
bool FzeroVsMenuSelected(void) {return selected;}
void FzeroVsMenuRestore(bool value) {selected=available && value;requested=false;previous=0;}

void FzeroVsMenuRestoreInput(uint16_t input) {previous=input&4095;}
