#include "fzero_native_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do {if(!(e)){fprintf(stderr,"title spacing:%d: %s\n",__LINE__,#e);exit(1);}}while(0)
static uint8_t rom_read(void *context,uint32_t address) {
  return ((const uint8_t *)context)[((address&0x7f0000)>>1)|(address&0x7fff)];
}
static void native_title(const char *rom_path,const char *preview) {
  FILE *f=fopen(rom_path,"rb");CHECK(f);
  uint8_t *rom=malloc(0x80000);CHECK(rom);
  CHECK(fread(rom,1,0x80000,f)==0x80000);fclose(f);
  static FzeroNativeUi ui;
  CHECK(FzeroNativeUiInit(&ui,(FzeroActorTables){rom_read,rom,0}));
  const uint8_t *v=ui.title_letters['V'-'A'],*s=ui.title_letters['S'-'A'];
  unsigned vtop=16,vbottom=0,stop=16,sbottom=0;
  for(unsigned y=0;y<16;++y)for(unsigned x=0;x<8;++x) {
    if(v[y*8+x]) {if(y<vtop)vtop=y;if(y>vbottom)vbottom=y;}
    if(s[y*8+x]) {if(y<stop)stop=y;if(y>sbottom)sbottom=y;}
    if(v[y*8+x] && v[y*8+x]!=5)CHECK(v[y*8+x]==ui.title_letters['I'-'A'][y*8+3]);
  }
  CHECK(vtop==stop && vbottom==sbottom);
  if(preview) {
    enum {width=144*4,height=88*4};
    uint32_t *pixels=malloc(width*height*4);CHECK(pixels);
    for(unsigned i=0;i<width*height;++i)pixels[i]=0xffa19d94;
    FzeroUiCanvas c={pixels,width,height,4};
    const char *options[]={"GRAND PRIX","PRACTICE","RECORDS","VS MODE"};
    for(unsigned row=0;row<4;++row)FzeroUiTitleText(&ui,c,4,4+row*20,options[row],false);
    f=fopen(preview,"wb");CHECK(f);fprintf(f,"P6\n%d %d\n255\n",width,height);
    for(unsigned i=0;i<width*height;++i) {
      uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};
      CHECK(fwrite(rgb,1,3,f)==3);
    }
    fclose(f);free(pixels);
  }
  free(rom);puts("PASS native title: V/S cap, baseline, cell width and row shading");
}
int main(int argc,char **argv) {
  static FzeroNativeUi ui;ui.valid=true;
  memset(ui.title_letters['V'-'A'],1,128);
  memset(ui.title_letters['S'-'A'],2,128);
  ui.title_palette[0][1]=0xff00ff00;ui.title_palette[0][2]=0xffff0000;
  for(unsigned scale=1;scale<=4;scale*=2) {
    unsigned w=32*scale,h=16*scale;uint32_t *pixels=malloc(w*h*4);CHECK(pixels);
    for(unsigned i=0;i<w*h;++i)pixels[i]=0x11223344;
    FzeroUiTitleText(&ui,(FzeroUiCanvas){pixels,(int)w,(int)h,scale},0,0,"VS",false);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
      uint32_t expected=x<8*scale?0xff00ff00:x<10*scale?0x11223344:x<18*scale?0xffff0000:0x11223344;
      CHECK(pixels[y*w+x]==expected);
    }
    for(unsigned i=0;i<w*h;++i)pixels[i]=0x11223344;
    FzeroUiTitleText(&ui,(FzeroUiCanvas){pixels,(int)w,(int)h,scale},0,0,"SV",false);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
      CHECK(pixels[y*w+x]==(x<8*scale?0xffff0000:x<16*scale?0xff00ff00:0x11223344));
    free(pixels);
  }
  puts("PASS title lettering: VS gap and current-glyph advances at 1/2/4x");
  if(argc>1)native_title(argv[1],argc>2?argv[2]:NULL);
  return 0;
}
