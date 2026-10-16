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
      unsigned offset=p==2 && planes==3?16+y:(p/2)*16+y*2+p%2;
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
