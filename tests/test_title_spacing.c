#include "fzero_native_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do {if(!(e)){fprintf(stderr,"title spacing:%d: %s\n",__LINE__,#e);exit(1);}}while(0)
int main(void) {
  static FzeroNativeUi ui;ui.valid=true;
  memset(ui.title_v,1,sizeof(ui.title_v));
  memset(ui.title_letters['S'-'A'],2,128);
  ui.title_palette[0][1]=0xff00ff00;ui.title_palette[0][2]=0xffff0000;
  for(unsigned scale=1;scale<=4;scale*=2) {
    unsigned w=32*scale,h=16*scale;uint32_t *pixels=malloc(w*h*4);CHECK(pixels);
    for(unsigned i=0;i<w*h;++i)pixels[i]=0x11223344;
    FzeroUiTitleText(&ui,(FzeroUiCanvas){pixels,(int)w,(int)h,scale},0,0,"VS",false);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
      uint32_t expected=x<12*scale?0xff00ff00:x<14*scale?0x11223344:x<22*scale?0xffff0000:0x11223344;
      CHECK(pixels[y*w+x]==expected);
    }
    for(unsigned i=0;i<w*h;++i)pixels[i]=0x11223344;
    FzeroUiTitleText(&ui,(FzeroUiCanvas){pixels,(int)w,(int)h,scale},0,0,"SV",false);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
      CHECK(pixels[y*w+x]==(x<8*scale?0xffff0000:x<20*scale?0xff00ff00:0x11223344));
    free(pixels);
  }
  puts("PASS title lettering: VS gap and current-glyph advances at 1/2/4x");return 0;
}
