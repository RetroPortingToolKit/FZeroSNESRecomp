#include "fzero_native_ui.h"
#include "fzero_ship.h"
#include "fzero_exhaust_layouts.inc"
#include <ctype.h>
#include <string.h>

const char *FzeroUiCourseName(unsigned course) {
  static const char *names[]={"MUTE CITY I","BIG BLUE","SAND OCEAN","DEATH WIND I","SILENCE",
    "MUTE CITY II","PORT TOWN I","RED CANYON I","WHITE LAND I","WHITE LAND II",
    "MUTE CITY III","DEATH WIND II","PORT TOWN II","RED CANYON II","FIRE FIELD"};
  return course<15?names[course]:"";
}
const char *FzeroUiMachineName(unsigned machine) {return FzeroMachineName(machine);}

static uint32_t rgb(unsigned w) {
  unsigned r=w&31,g=(w>>5)&31,b=(w>>10)&31;
  return 0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(b<<3)|(b>>2);
}
static unsigned byte(FzeroActorTables t,unsigned address) {return t.read(t.context,address);}
static unsigned word(FzeroActorTables t,unsigned address) {return byte(t,address)|(byte(t,address+1)<<8);}
static void decode(uint8_t out[64],FzeroActorTables t,unsigned address,unsigned planes) {
  for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
    unsigned color=0;
    for(unsigned p=0;p<planes;++p) {
      unsigned offset=planes==1?y:p==2 && planes==3?16+y:(p/2)*16+y*2+p%2;
      color|=((byte(t,address+offset)>>(7-x))&1)<<p;
    }
    out[y*8+x]=(uint8_t)color;
  }
}
bool FzeroNativeUiInit(FzeroNativeUi *ui,FzeroActorTables t) {
  if(!ui || !t.read)return false;
  memset(ui,0,sizeof(*ui));
  /* $03:98EE expands these 256 three-plane menu tiles to native VRAM.
   * $03:8BD5 installs the four matching menu palettes. */
  for(unsigned i=0;i<256;++i)decode(ui->tiles[i],t,0x0ee800+i*24,3);
  for(unsigned n=0;n<26;++n)for(unsigned half=0;half<2;++half)
    memcpy(ui->menu_letters[n]+half*64,ui->tiles[n/16*32+n%16+half*16],64);
  /* The tall cartridge K/X read as H, and Z has the hooked shape of 2.
   * Use the complete native records alphabet for these silhouettes, doubling
   * its seven rows inside the menu's shaded outline. I supplies the exact
   * native outline/fill colors on each scanline, for every menu palette.
   * Keep the raw atlas intact: information cards and frame tiles share it. */
  static const unsigned clear_letters[]={'K','X','Z'};
  for(unsigned i=0;i<sizeof(clear_letters)/sizeof(*clear_letters);++i) {
    unsigned n=clear_letters[i]-'A';uint8_t source[64],stroke[16][8]={0};
    decode(source,t,0x0edea0+n*16,2);
    for(unsigned y=1;y<15;++y)for(unsigned x=0;x<8;++x)
      stroke[y][x]=(source[(y-1)/2*8+x]&2)!=0;
    for(int y=0;y<16;++y)for(int x=0;x<8;++x) {
      bool edge=false;
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        if(y+dy>=0 && y+dy<16 && x+dx>=0 && x+dx<8 && stroke[y+dy][x+dx])edge=true;
      const uint8_t *shade=ui->tiles[8+y/8*16]+(y%8)*8;
      ui->menu_letters[n][y*8+x]=stroke[y][x]?shade[2]:edge?shade[1]:0;
    }
  }
  for(unsigned p=0;p<4;++p)for(unsigned i=0;i<8;++i)
    ui->palette[p][i]=rgb(word(t,0x0ee100+p*32+i*2));
  /* $00:A6A3 uploads the original two-plane race digits, gauge and symbols. */
  /* CGP's energy gauge includes its authored lightning emblem. Keep the
   * native layout/attributes, replacing only the donor-owned HUD tiles. */
  FzeroMachineReader hud_reader;
  FzeroActorTables hud=t;
  if(t.cgp_originals==15) {hud=FzeroMachineTables(&hud_reader,t,0);ui->cgp_hud=true;}
  for(unsigned i=0;i<56;++i)decode(ui->hud[i],hud,0x0cc000+i*16,2);
  for(unsigned i=0;i<128;++i)ui->hud_map[i]=(uint16_t)word(t,0x0aec00+i*2);
  for(unsigned p=0;p<4;++p)for(unsigned i=0;i<4;++i)
    ui->hud_palette[p][i]=rgb(word(t,0x0edde0+p*8+i*2));
  for(unsigned p=0;p<3;++p)for(unsigned i=0;i<16;++i)
    ui->boost_palette[p][i]=rgb(word(t,0x0fcd00+p*32+i*2));
  for(unsigned m=0;m<4;++m)memcpy(ui->exhaust_palette[m],ui->boost_palette[1],sizeof(ui->boost_palette[1]));
  /* AA06's ordinary/low-power fixed-color HDMA records. Palette zero's
   * red is subsequently set to $18 by the native HUD setup. */
  ui->hud_palette[0][3]=rgb(0x18);
  for(unsigned p=0;p<4;++p)for(unsigned i=0;i<4;++i)
    ui->power_colors[p][i]=(uint8_t)byte(t,0x0fc220+p*4+i);
  static const unsigned warning[]={0xaa19,0xaa1e,0xaa23,0xaa28};
  for(unsigned i=0;i<4;++i)ui->power_colors[4][i]=(uint8_t)byte(t,warning[i]);
  ui->power_warning=rgb(word(t,0xaa2d));
  /* BF83 emits three small OBJ pieces from these original contact frames.
   * The indirect pointer's bank is explicitly $0B (DP $19). */
  for(unsigned f=0;f<14;++f)for(unsigned i=0;i<3;++i) {
    unsigned at=0x0b0000+word(t,0xc03b+f*2)+i*4;
    ui->contact[f][i].x=(int)(int8_t)byte(t,at);
    ui->contact[f][i].y=(int)(int8_t)byte(t,at+1);
    ui->contact[f][i].tile=byte(t,at+2);ui->contact[f][i].attr=byte(t,at+3);
  }
  /* The race/introduction OBJ stream includes a complete 8x16 number set
   * and the matching P glyph used for player identities. */
  static const unsigned sizes[4]={32,8,16,24};
  for(unsigned pos=0x0f8000;pos+2<0x100000;) {
    unsigned header=byte(t,pos++),first=byte(t,pos++),count=header&63,mode=header>>6;
    if(!count || first+count>256 || pos+count*sizes[mode]>0x100000)break;
    for(unsigned n=0;n<count;++n) {
      decode(ui->objects[first+n],t,pos+n*sizes[mode],mode==0?4:mode);
      /* The native OBJ loader expands nontransparent 3bpp colors into the
       * upper half of its palette. Menu BG tiles use the lower half. */
      if(mode==3)for(unsigned i=0;i<64;++i)if(ui->objects[first+n][i])ui->objects[first+n][i]|=8;
    }
    if(mode==2)for(unsigned n=0;n<10;++n)for(unsigned half=0;half<2;++half) {
      unsigned id=0x80+n+half*16;
      if(id>=first && id<first+count)decode(ui->numbers[n]+half*64,t,pos+(id-first)*16,2);
    }
    pos+=count*sizes[mode];
  }
  /* $03:8BB5's four DMA descriptors upload three tiles per half, at
   * $4600/$4700 etc. Reassemble the exact 24x16 menu side-profile icons. */
  for(unsigned m=0;m<4;++m) {
    /* Original car-select BG portraits, including the native non-linear
     * bottom-row arrangement and the optional upper-left nose/wing tile. */
    static const int layout[15]={-1,0,1,2,3,7,8,9,10,11,4,5,6,12,13};
    for(unsigned n=0;n<15;++n) {
      uint8_t pixels[64];decode(pixels,t,((8+m)<<16)| (0xd600+layout[n]*32),4);
      for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        ui->menu_machines[m][((n/5)*8+y)*40+(n%5)*8+x]=pixels[y*8+x];
    }
    for(unsigned tile=0;tile<6;++tile) {
      uint8_t pixels[64];decode(pixels,t,((8+m)<<16)| (0xe860+tile*32),4);
      for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        ui->machines[m][((tile/3)*8+y)*24+(tile%3)*8+x]=pixels[y*8+x];
    }
    for(unsigned i=0;i<16;++i) {
      ui->machine_palette[0][m][i]=rgb(word(t,0x0ee180+m*32+i*2));
      ui->machine_palette[1][m][i]=rgb(word(t,0x0fcd80+m*32+i*2));
    }
  }
  for(unsigned i=0;i<16;++i) {
    for(unsigned p=0;p<2;++p)ui->title_palette[p][i]=rgb(word(t,0x0fc300+p*32+i*2));
    /* Race initialization replaces this palette's first eight entries;
     * retaining the introduction colors makes contact arcs pink/purple. */
    ui->effect_palette[i]=rgb(word(t,(i<8?0x0aeee0:0x0fcd60)+i*2));
    ui->crash_palette[i]=rgb(word(t,0x0fcd40+i*2));
  }
  for(unsigned set=0;set<3;++set)for(unsigned i=0;i<(set==2?48:32);++i)
    ui->pit_objects[set][i]=(uint8_t)byte(t,0x0bed50+set*32+i);
  /* $CD2B's native result banner is CRASHED OUT, rendered by $D107 as
   * paired 8x8 OBJ tiles. Keep its wording, glyphs and $35 palette. */
  for(unsigned i=0;i<32 && byte(t,0xcd2e +i);++i) {
    ui->crash_text[i]=(uint8_t)byte(t,0xcd2e +i);++ui->crash_length;
  }
  /* $ADE4 selects $AFF8's native message records. Bit4 is laps remaining;
   * bit3 is FINAL LAP. Keep their OBJ alphabet, spaces and original alignment. */
  for(unsigned final=0;final<2;++final) {
    unsigned record=0xaffa+byte(t,0xafee + (final?3:4));
    ui->lap_x[final]=(uint8_t)byte(t,record);
    for(unsigned i=0;i<sizeof(ui->lap_text[final]) && byte(t,record+2+i);++i) {
      ui->lap_text[final][i]=(uint8_t)byte(t,record+2+i);++ui->lap_length[final];
    }
  }
  for(unsigned i=0;i<8;++i) {
    unsigned at=0x0fc200+i*4;
    ui->sparks[i].x=(int)(int8_t)byte(t,at);ui->sparks[i].y=(int)(int8_t)byte(t,at+1);
    ui->sparks[i].tile=byte(t,at+2);ui->sparks[i].attr=byte(t,at+3);
  }
  /* The title menu has its own 8x16 OBJ alphabet and green scanline shading,
   * distinct from course introductions. These are the exact native word tiles
   * uploaded at $120..$156. M/V are absent from those words. */
  static const struct {char letter;unsigned address;} title_glyphs[]={
    {'G',0x0cf800},{'R',0x0cf820},{'A',0x0cf840},{'N',0x0cf860},
    {'D',0x0cf880},{'P',0x0cf8c0},{'I',0x0cf900},{'X',0x0cf920},
    {'E',0x0cf960},{'C',0x0cf980},{'O',0x0cf9a0},{'T',0x0cfc40},{'S',0x0cfcc0}};
  for(unsigned n=0;n<sizeof(title_glyphs)/sizeof(*title_glyphs);++n)
    for(unsigned half=0;half<2;++half)
      decode(ui->title_letters[title_glyphs[n].letter-'A']+half*64,t,title_glyphs[n].address+half*0x200,4);
  /* M retains the cartridge silhouette with the title font's row shading. */
  uint8_t *m=ui->title_letters['M'-'A'];
  for(unsigned y=1;y<15;++y)for(unsigned x=0;x<8;++x) {
    unsigned sy=3+(y-1)*10/14;
    if(ui->objects[0x8a+(sy/8)*16][(sy&7)*8+x]==3)
      m[y*8+x]=ui->title_letters['I'-'A'][y*8+3];
  }
  /* Use the same 8x16 cell, cap/baseline and scanline colors as S MODE.
   * A wider V reads larger than the native condensed menu alphabet. */
  uint8_t *v=ui->title_letters['V'-'A'];
  for(unsigned y=1;y<15;++y)for(unsigned x=0;x<8;++x) {
    /* Keep the cartridge V's actual taper. Its lower tip is drawn with
     * the highlight index after the two-pixel inner strokes end. */
    unsigned sy=2+(y-1)*11/14;
    unsigned source=ui->objects[0xa3+(sy/8)*16][(sy&7)*8+x];
    if(source==3 || (sy>=9 && source==2))
      v[y*8+x]=ui->title_letters['I'-'A'][y*8+3];
  }
  for(unsigned n=0;n<2;++n) {
    uint8_t *glyph=n?v:m;
    for(int y=0;y<16;++y)for(int x=0;x<8;++x)if(glyph[y*8+x] && glyph[y*8+x]!=5)
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        int xx=x+dx,yy=y+dy;
        if(xx>=0 && xx<8 && yy>=0 && yy<16 && !glyph[yy*8+xx])glyph[yy*8+xx]=5;
      }
  }
  /* Native $ED6A..$EED1 explosion frames: the first four use big OBJ
   * offsets; the remaining six expand paired 16x16 layouts from $FED9. */
  for(unsigned i=0;i<20;++i) {
    ui->explosion_frame[i]=byte(t,0xeee2+i);ui->explosion_duration[i]=byte(t,0xeef6+i);
  }
  for(unsigned f=0;f<10;++f) {
    unsigned at=word(t,0xef14+f*2),count=0;int base=(int)byte(t,0xef0a+f);if(base>=128)base-=256;
    if(byte(t,at)!=255) {
      for(unsigned i=0;i<64 && byte(t,at)!=255;++i,at+=2) {
        unsigned xy=byte(t,at),attr=byte(t,at+1);
        int x=(int)byte(t,0xee5f+(xy>>4)),y=(int)byte(t,0xee6b+(xy&15));
        if(x>=128)x-=256;
        if(y>=128)y-=256;
        ui->explosion[f][count].x=x;ui->explosion[f][count].y=y+base;
        ui->explosion[f][count].tile=attr&63;ui->explosion[f][count++].attr=attr;
      }
    } else {
      unsigned groups=byte(t,++at);++at;
      for(unsigned g=0;g<groups && g<16;++g,at+=3) {
        unsigned pattern=byte(t,at);int dx=byte(t,at+1),dy=byte(t,at+2);
        if(dx>=128)dx-=256;
        if(dy>=128)dy-=256;
        unsigned parts=byte(t,0xfed9+pattern++);
        for(unsigned j=0;j<parts && j<16;++j,pattern+=2) {
          unsigned id=byte(t,0xfed9+pattern);int x=byte(t,0xfed9+pattern+1);if(x>=128)x-=256;
          for(unsigned half=0;half<2 && count<64;++half) {
            unsigned attr=word(t,0xfec1+id+half*2);
            ui->explosion[f][count].x=dx+x;ui->explosion[f][count].y=dy+base+(int)half*16;
            ui->explosion[f][count].tile=attr&255;ui->explosion[f][count++].attr=attr>>8;
          }
        }
      }
    }
    ui->explosion_count[f]=count;
  }
  /* $03:9892's four original information maps. Keep the authored frame,
   * engine/power/speed/weight text and acceleration curves as actual tiles. */
  unsigned pos=0x0fb8b0,count=0,hi=0,lo=0;
  while(pos<0x0fc100) {
    unsigned command=byte(t,pos++),run=1;
    if(!(command&0x20)) {hi=command;lo=byte(t,pos++);}
    else if(command&0x80)run=(command&31)+1;
    else if(!(command&0x40))run=(command&31)+1;
    else break;
    if(count+run>4096)break;
    for(unsigned n=0;n<run;++n) {
      unsigned value=(command&0xa0)==0xa0?byte(t,pos++):lo;
      ui->information[count/1024][count%1024]=(uint16_t)((hi<<8)|value);++count;
    }
  }
  /* Alternate native cartridges can replace this stream. Their title/HUD
   * assets remain usable; VS selection always reads the verified stock copy. */
  ui->information_valid=count==4096;
  for(unsigned m=0;m<4;++m) {
    memcpy(ui->information_palette[m],ui->palette[0],sizeof(ui->palette[0]));
    for(unsigned i=0;i<3;++i)ui->information_palette[m][3+i]=rgb(word(t,0x038901+m*6+i*2));
  }
  ui->machines_available=15;ui->tables=t;ui->valid=true;return true;
}
bool FzeroNativeUiLoadMachines(FzeroNativeUi *ui,FzeroActorTables base) {
  if(!ui || !ui->valid)return false;
  static const int layout[]={-1,0,1,2,3,7,8,9,10,11,4,5,6,12,13};
  for(unsigned id=0;id<FZERO_MACHINE_COUNT;++id) {
    if(!FzeroMachineProfile(base,id))continue;
    if(!FzeroMachineAvailable(base,id))return false;
    FzeroMachineReader reader;FzeroActorTables t=FzeroMachineTables(&reader,base,id);
    unsigned slot=FzeroMachineSlot(id);
    for(unsigned n=0;n<15;++n) {
      uint8_t pixels[64];decode(pixels,t,((8+slot)<<16)|(0xd600+layout[n]*32),4);
      for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        ui->menu_machines[id][(n/5*8+y)*40+n%5*8+x]=pixels[y*8+x];
    }
    for(unsigned n=0;n<6;++n) {
      uint8_t pixels[64];decode(pixels,t,((8+slot)<<16)|(0xe860+n*32),4);
      for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        ui->machines[id][(n/3*8+y)*24+n%3*8+x]=pixels[y*8+x];
    }
    for(unsigned i=0;i<16;++i) {
      ui->machine_palette[0][id][i]=rgb(word(t,0x0ee180+slot*32+i*2));
      ui->machine_palette[1][id][i]=rgb(word(t,0x0fcd80+slot*32+i*2));
      ui->exhaust_palette[id][i]=rgb(word(t,0x0fcd20+i*2));
    }
    unsigned card=0x108008+slot*(0x580+6);
    for(unsigned n=0;n<44;++n)decode(ui->cards[id][n],t,card+n*32,4);
    memcpy(ui->information_palette[id],ui->palette[0],sizeof(ui->palette[0]));
    for(unsigned i=0;i<3;++i)ui->information_palette[id][3+i]=rgb(word(t,card+0x580+i*2));
    ui->machines_available|=(uint16_t)(1u<<id);
  }
  return true;
}
void FzeroUiRect(FzeroUiCanvas c,int x,int y,int w,int h,uint32_t color) {
  if(!c.pixels || !c.scale || c.scale>10 || w<=0 || h<=0)return;
  int left=x*(int)c.scale,top=y*(int)c.scale,right=(x+w)*(int)c.scale,bottom=(y+h)*(int)c.scale;
  if(left<0)left=0;
  if(top<0)top=0;
  if(right>c.width)right=c.width;
  if(bottom>c.height)bottom=c.height;
  for(int row=top;row<bottom;++row)for(int col=left;col<right;++col)c.pixels[(size_t)row*c.width+col]=color;
}
static void tile(const uint8_t *pixels,const uint32_t *palette,FzeroUiCanvas c,int x,int y,bool fx,bool fy) {
  for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col) {
    unsigned color=pixels[(fy?7-row:row)*8+(fx?7-col:col)];
    if(color && palette[color])FzeroUiRect(c,x+(int)col,y+(int)row,1,1,palette[color]);
  }
}
void FzeroUiTile(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned id,FzeroUiInk ink,bool fx,bool fy) {
  if(ui && ui->valid && id<256 && (unsigned)ink<4)tile(ui->tiles[id],ui->palette[ink],c,x,y,fx,fy);
}
void FzeroUiHudTile(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned id,FzeroUiInk ink,bool fy) {
  if(!ui || !ui->valid || id>=56 || (unsigned)ink>=4)return;
  uint32_t colors[4]={0,0,ui->palette[ink][2],ui->palette[ink][3]};
  tile(ui->hud[id],colors,c,x,y,false,fy);
}
void FzeroUiText(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *s,FzeroUiFont font,FzeroUiInk ink) {
  if(!ui || !ui->valid || (unsigned)ink>=4 || !s)return;
  /* Text overlays retain the scenery through the cartridge font's black
   * backing pixels. Frames, icons and title lettering keep their own art. */
  uint32_t colors[8];
  for(unsigned i=0;i<8;++i)colors[i]=(ui->palette[ink][i]&0x00ffffff)?ui->palette[ink][i]:0;
  colors[0]=0;
  for(;*s;++s,x+=8) {
    unsigned ch=(unsigned)toupper((unsigned char)*s);
    if(ch==' ')continue;
    if(font!=FZERO_UI_MENU) {
      if(ch>='0' && ch<='9') {
        /* The small race numerals are authored at 8x8. Keep their aspect
         * rather than squashing the tall introduction numerals. */
        FzeroUiHudTile(ui,c,x,y,ch-'0',ink,false);
      }
      else if(ch>='A' && ch<='Z') {
        unsigned id=ch=='A'?0xaf:ch=='B'?0xbf:ch=='M'?0xcf:ch=='N'?0xce:0xc4+ch-'C';
        tile(ui->tiles[id],colors,c,x,y,false,false);
      } else if(ch==':') {
        FzeroUiRect(c,x+3,y+2,1,1,ui->palette[ink][2]);FzeroUiRect(c,x+3,y+5,1,1,ui->palette[ink][2]);
      } else if(ch=='.' || ch=='\'' || ch=='"') {
        int yy=ch=='.'?6:0;FzeroUiRect(c,x+2,y+yy,1,2,ui->palette[ink][2]);
        if(ch=='"')FzeroUiRect(c,x+5,y+yy,1,2,ui->palette[ink][2]);
      } else if(ch=='-')FzeroUiRect(c,x+1,y+3,5,1,ui->palette[ink][2]);
      continue;
    }
    if(ch>='A' && ch<='Z') {
      const uint8_t *glyph=ui->menu_letters[ch-'A'];
      tile(glyph,colors,c,x,y,false,false);tile(glyph+64,colors,c,x,y+8,false,false);
      continue;
    }
    else if(ch>='0' && ch<='9') {
      const uint8_t *pixels=ui->numbers[ch-'0'];
      for(unsigned row=0;row<16;++row)for(unsigned col=0;col<8;++col)
        if(colors[pixels[row*8+col]])FzeroUiRect(c,x+(int)col,y+(int)row,1,1,colors[pixels[row*8+col]]);
      continue;
    }
  }
}
void FzeroUiCenter(const FzeroNativeUi *ui,FzeroUiCanvas c,int y,const char *s,FzeroUiFont font,FzeroUiInk ink) {
  if(s && c.scale)FzeroUiText(ui,c,(c.width/(int)c.scale-(int)strlen(s)*8)/2,y,s,font,ink);
}
void FzeroUiFrame(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,int w,int h) {
  if(w<16 || h<16)return;
  FzeroUiRect(c,x,y,w,h,0xff000000);
  for(int i=8;i<w-8;i+=8) {
    FzeroUiTile(ui,c,x+i,y,0x3d,FZERO_UI_BLUE,false,false);
    FzeroUiTile(ui,c,x+i,y+h-8,0x3d,FZERO_UI_BLUE,false,true);
  }
  for(int i=8;i<h-8;i+=8) {
    FzeroUiTile(ui,c,x,y+i,0x3c,FZERO_UI_BLUE,false,false);
    FzeroUiTile(ui,c,x+w-8,y+i,0x3c,FZERO_UI_BLUE,true,false);
  }
  FzeroUiTile(ui,c,x,y,0x3e,FZERO_UI_BLUE,false,true);
  FzeroUiTile(ui,c,x+w-8,y,0x3e,FZERO_UI_BLUE,true,true);
  FzeroUiTile(ui,c,x,y+h-8,0x3e,FZERO_UI_BLUE,false,false);
  FzeroUiTile(ui,c,x+w-8,y+h-8,0x3e,FZERO_UI_BLUE,true,false);
}
void FzeroUiMachine(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned m,bool selected) {
  if(!ui || !ui->valid || m>=FZERO_MACHINE_COUNT)return;
  for(unsigned row=0;row<16;++row)for(unsigned col=0;col<24;++col) {
    unsigned color=ui->machines[m][row*24+col];
    if(color)FzeroUiRect(c,x+(int)col,y+(int)row,1,1,ui->machine_palette[selected?1:0][m][color]);
  }
}
void FzeroUiMenuMachine(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned m,bool selected) {
  if(!ui || !ui->valid || m>=FZERO_MACHINE_COUNT)return;
  for(unsigned row=0;row<24;++row)for(unsigned col=0;col<40;++col) {
    unsigned color=ui->menu_machines[m][row*40+col];
    if(color)FzeroUiRect(c,x+(int)col,y+(int)row,1,1,ui->machine_palette[selected?1:0][m][color]);
  }
}
static void pit_object(const FzeroNativeUi *ui,FzeroUiCanvas c,int offset_x,int x,unsigned y,
    unsigned id,unsigned attr,unsigned size) {
  const uint32_t *palette=(attr&14)==6?ui->effect_palette:ui->boost_palette[(attr>>1)&7];
  for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
    unsigned cx=attr&64?size-1-col:col,cy=attr&128?size-1-row:row;
    unsigned color=ui->objects[(id+(cy/8)*16+cx/8)&255][(cy&7)*8+(cx&7)];
    /* Native OBJ Y wraps at 256; the arrival starts above the viewport. */
    if(color)FzeroUiRect(c,offset_x+x+(int)col,(int)((y+row)&255),1,1,palette[color]);
  }
}
void FzeroUiPit(const FzeroNativeUi *ui,FzeroUiCanvas c,int offset_x,const FzeroActor *a) {
  if(!ui || !ui->valid || !a || !a->pit_pose)return;
  if(a->pit_phase==0x23) {
    const uint8_t *beam=ui->pit_objects[a->pit_recovery?0:1];
    for(int i=7;i>=0;--i)pit_object(ui,c,offset_x,beam[i*4],i<4?a->beam_top:a->beam_bottom,
        beam[i*4+2],beam[i*4+3],16);
  }
  const uint8_t *body=ui->pit_objects[2];
  static const unsigned high[3]={0xff,0xa8,0x2a};
  static const unsigned dx[12]={12,92,10,94,0,8,24,40,56,72,88,104};
  unsigned q=a->pit_pose-1;
  for(int i=11;i>=0;--i) {
    unsigned hi=(high[i/4]>>((i%4)*2))&3;
    /* C4E9 parks these four spare OBJ pieces offscreen with X-high set.
     * A wider camera must not expose their flame tiles beside the HUD. */
    if(hi&1)continue;
    int x=a->pit_pose==1?body[i*4]:(int)((0x88-q*2+dx[i])&255);
    unsigned y=a->pit_pose==1?body[i*4+1]:(0xf0+(q+1)/2+((i==2 || i==3)?5:0))&255;
    pit_object(ui,c,offset_x,x,y,body[i*4+2],body[i*4+3],hi&2?16:8);
  }
}

void FzeroUiExplosion(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned distance,unsigned age) {
  if(!ui || !ui->valid || distance>8)return;
  unsigned stage=0;
  while(stage<20 && age>=ui->explosion_duration[stage]) {age-=ui->explosion_duration[stage];++stage;}
  if(stage>=20 || ui->explosion_frame[stage]>=10)return;
  unsigned f=ui->explosion_frame[stage],zoom=16-distance;
  for(unsigned i=0;i<ui->explosion_count[f];++i) {
    for(unsigned row=0;row<16;++row)for(unsigned col=0;col<16;++col) {
      unsigned attr=ui->explosion[f][i].attr,cx=attr&64?15-col:col,cy=attr&128?15-row:row;
      unsigned id=ui->explosion[f][i].tile+(cy/8)*16+cx/8;
      unsigned color=ui->objects[id&255][(cy&7)*8+(cx&7)];if(!color)continue;
      int dx=ui->explosion[f][i].x+(int)col,dy=ui->explosion[f][i].y+(int)row;
      FzeroUiRect(c,x+dx*(int)zoom/16,y+dy*(int)zoom/16,1,1,ui->effect_palette[color]);
    }
  }
}
void FzeroUiCrashOut(const FzeroNativeUi *ui,FzeroUiCanvas c,int y) {
  if(!ui || !ui->valid || !c.scale)return;
  int x=(c.width/(int)c.scale-(int)ui->crash_length*8)/2;
  for(unsigned n=0;n<ui->crash_length;++n,x+=8) {
    unsigned id=ui->crash_text[n];if(id==255)continue;
    tile(ui->objects[id],ui->crash_palette,c,x,y,false,false);
    tile(ui->objects[(id+16)&255],ui->crash_palette,c,x,y+8,false,false);
  }
}
void FzeroUiLapNotice(const FzeroNativeUi *ui,FzeroUiCanvas c,unsigned completed,
    uint64_t age,bool boost_awarded) {
  if(!ui || !ui->valid || !c.scale || !completed || completed>=5)return;
  int extra=(c.width/(int)c.scale-256)/2;
  bool final=completed==4;
  /* $AE5F holds the ordinary banner for128 ticks. $AEE3 holds FINAL LAP
   * for160, hiding at countdown low bits10 and showing again at0. */
  if((!final && age<128) || (final && age<160 && age%32<22)) {
    for(unsigned n=0;n<ui->lap_length[final];++n) {
      unsigned id=ui->lap_text[final][n];if(id==255)continue;
      if(!final && !n)id=0x85-completed; /* $AE6C's laps-left digit. */
      int x=extra+ui->lap_x[final]+(int)n*8;
      tile(ui->objects[id],ui->crash_palette,c,x,72,false,false);
      tile(ui->objects[(id+16)&255],ui->crash_palette,c,x,80,false,false);
    }
  }
  /* $C782 installs two16x16 S OK objects, removing them when $CFE reaches1.
   * A full stock boost inventory must not claim another boost was awarded. */
  if(boost_awarded && age<31) {
    pit_object(ui,c,extra,112,132,0x6c,0x31,16);
    pit_object(ui,c,extra,128,132,0xa6,0x31,16);
  }
}
void FzeroUiSpark(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned distance,uint8_t phase) {
  if(!ui || !ui->valid || distance>8 || !phase)return;
  /* Completed DF has already advanced in BED7. Reconstruct that tick's
   * emitted OAM selection (1..10, mirrored by bit7), never advance here. */
  unsigned previous=(phase&0x7f)==1?10:(phase&0x7f)-1;
  unsigned step=previous-1,size=(step&8)?16:8;
  unsigned index=((step&8)?(step&1)*4:8|(step&2)*2)|(phase&0x80?16:0);
  index/=4;if(index>=8)return;
  unsigned attr=ui->sparks[index].attr,zoom=16-distance;
  for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
    unsigned cx=attr&64?size-1-col:col,cy=attr&128?size-1-row:row;
    unsigned id=ui->sparks[index].tile+(cy/8)*16+cx/8;
    unsigned color=ui->objects[id&255][(cy&7)*8+(cx&7)];if(!color)continue;
    int dx=ui->sparks[index].x+(int)col,dy=ui->sparks[index].y+(int)row;
    FzeroUiRect(c,x+dx*(int)zoom/16,y+dy*(int)zoom/16,1,1,ui->effect_palette[color]);
  }
}
void FzeroUiContact(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned distance,uint8_t phase) {
  if(!ui || !ui->valid || distance>8 || !(phase&15))return;
  unsigned f=phase&0x80?(phase&15)-1:phase&0x40?(phase&15)+9:14;
  if(f>=14)return;
  unsigned zoom=16-distance;
  for(int i=2;i>=0;--i) {
    unsigned attr=ui->contact[f][i].attr;
    if(!attr)continue; /* Native hidden reservation. */
    for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col) {
      unsigned color=ui->objects[ui->contact[f][i].tile][(attr&128?7-row:row)*8+(attr&64?7-col:col)];
      if(!color)continue;
      int dx=ui->contact[f][i].x+(int)col,dy=ui->contact[f][i].y+(int)row;
      FzeroUiRect(c,x+dx*(int)zoom/16,y+dy*(int)zoom/16,1,1,ui->effect_palette[color]);
    }
  }
}
void FzeroUiExhaust(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const FzeroActor *a) {
  if(!ui || !ui->valid || !a || a->machine>=FZERO_MACHINE_COUNT || !a->exhaust ||
      a->race_state || (!(a->exhaust&128) && a->exhaust_frame==3))return;
  unsigned pose=a->steering_history<128?a->steering_history:(a->steering_history&127)+11;
  if(pose>12)return;
  unsigned group=FzeroMachineProfile(ui->tables,a->machine),slot=FzeroMachineSlot(a->machine);
  FzeroMachineReader reader;FzeroActorTables t=FzeroMachineTables(&reader,ui->tables,a->machine);
  unsigned count=group?exhaust_counts[group-1][slot]:slot==1?3:slot==3?2:4;
  unsigned frame=a->exhaust&128?(a->exhaust==0x81?2:a->exhaust==0x82?1:0):a->exhaust_frame;
  if(frame>2)return;
  unsigned id=byte(t,0xc77f+frame);
  uint32_t colors[16];memcpy(colors,ui->exhaust_palette[a->machine],sizeof(colors));
  unsigned level=a->exhaust_color,color=level<32?level:level<63?31|((level-31)<<5):0x3ff|((level-62)<<10);
  colors[12]=(a->exhaust&128) || (a->exhaust==4 && !a->exhaust_color && !a->exhaust_frame)?rgb(0x7fff):rgb(color);
  /* C64F..C77B's packed XY additions deliberately carry between bytes and
   * successive pipes. Shoulder poses and hover offsets are native inputs. */
  unsigned origin=104|(unsigned)(uint8_t)(y-32+a->exhaust_offset+(a->animation_scale&128?2:0))<<8,carry=0;
  int pipe_x[4],pipe_y[4];
  for(unsigned pipe=0;pipe<count;++pipe) {
    unsigned position;
    if(group)position=exhaust_positions[group-1][slot][pose][pipe*2]|(unsigned)exhaust_positions[group-1][slot][pose][pipe*2+1]<<8;
    else {
      static const unsigned bases[]={0x02c630,0x02c658,0x02c682,0x02c6aa};
      unsigned offset=byte(t,(slot==1?0xc762:0xc755)+pose);
      if(slot==3)offset>>=1;
      position=word(t,bases[slot]+offset+pipe*2);
      if(slot==1 && !pipe)position+=byte(t,0xc76f+pose);
    }
    unsigned sum=origin+position+carry;carry=sum>65535;
    pipe_x[pipe]=(int)(sum&255)+x-128;pipe_y[pipe]=(int)((sum>>8)&255);
  }
  for(int pipe=(int)count-1;pipe>=0;--pipe)tile(ui->objects[id],colors,c,pipe_x[pipe],pipe_y[pipe],false,false);
}
void FzeroUiPlayerTag(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned player,FzeroUiInk ink) {
  if(!ui || !ui->valid || player>=9 || (unsigned)ink>=4)return;
  uint32_t colors[16]={0};
  for(unsigned i=1;i<4;++i)if(ui->palette[ink][i]&0xffffff)colors[i]=ui->palette[ink][i];
  /* P and numeral share the unscaled native race OBJ font and palette.
   * Never mix an 8x8 menu letter with a compressed 8x16 number. */
  for(unsigned n=0;n<2;++n)for(unsigned half=0;half<2;++half)
    tile(ui->objects[(n?0x81+player:0x8d)+half*16],colors,c,x+(int)n*8,y+(int)half*8,false,false);
}
void FzeroUiRaceClock(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *s) {
  if(!ui || !ui->valid || !s)return;
  uint32_t colors[16];memcpy(colors,ui->crash_palette,sizeof(colors));colors[1]=0;
  for(;*s;++s,x+=8) {
    if(*s>='0' && *s<='9') {
      unsigned id=0x80+*s-'0';
      tile(ui->objects[id],colors,c,x,y,false,false);tile(ui->objects[id+16],colors,c,x,y+8,false,false);
    } else if(*s=='\'' || *s=='"')tile(ui->objects[*s=='\''?0xa8:0xb8],colors,c,x,y,false,false);
    else if(*s=='-')FzeroUiRect(c,x+1,y+7,5,2,colors[3]);
  }
}
static void hud_tile(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned attr,bool backing,uint32_t color3) {
  unsigned id=attr&255;if(id>=56)return;
  uint32_t colors[4];memcpy(colors,ui->hud_palette[(attr>>10)&3],sizeof(colors));
  colors[0]=0;
  if(color3)colors[3]=color3;
  uint8_t pixels[64];memcpy(pixels,ui->hud[id],sizeof(pixels));
  /* Preserve the native dark edging next to the colored lettering, while
   * removing the rest of its solid black tile backing. Bright scenery can
   * show through without losing the stock glyph's contrast and silhouette. */
  if(!backing)for(int row=0;row<8;++row)for(int col=0;col<8;++col)if(pixels[row*8+col]==1) {
    bool edge=false;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
      int xx=col+dx,yy=row+dy;
      if(xx>=0 && xx<8 && yy>=0 && yy<8 && ui->hud[id][yy*8+xx]>1)edge=true;
    }
    if(!edge)pixels[row*8+col]=0;
  }
  tile(pixels,colors,c,x,y,(attr&0x4000)!=0,(attr&0x8000)!=0);
}
void FzeroUiRaceInstruments(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,uint16_t health,unsigned speed,uint64_t tick) {
  if(!ui || !ui->valid)return;
  if(health&0x8000)health=0;
  if(health>0x800)health=0x800;
  unsigned filled=health>>5;
  /* Native AA06: ordinary fixed red; low power alternates its warning
   * HDMA record for eight frames of each sixteen-frame period. Fill uses
   * the original window's saturated 5-bit addition to scenery. */
  unsigned record=health<0x200 && (tick&8)?4:0;
  for(unsigned dy=3;dy<=10;++dy)for(unsigned dx=0;dx<filled;++dx) {
    int xx=x+32+(int)dx,yy=y+(int)dy;
    for(unsigned sy=0;sy<c.scale;++sy)for(unsigned sx=0;sx<c.scale;++sx) {
      int px=xx*(int)c.scale+(int)sx,py=yy*(int)c.scale+(int)sy;
      if(px<0 || px>=c.width || py<0 || py>=c.height)continue;
      uint32_t old=c.pixels[(size_t)py*c.width+px],color=0xff000000;
      for(unsigned component=0;component<3;++component) {
        unsigned shift=(2-component)*8,v=(old>>(shift+3))&31;
        v+=ui->power_colors[record][component+1]&31;if(v>31)v=31;
        color|=((v<<3)|(v>>2))<<shift;
      }
      c.pixels[(size_t)py*c.width+px]=color;
    }
  }
  /* AA97's boundary/outline tile layout. Keep the gauge's black edge;
   * only the lettering's backing becomes transparent. */
  for(unsigned row=0;row<2;++row)for(unsigned col=18;col<=30;++col) {
    unsigned attr=ui->hud_map[row*32+col];
    if(col>=22 && col<30) {
      unsigned start=(col-22)*8,amount=filled>start?filled-start:0;
      unsigned id=amount>=8?0x10:start>filled?0x19:0x18-amount;
      attr=(attr&0xff00)|id;
    }
    hud_tile(ui,c,x+((int)col-18)*8,y+(int)row*8,attr,col>=22,col>=22 && record==4?ui->power_warning:0);
  }
  /* SPEED, native cyan digits and km/h retain their exact BG3 attributes. */
  for(unsigned col=20;col<=29;++col) {
    unsigned attr=ui->hud_map[64+col];
    if(col>=24 && col<=26) {
      unsigned divisor=col==24?100:col==25?10:1;
      attr=(attr&0xff00)|((speed/divisor)%10);
    }
    hud_tile(ui,c,x+((int)col-18)*8,y+16,attr,false,0);
  }
  for(unsigned col=20;col<=29;++col)
    hud_tile(ui,c,x+((int)col-18)*8,y+24,ui->hud_map[96+col],false,0);
}
void FzeroUiBoosts(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned count,bool active) {
  if(!ui || !ui->valid)return;
  /* B825/B85E change the palette of the SAME $5F tile: $31 available,
   * $33 currently burning, $35 spent. No substitute S or empty glyph. */
  for(unsigned i=0;i<(ui->cgp_hud?1u:3u);++i) {
    unsigned p=ui->cgp_hud?(active?1:count?0:2):i<count?0:active && i==count?1:2;
    uint32_t colors[16];memcpy(colors,ui->boost_palette[p],sizeof(colors));
    tile(ui->objects[0x5f],colors,c,x+(int)i*8,y,false,false);
  }
}
void FzeroUiTitleText(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *text,bool dim) {
  if(!ui || !ui->valid || !text)return;
  for(;*text;x+=*text=='V'?10:8,++text)if(*text>='A' && *text<='Z') {
    const uint8_t *glyph=ui->title_letters[*text-'A'];
    tile(glyph,ui->title_palette[dim],c,x,y,false,false);
    tile(glyph+64,ui->title_palette[dim],c,x,y+8,false,false);
  }
}

void FzeroUiMachineSelect(const FzeroNativeUi *ui,FzeroUiCanvas c,unsigned machine,
    uint64_t age,unsigned player,bool joined,bool ready) {
  FzeroUiMachineSelectScroll(ui,c,machine,age,player,joined,ready,0);
}
void FzeroUiMachineSelectScroll(const FzeroNativeUi *ui,FzeroUiCanvas c,unsigned machine,
    uint64_t age,unsigned player,bool joined,bool ready,int slide) {
  if(!ui || !ui->valid || !ui->information_valid || machine>=FZERO_MACHINE_COUNT ||
      !(ui->machines_available&(1u<<machine)) || !c.scale)return;
  int left=(c.width/(int)c.scale-256)/2;
  bool expanded=ui->machines_available==4095;
  unsigned slot=FzeroMachineSlot(machine),native_row=slot==1?2:slot==2?1:slot;
  unsigned selected=expanded?FzeroMachineIndex(machine)%4:native_row;
  FzeroUiPlayerTag(ui,c,left+120,8,player,player?FZERO_UI_YELLOW:FZERO_UI_CYAN);
  /* A 104px native car pane exposes neighboring columns, like the 1P
   * carousel. Its clip is independent of split/full view and player count. */
  uint32_t car_pixels[104*224]={0};FzeroUiCanvas car={car_pixels,104,224,1};
  int offset=expanded && age<12?slide*(56-(int)age*56/12):0;
  for(int relative=expanded?-1:0;relative<=(expanded?1:0);++relative)for(unsigned row=0;row<4;++row) {
    static const unsigned stock_order[]={0,2,1,3};
    unsigned column=(FzeroMachineIndex(machine)/4+3+relative)%3;
    unsigned id=expanded?FzeroMachineAt(column*4+row):stock_order[row];
    if(joined && !relative && row==selected)continue;
    FzeroUiMenuMachine(ui,car,32+relative*56+offset,47+(int)row*40,id,false);
  }
  if(joined) {
    FzeroShip spin;
    if(FzeroShipMenu(&spin,machine,age,ui->tables)) {
      int origin=byte(ui->tables,0x038b0a+selected);
      for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        uint32_t color=spin.pixels[y*64+x];
        if(color)FzeroUiRect(car,52+spin.x+(int)x+offset,origin+spin.y+(int)y-1,1,1,color);
      }
    }
    FzeroUiTile(ui,car,16,55+(int)selected*40,0x2f,FZERO_UI_CYAN,true,false);
  }
  for(unsigned y=0;y<224;++y)for(unsigned x=0;x<104;++x)if(car_pixels[y*104+x])
    FzeroUiRect(c,left+(int)x,(int)y,1,1,car_pixels[y*104+x]);
  /* Native information maps and the author's original card tiles. GP's
   * YES/NO row becomes an uninterrupted original frame border for readiness. */
  for(unsigned y=4;y<24;++y)for(unsigned x=13;x<30;++x) {
    unsigned entry=ui->information[native_row][y*32+(y==23 && x>13 && x<29?14:x)];
    unsigned id=entry&1023,pal=(entry>>10)&7;
    if(id>=256 || pal>1)continue;
    tile(ui->tiles[id],pal?ui->palette[1]:ui->information_palette[machine],c,
        left+(int)x*8,(int)y*8-1,(entry&0x4000)!=0,(entry&0x8000)!=0);
  }
  if(FzeroMachineProfile(ui->tables,machine)) {
    /* Same 44 data-only slots used by the existing 1P card adapter. */
    static const unsigned spans[][3]={{22,29,7},{22,25,9},{22,24,11},{22,25,13},
      {17,28,17},{16,24,18},{15,21,19},{15,18,20},{15,16,21}};
    uint32_t colors[16]={0};colors[13]=ui->palette[0][2];colors[15]=ui->palette[0][7];unsigned n=0;
    for(unsigned span=0;span<9;++span)for(unsigned x=spans[span][0];x<spans[span][1];++x) {
      int y=(int)spans[span][2]*8-1;FzeroUiRect(c,left+(int)x*8,y,8,8,0xff000000);
      tile(ui->cards[machine][n++],colors,c,left+(int)x*8,y,false,false);
    }
    FzeroUiRect(c,left+112,30,120,18,0xff000000);
    const char *name=FzeroMachineName(machine);int x=left+172-(int)strlen(name)*4;
    for(;*name;++name,x+=8)if(*name>='A' && *name<='Z') {
      const uint8_t *glyph=ui->menu_letters[*name-'A'];
      tile(glyph,ui->information_palette[machine],c,x,31,false,false);
      tile(glyph+64,ui->information_palette[machine],c,x,39,false,false);
    } else if(*name=='.') {
      uint32_t colors[16]={0};colors[2]=colors[3]=ui->information_palette[machine][2];
      tile(ui->objects[0xa8],colors,c,x,31,false,false);
    }
  }
  const char *status=!joined?"PRESS START":ready?"READY":NULL;
  if(status)FzeroUiText(ui,c,left+144,192,status,FZERO_UI_MENU,ready?FZERO_UI_YELLOW:FZERO_UI_CYAN);
}
