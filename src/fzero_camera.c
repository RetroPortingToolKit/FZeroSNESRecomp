#include "fzero_camera.h"
#include <string.h>

static unsigned byte(FzeroActorTables t,unsigned address) { return t.read(t.context,address); }
static uint16_t word(FzeroActorTables t,unsigned address) { return (uint16_t)(byte(t,address)|(byte(t,address+1)<<8)); }
static int16_t signed_word(uint16_t value) { return (int16_t)(value<0x8000?(int32_t)value:(int32_t)value-65536); }

bool FzeroCameraBuild(FzeroCamera *c,const FzeroActor *a,FzeroActorTables t) {
  if (!c || !a || !t.read || a->x>=8192 || a->y>=4096 || a->facing>=0xc000) return false;
  memset(c,0,sizeof(*c));
  /* $97BC/$97E3. Keep the representative selected by the stock additions:
   * a center may be the coordinate modulo 1024 plus a whole map period. */
  c->scroll_x=(uint16_t)((a->x-128u)&1023);c->center_x=c->scroll_x+128;
  c->scroll_y=(uint16_t)((a->y-176u)&1023);c->center_y=c->scroll_y+176;
  /* $A60C regular-race branch, including integer divide and its lost bits. */
  unsigned quotient=a->facing/3,offset=(quotient-(quotient>>3))>>4;
  c->background_h=offset&255;c->background_v=(uint16_t)byte(t,0xa69f+(offset>>8));
  c->skyline_h=(a->facing>>6)&255;c->skyline_v=(uint16_t)byte(t,0xa6a0+(a->facing>>14));
  /* $980E's inverse facing, normalized explicitly under the versus interface
   * rather than inheriting an unrelated caller's processor carry flag. */
  unsigned angle=a->facing>>8;c->inverse_angle=(uint8_t)(angle?192-angle:0);
  /* $03:94CF..95D5 chooses signed banks and two 352-byte perspective tables.
   * $03:97EA's bank-$7E negative copy is the exact two's complement of $0E.
   * Decode immutable ROM directly, sharing no streamed tilemap/camera state. */
  unsigned quadrant=c->inverse_angle/48,within=c->inverse_angle%48;
  unsigned cosine=quadrant&1?within:48-within,sine=quadrant&1?48-within:within;
  for (unsigned row=0;row<176;++row) {
    uint16_t cv=word(t,0x0e8000+cosine*352+row*2),sv=word(t,0x0e8000+sine*352+row*2);
    if (quadrant==1 || quadrant==2) cv=(uint16_t)(0u-cv);
    if (quadrant>=2) sv=(uint16_t)(0u-sv);
    c->coefficients[row][0]=c->coefficients[row][3]=signed_word(cv);
    c->coefficients[row][1]=signed_word(sv);c->coefficients[row][2]=signed_word((uint16_t)(0u-sv));
  }
  return true;
}
bool FzeroCameraAtLine(const FzeroCamera *c,unsigned scanline,int16_t m[8]) {
  if (!c || !m || scanline<48 || scanline>224) return false;
  unsigned row=scanline-48;if(row>175)row=175;
  memcpy(m,c->coefficients[row],sizeof(c->coefficients[row]));
  m[4]=(int16_t)c->center_x;m[5]=(int16_t)c->center_y;
  m[6]=(int16_t)c->scroll_x;m[7]=(int16_t)c->scroll_y;
  return true;
}
uint16_t FzeroCameraFlashColor(uint16_t base,unsigned phase,bool surface) {
  if(surface)return phase?(base>>1)&0x3def:base;
  phase&=7;
  if(!phase)return base;
  if(phase>=4 && phase<=6)return 0x7fff;
  unsigned half=((0x7fff-base)>>1)&0x3def,quarter=(half>>1)&0x1ce7;
  return (uint16_t)(base+(phase==1?quarter:phase==3?half+quarter:half));
}
