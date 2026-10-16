#include "fzero_ship.h"
#include <string.h>

static unsigned byte(FzeroActorTables t,unsigned address) { return t.read(t.context,address); }
static unsigned word(FzeroActorTables t,unsigned address) { return byte(t,address)|(byte(t,address+1)<<8); }
static int signed16(unsigned value) { return value<32768?(int)value:(int)value-65536; }
static uint32_t rgb(unsigned c) {
  unsigned r=c&31,g=(c>>5)&31,b=(c>>10)&31;
  return 0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(b<<3)|(b>>2);
}

/* $A98A..$AA05. Preserve the independently truncated hardware products and
 * the native decimal-digit conversion, including its 999 display ceiling. */
unsigned FzeroDisplaySpeed(uint16_t speed) {
  unsigned hi=speed>>8,lo=speed&255;
  unsigned square=((hi*hi)&255)<<8 | ((lo*lo)>>8);
  unsigned value=(uint16_t)((square>>1)+lo*hi)>>2;
  value=(uint16_t)(value+speed);
  if ((value>>10)>=10) return 999;
  unsigned shifted=value>>2;
  unsigned fraction=((value&1023)+(shifted&255))*2;
  unsigned hundreds=value>>10,tens=fraction>>8;
  unsigned ones=((fraction&255)*10)>>8;
  return hundreds*100+tens*10+ones;
}

bool FzeroShipBuild(FzeroShip *s,const FzeroActor *camera,const FzeroActor *a,FzeroActorTables t) {
  if (!s || !camera || !a || !t.read || a->machine>=4) return false;
  memset(s,0,sizeof(*s));
  FzeroActorProjection p=FzeroActorProject(camera,a,t);
  if (!p.projectable) return true;
  /* $F228 orientation folding; $F1F7 distance bands and $F215 graphic ID.
   * Select directly from the completed pose. Native hysteresis is a visual
   * cache optimization, unnecessary when both displays own their art. */
  int angle=(int)(camera->facing>>8)-(int)(a->facing>>8);
  /* Human bank/steering history is owned by the simulation ($BA70..$BB11).
   * Use the cartridge's authored angle poses to show its lean in each view;
   * the followed actor otherwise always has angle zero and looks rigid even
   * while L/R is changing its lateral momentum. This is presentation only. */
  int lean=(a->steering_history&0x80)?((a->steering_history&1)?12:-12):
      a->turn_stage?((int)a->steering_history-5)*2:0;
  if(lean>12)lean=12;
  if(lean< -12)lean= -12;
  angle+=lean;
  if(angle< -96)angle+=192;
  if(angle>=96)angle-=192;
  bool mirror=angle<0;
  unsigned direction=byte(t,0xf26a+(unsigned)(mirror?-angle:angle)/4),band=0;
  while(band<8 && p.y<(int)byte(t,0xf283+band))++band;
  unsigned graphic=byte(t,0x02fd56+byte(t,0xf260+band)+direction);
  unsigned flags=byte(t,0xe84e + ((graphic>>1)&7));
  unsigned variant=(byte(t,0xf330+(graphic>>4))&flags)?1:0;
  if(variant || mirror)++variant;
  unsigned layout=word(t,0xfaf1+(variant*9+band)*2);
  unsigned sizes=word(t,layout),art=word(t,0x02fdcb+graphic);
  s->layout=layout;s->graphic=graphic;
  unsigned half=byte(t,0x02feb3+(graphic>>1))*2;
  unsigned height=a->height>>8;
  /* $03:907E distant jump-height scaling. The followed human retains the
   * stock full-resolution height adjustment from $9D42. */
  if(camera!=a && height<128) height=(height*2*byte(t,0x08ede0+255-(unsigned)p.y))>>8;
  s->ground_x=p.x;s->ground_y=p.y;s->size=band;
  s->x=p.x-32;s->y=p.y-(int)height-48;
  uint32_t palette[16];
  for(unsigned i=0;i<16;++i)palette[i]=rgb(word(t,0x0fcd80+a->machine*32+i*2));
  for(unsigned slot=0;slot<8;++slot) {
    unsigned entry=layout+2+slot*5,dx=word(t,entry);
    if(dx==128)break;
    int x=signed16(dx)+32,y=(int)byte(t,entry+2)-256+48;
    unsigned attr=word(t,entry+3),size=(sizes&(2u<<(slot*2)))?16:8;
    for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
      int ox=x+(int)col,oy=y+(int)row;
      if(ox<0 || ox>=64 || oy<0 || oy>=64)continue;
      unsigned cx=attr&0x4000?size-1-col:col,cy=attr&0x8000?size-1-row:row;
      unsigned tile=(((attr&255)>>4)+cy/8)*16+(((attr&15)+cx/8)&15);
      /* $8364 uploads two contiguous halves one 16-tile row apart. The
       * layout uses local tile indices; no live VRAM reservation is needed. */
      unsigned offset=(tile>=16?half+(tile-16)*32:tile*32)+(cy&7)*2;
      unsigned color=0;
      for(unsigned bit=0;bit<4;++bit)
        color|=((byte(t,((8+a->machine)<<16)|((art+offset+(bit/2)*16+bit%2)&65535))>>(7-(cx&7)))&1)<<bit;
      if(color && !s->pixels[oy*64+ox])s->pixels[oy*64+ox]=palette[color];
    }
  }
  s->visible=true;
  return true;
}
