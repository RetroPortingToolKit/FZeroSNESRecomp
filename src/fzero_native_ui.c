#include "fzero_native_ui.h"
#include <ctype.h>
#include <string.h>

const char *FzeroUiCourseName(unsigned course) {
  static const char *names[]={"MUTE CITY I","BIG BLUE","SAND OCEAN","DEATH WIND I","SILENCE",
    "MUTE CITY II","PORT TOWN I","RED CANYON I","WHITE LAND I","WHITE LAND II",
    "MUTE CITY III","DEATH WIND II","PORT TOWN II","RED CANYON II","FIRE FIELD"};
  return course<15?names[course]:"";
}
const char *FzeroUiMachineName(unsigned machine) {
  static const char *names[]={"BLUE FALCON","WILD GOOSE","GOLDEN FOX","FIRE STINGRAY"};
  return machine<4?names[machine]:"";
}

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
  for(unsigned p=0;p<4;++p)for(unsigned i=0;i<8;++i)
    ui->palette[p][i]=rgb(word(t,0x0ee100+p*32+i*2));
  /* $00:A6A3 uploads the original two-plane race digits, gauge and symbols. */
  for(unsigned i=0;i<56;++i)decode(ui->hud[i],t,0x0cc000+i*16,2);
  /* The course-introduction OBJ stream has clear 8x16 numerals. Use these
   * for player/menu identities instead of the thin race-clock glyphs. */
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
    ui->effect_palette[i]=rgb(word(t,0x0fcd60+i*2));
  }
  /* The title menu has its own 8x16 OBJ alphabet and green scanline shading,
   * distinct from course introductions. These are the exact native word tiles
   * uploaded at $120..$156. M/V are absent from those words; retain their
   * cartridge shapes with the title menu's row colors and black outline. */
  static const struct {char letter;unsigned address;} title_glyphs[]={
    {'G',0x0cf800},{'R',0x0cf820},{'A',0x0cf840},{'N',0x0cf860},
    {'D',0x0cf880},{'P',0x0cf8c0},{'I',0x0cf900},{'X',0x0cf920},
    {'E',0x0cf960},{'C',0x0cf980},{'O',0x0cf9a0},{'T',0x0cfc40},{'S',0x0cfcc0}};
  for(unsigned n=0;n<sizeof(title_glyphs)/sizeof(*title_glyphs);++n)
    for(unsigned half=0;half<2;++half)
      decode(ui->title_letters[title_glyphs[n].letter-'A']+half*64,t,title_glyphs[n].address+half*0x200,4);
  for(unsigned n=0;n<2;++n) {
    unsigned letter=n?'V':'M',id=n?0xa3:0x8a;
    uint8_t *glyph=ui->title_letters[letter-'A'];
    for(unsigned y=1;y<15;++y)for(unsigned x=0;x<8;++x) {
      unsigned sy=(n?2:3)+(y-1)*(n?11:10)/14;
      unsigned source=ui->objects[id+(sy/8)*16][(sy&7)*8+x];
      /* V's taper/tip uses the highlight color after the inner stroke ends. */
      bool stroke=source==3 || (n && source==2);
      if(stroke)glyph[y*8+x]=ui->title_letters['I'-'A'][y*8+3];
    }
    /* Main-menu letters use a black outline rather than the introduction's
     * bright left edge and drop shadow. Keep that same outline on M and V. */
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
  ui->valid=true;return true;
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
    if(color)FzeroUiRect(c,x+(int)col,y+(int)row,1,1,palette[color]);
  }
}
void FzeroUiTile(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned id,FzeroUiInk ink,bool fx,bool fy) {
  if(ui && ui->valid && id<256 && (unsigned)ink<4)tile(ui->tiles[id],ui->palette[ink],c,x,y,fx,fy);
}
void FzeroUiHudTile(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,unsigned id,FzeroUiInk ink,bool fy) {
  if(!ui || !ui->valid || id>=56 || (unsigned)ink>=4)return;
  uint32_t colors[4]={0,ui->palette[ink][2],ui->palette[ink][3],ui->palette[ink][1]};
  tile(ui->hud[id],colors,c,x,y,false,fy);
}
void FzeroUiText(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *s,FzeroUiFont font,FzeroUiInk ink) {
  if(!ui || !ui->valid || (unsigned)ink>=4 || !s)return;
  for(;*s;++s,x+=8) {
    unsigned ch=(unsigned)toupper((unsigned char)*s),id=0xff;
    if(ch==' ')continue;
    if(font!=FZERO_UI_MENU) {
      if(ch>='0' && ch<='9') {
        if(font==FZERO_UI_HUD)FzeroUiHudTile(ui,c,x,y,ch-'0',ink,false);
        else {
          const uint8_t *pixels=ui->numbers[ch-'0'];
          uint32_t colors[4]={0,ui->palette[ink][1],ui->palette[ink][2],ui->palette[ink][3]};
          for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col)
            if(pixels[row*16+col])FzeroUiRect(c,x+(int)col,y+(int)row,1,1,colors[pixels[row*16+col]]);
        }
      }
      else if(ch>='A' && ch<='Z') {
        id=ch=='A'?0xaf:ch=='B'?0xbf:ch=='M'?0xcf:ch=='N'?0xce:0xc4+ch-'C';
        FzeroUiTile(ui,c,x,y,id,ink,false,false);
      } else if(ch==':') {
        FzeroUiRect(c,x+3,y+2,1,1,ui->palette[ink][2]);FzeroUiRect(c,x+3,y+5,1,1,ui->palette[ink][2]);
      } else if(ch=='.' || ch=='\'' || ch=='"') {
        int yy=ch=='.'?6:0;FzeroUiRect(c,x+2,y+yy,1,2,ui->palette[ink][2]);
        if(ch=='"')FzeroUiRect(c,x+5,y+yy,1,2,ui->palette[ink][2]);
      } else if(ch=='-')FzeroUiHudTile(ui,c,x,y,0x0d,ink,false);
      continue;
    }
    if(ch>='A' && ch<='Z') {unsigned n=ch-'A';id=n/16*32+n%16;}
    else if(ch>='0' && ch<='9') {
      uint32_t colors[4]={0,ui->palette[ink][1],ui->palette[ink][2],ui->palette[ink][3]};
      const uint8_t *pixels=ui->numbers[ch-'0'];
      for(unsigned row=0;row<16;++row)for(unsigned col=0;col<8;++col)
        if(pixels[row*8+col])FzeroUiRect(c,x+(int)col,y+(int)row,1,1,colors[pixels[row*8+col]]);
      continue;
    }
    if(id==0xff)continue;
    FzeroUiTile(ui,c,x,y,id,ink,false,false);FzeroUiTile(ui,c,x,y+8,id+16,ink,false,false);
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
  if(!ui || !ui->valid || m>=4)return;
  for(unsigned row=0;row<16;++row)for(unsigned col=0;col<24;++col) {
    unsigned color=ui->machines[m][row*24+col];
    if(color)FzeroUiRect(c,x+(int)col,y+(int)row,1,1,ui->machine_palette[selected?1:0][m][color]);
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
void FzeroUiTitleText(const FzeroNativeUi *ui,FzeroUiCanvas c,int x,int y,const char *text,bool dim) {
  if(!ui || !ui->valid || !text)return;
  for(;*text;++text,x+=8)if(*text>='A' && *text<='Z') {
    const uint8_t *glyph=ui->title_letters[*text-'A'];
    tile(glyph,ui->title_palette[dim],c,x,y,false,false);
    tile(glyph+64,ui->title_palette[dim],c,x,y+8,false,false);
  }
}
