#include "fzero_ship.h"
#include "fzero_machine.h"
#include "fzero_traffic.h"
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

/* Native human OAM setup ($D71D..$D8BE), independent of the guest's selected
 * machine and VRAM slots. The body, steering inserts and shoulder tilts have
 * different authored layouts; rotating opponent art cannot reproduce them. */
static void player_full(uint8_t layout[512],unsigned dst,unsigned src,unsigned count,
                        unsigned palette,FzeroActorTables t) {
  for(unsigned i=0;i<count;++i) {
    for(unsigned j=0;j<4;++j)layout[dst+i*4+j]=byte(t,0x020000+src+i*4+j);
    layout[dst+i*4+3]|=palette;
  }
}
static void player_attributes(uint8_t layout[512],unsigned dst,unsigned src,unsigned count,
                              unsigned palette,FzeroActorTables t) {
  for(unsigned i=0;i<count;++i) {
    unsigned packed=byte(t,0x020000+src+i);
    layout[dst+i*4+2]=packed&0xee;
    layout[dst+i*4+3]=(packed&1?0x70:0x30)|(packed&16?1:0)|palette;
  }
}
static void player_layout(uint8_t layout[512],unsigned machine,FzeroActorTables t) {
  memset(layout,0,512);unsigned parity=machine&1,palette=8+machine*2;
  for(unsigned start=0;start<0xe0;) {
    unsigned count=start<0x80?byte(t,0x02fc1a+machine):6;
    for(unsigned i=0;i<count;++i) {
      layout[start+i*4]=byte(t,0x02fc28+i);
      layout[start+i*4+1]=(uint8_t)(byte(t,0x02fc2e +i)+byte(t,0x02fc24+machine));
    }
    start+=start<0x80?0x20:0x18;
  }
  for(unsigned group=0;group<4;++group)
    player_attributes(layout,word(t,0x02fc44+group*2),
        0xfc70+byte(t,0x02fc3c+group*2+parity),byte(t,0x02fc34+group*2+parity),palette,t);
  player_attributes(layout,0x80,0xfc70+byte(t,0x02fc4c+machine),12,palette,t);
  player_full(layout,0xc0,0xfc50,8,palette,t);
  if(!parity)for(unsigned i=0;i<4;++i)
    player_full(layout,0x18+i*0x20,(machine?0xfd2a:0xfd0a)+i*8,2,palette,t);
}
static unsigned player_tile(FzeroActorTables t,unsigned machine,unsigned tile,unsigned x,unsigned y) {
  unsigned source=0,bank=8+machine;
  /* Native human graphics upload: F000..FBFF is contiguous at OBJ $A0;
   * FC00..FFFF supplies the eight split tile pairs at the remaining slots. */
  if(tile>=0xa0 && tile<0x100)source=0xf000+(tile-0xa0)*32;
  else {
    static const unsigned pairs[]={0x0e,0x2e,0x4e,0x6e,0x8e,0x100,0x102,0x104};
    for(unsigned i=0;i<8;++i)if(tile==pairs[i] || tile==pairs[i]+1 ||
        tile==pairs[i]+16 || tile==pairs[i]+17) {
      unsigned part=tile-pairs[i];source=0xfc00+i*64+(part>=16?0x200:0)+(part&1)*32;break;
    }
  }
  if(!(machine&1)) {
    /* Blue Falcon/Golden Fox also use four pairs stored in Wild Goose's
     * bank, plus the shared small nose/tilt piece in Fire Stingray's bank. */
    for(unsigned i=0;i<4;++i) {
      unsigned base=0x0c+i*0x20;
      if(tile==base || tile==base+1 || tile==base+16 || tile==base+17) {
        unsigned part=tile-base;bank=9;
        source=0xfc00+(machine?0x100:0)+i*64+(part>=16?0x200:0)+(part&1)*32;break;
      }
    }
    if(tile==0x8c || tile==0x8d || tile==0x9c || tile==0x9d) {
      /* The lower upload begins at word $49C1; initialize its skipped
       * first word from the zero ROM padding, rather than leftover VRAM. */
      bank=11;source=(tile>=0x9c?0xfdfe:0xfc00)+(tile&1)*32;
    }
  }
  if(!source)return 0;
  unsigned color=0;
  for(unsigned bit=0;bit<4;++bit) {
    unsigned address=(bank<<16)|(source+y*2+(bit/2)*16+bit%2);
    /* Native upload's final two-byte transfer replaces tile $9E's first
     * plane pair for Blue Falcon/Golden Fox. Retain that authored overlap. */
    if(!(machine&1) && tile==0x9e && !y && bit<2)address=0x0bfe3e +bit;
    color|=((byte(t,address)>>(7-x))&1)<<bit;
  }
  return color;
}
static void player_paint(FzeroShip *s,int x,int y,unsigned attr,unsigned size,
                         const uint32_t palette[16],unsigned machine,FzeroActorTables t) {
  for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
    int ox=x+(int)col,oy=y+(int)row;if(ox<0 || ox>=64 || oy<0 || oy>=64)continue;
    unsigned cx=attr&0x4000?size-1-col:col,cy=attr&0x8000?size-1-row:row;
    unsigned tile=(attr&0x100)|(((((attr&255)>>4)+cy/8)*16+(((attr&15)+cx/8)&15))&255);
    unsigned color=player_tile(t,machine,tile,cx&7,cy&7);
    if(color && !s->pixels[oy*64+ox])s->pixels[oy*64+ox]=palette[color];
  }
}
static bool player_build(FzeroShip *s,const FzeroActor *a,FzeroActorProjection p,FzeroActorTables t) {
  unsigned pose=a->steering_history,scale=a->animation_scale,parity=a->machine&1;
  /* Air/recovery effects continue through the existing projected-ship path.
   * Ground steering and L/R use the full native human artwork. */
  if((scale&128) || (pose>10 && pose!=0x80 && pose!=0x81))return false;
  uint8_t layout[512];player_layout(layout,a->machine,t);
  struct {int x,y;unsigned attr,size;} parts[8]={0};unsigned count=0,identity=0;
  if(pose&128 || pose<2 || pose>8) {
    unsigned base,height=0;
    if(pose&128) {base=0x80+(pose&1)*0x18;count=6;}
    else {
      unsigned variant=pose<2?pose:pose-7;base=variant*0x20;
      count=byte(t,0x02fc1a+a->machine);
      if(pose==0 || pose==10)height=byte(t,0xf610+a->machine);
    }
    identity=base;
    for(unsigned i=0;i<count;++i) {
      parts[i].x=(int)(int8_t)layout[base+i*4];parts[i].y=(int)(int8_t)layout[base+i*4+1]+height;
      parts[i].attr=layout[base+i*4+2]|(unsigned)layout[base+i*4+3]<<8;parts[i].size=i<6?16:8;
    }
  } else {
    unsigned starts[]={byte(t,0x02fc20+parity)/4,byte(t,0x02fc1e +parity)/4,0};
    unsigned counts[]={byte(t,0x02fc14+parity),byte(t,0x02fc10+parity),byte(t,0x02fc12+parity)};
    unsigned bases[]={0x16c,0xe0+(pose-2)*counts[1]*4,0x150+(scale<=6?scale:3)*4};
    for(unsigned group=0;group<3;++group)for(unsigned n=0;n<counts[group];++n) {
      unsigned i=starts[group]+n;parts[i].x=(int)(int8_t)byte(t,0xecb9+i);
      parts[i].y=(int)(int8_t)byte(t,0xecbf+i);
      parts[i].attr=layout[bases[group]+n*4+2]|(unsigned)layout[bases[group]+n*4+3]<<8;parts[i].size=16;
      if(i>=count)count=i+1;
    }
    identity=bases[1];
  }
  /* $F796 adds Golden Fox's small nose piece during shoulder tilts. */
  if((pose&128) && a->machine==2) {
    parts[6].x=pose&1?-16:8;parts[6].y=-38;parts[6].size=8;
    parts[6].attr=0x3104|((8+a->machine*2)<<8)|(pose&1?0x4000:0);count=7;
  }
  unsigned screen_y=(uint8_t)((unsigned)p.y-(a->height>>8));
  s->ground_x=p.x;s->ground_y=p.y;s->x=p.x-32;s->y=(int)screen_y-48;
  s->layout=0x10000+identity;s->graphic=pose|(scale<<8);s->size=0;
  uint32_t palette[16];for(unsigned i=0;i<16;++i)palette[i]=rgb(word(t,(a->body_flash?0x0aef00:0x0fcd80)+a->machine*32+i*2));
  for(unsigned i=0;i<count;++i)if(parts[i].size)
    player_paint(s,parts[i].x+32,parts[i].y+48,parts[i].attr,parts[i].size,palette,a->machine,t);
  s->visible=true;return true;
}

static bool projected_build(FzeroShip *s,unsigned machine,FzeroActorProjection p,
    unsigned band,unsigned direction,bool mirror,unsigned height,bool body_flash,FzeroActorTables t,const uint8_t *bumper,unsigned bumper_palette) {
  unsigned graphic=byte(t,0x02fd56+byte(t,0xf260+band)+direction);
  unsigned flags=byte(t,0xe84e + ((graphic>>1)&7));
  unsigned variant=(byte(t,0xf330+(graphic>>4))&flags)?1:0;
  if(variant || mirror)++variant;
  unsigned layout=word(t,0xfaf1+((variant+(bumper?3:0))*9+band)*2);
  unsigned sizes=word(t,layout),art=word(t,(bumper?0x02fe3f:0x02fdcb)+graphic);
  s->layout=layout;s->graphic=graphic;
  unsigned half=byte(t,(bumper?0x02feec:0x02feb3)+(graphic>>1))*2;
  s->ground_x=p.x;s->ground_y=p.y;s->size=band;
  s->x=p.x-32;s->y=p.y-(int)height-48;
  uint32_t palette[16];
  for(unsigned i=0;i<16;++i)palette[i]=rgb(word(t,(body_flash?0x0aef00:0x0fcd80)+machine*32+i*2));
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
      for(unsigned bit=0;bit<4;++bit) {
        unsigned address=(art+offset+(bit/2)*16+bit%2)&65535;
        if(bumper && (address<0x2000 || address>=0x8000))return false;
        unsigned value=bumper?bumper[FZERO_TRAFFIC_ART+address-0x2000]:byte(t,((8+machine)<<16)|address);
        color|=((value>>(7-(cx&7)))&1)<<bit;
      }
      if(color && !s->pixels[oy*64+ox]) {
        if(bumper) {
          /* $F464 ORs C41's brown(4)/red(6) attribute into the layout. */
          unsigned at=FZERO_TRAFFIC_PALETTE+(128+(((attr|(bumper_palette<<8))>>9)&7)*16+color)*2;
          s->pixels[oy*64+ox]=rgb(bumper[at]|(unsigned)bumper[at+1]<<8);
        } else s->pixels[oy*64+ox]=palette[color];
      }
    }
  }
  s->visible=true;
  return true;
}

bool FzeroShipBuild(FzeroShip *s,const FzeroActor *camera,const FzeroActor *a,FzeroActorTables t) {
  if (!s || !camera || !a || !t.read || a->machine>=FZERO_MACHINE_COUNT) return false;
  FzeroMachineReader reader;t=FzeroMachineTables(&reader,t,a->machine);
  FzeroActor native=*a;native.machine=(uint8_t)FzeroMachineSlot(a->machine);
  if(camera==a)camera=&native;
  a=&native;
  memset(s,0,sizeof(*s));
  FzeroActorProjection p=FzeroActorProject(camera,a,t);
  if (!p.projectable) return true;
  if(camera==a && player_build(s,a,p,t))return true;
  /* $F228 orientation folding; $F1F7 distance bands and $F215 graphic ID.
   * Select directly from the completed pose. Native hysteresis is a visual
   * cache optimization, unnecessary when both displays own their art. */
  int angle=(int)(camera->facing>>8)-(int)(a->facing>>8);
  if(angle< -96)angle+=192;
  if(angle>=96)angle-=192;
  bool mirror=angle<0;
  unsigned direction=byte(t,0xf26a+(unsigned)(mirror?-angle:angle)/4),band=0;
  while(band<8 && p.y<(int)byte(t,0xf283+band))++band;
  unsigned height=a->height>>8;
  /* $03:907E distant jump-height scaling, also retained for native race views. */
  if(camera!=a && height<128)height=(height*2*byte(t,0x08ede0+255-(unsigned)p.y))>>8;
  return projected_build(s,a->machine,p,band,direction,mirror,height,a->body_flash!=0,t,NULL,0);
}
bool FzeroShipBumper(FzeroShip *s,const FzeroActor *camera,const FzeroActor *a,const uint8_t world[0x20000],FzeroActorTables t,bool explosive) {
  if(!s || !camera || !a || !world || !t.read)return false;
  memset(s,0,sizeof(*s));FzeroActorProjection p=FzeroActorProject(camera,a,t);if(!p.projectable)return true;
  int angle=(int)(camera->facing>>8)-(int)(a->facing>>8);
  if(angle< -96)angle+=192;
  if(angle>=96)angle-=192;
  bool mirror=angle<0;unsigned direction=byte(t,0xf26a+(unsigned)(mirror?-angle:angle)/4),band=0;
  while(band<8 && p.y<(int)byte(t,0xf283+band))++band;
  unsigned height=a->height>>8;if(height<128)height=(height*2*byte(t,0x08ede0+255-(unsigned)p.y))>>8;
  return projected_build(s,0,p,band,direction,mirror,height,false,t,world,explosive?6:4);
}

bool FzeroShipMenu(FzeroShip *s,unsigned machine,uint64_t age,FzeroActorTables t) {
  if(!s || machine>=FZERO_MACHINE_COUNT || !t.read)return false;
  FzeroMachineReader reader;t=FzeroMachineTables(&reader,t,machine);machine=FzeroMachineSlot(machine);
  memset(s,0,sizeof(*s));
  /* $03:8AD9 decreases CE every six frames, from 8 through -12, wrapping
   * to 11. $00:ECC5 sends its magnitude straight to the distance-zero
   * opponent layout and mirrors negative angles; no host rotation/filter. */
  int direction=8-(int)((age/6)%24);
  if(direction< -12)direction+=24;
  FzeroActorProjection p={0};p.projectable=true;
  return projected_build(s,machine,p,0,(unsigned)(direction<0?-direction:direction),direction<0,0,false,t,NULL,0);
}
