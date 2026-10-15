#include "fzero_actor.h"

static unsigned byte(FzeroActorTables t,unsigned address) { return t.read(t.context,address); }
static unsigned word(FzeroActorTables t,unsigned address) { return byte(t,address)|(byte(t,address+1)<<8); }
static int32_t signed16(uint16_t value) { return value&0x8000?(int32_t)value-65536:value; }
static int32_t wrap_delta(uint16_t value,uint16_t origin,unsigned period) {
  unsigned delta=((unsigned)value-origin)&(period-1);
  return delta>=period/2?(int32_t)delta-(int32_t)period:(int32_t)delta;
}
static int32_t multiply(int32_t coordinate,unsigned magnitude,bool negative) {
  unsigned absolute=(unsigned)(coordinate<0?-coordinate:coordinate);
  int32_t result=(int32_t)(absolute*magnitude/128);
  return (coordinate<0)!=negative?-result:result;
}
/* $DBC4..$DCD8 and $DCD9..$DD1F. The two scalar products truncate separately,
 * as the guest's multiply bus does; a single floating-point matrix differs. */
FzeroActorProjection FzeroActorProject(const FzeroActor *reference,const FzeroActor *a,FzeroActorTables t) {
  FzeroActorProjection p={0};
  int32_t dx=wrap_delta(a->x,reference->x,8192),dy=wrap_delta(a->y,reference->y,4096);
  unsigned angle=reference->facing>>8;
  angle=angle?192-angle:0;
  unsigned sin=byte(t,0x08ec00+angle),cos=byte(t,0x08ec30+angle);
  bool sin_negative=byte(t,0x08ecf0+angle)!=0,cos_negative=byte(t,0x08ed20+angle)!=0;
  p.right=signed16((uint16_t)(multiply(dx,cos,cos_negative)-multiply(dy,sin,sin_negative)));
  p.forward=signed16((uint16_t)(multiply(dx,sin,sin_negative)+multiply(dy,cos,cos_negative)));
  unsigned depth=(unsigned)(p.forward+639)&65535;
  if (depth>=658) return p;
  p.projectable=true;
  p.y=(int32_t)byte(t,0x09ed00+depth);
  unsigned factor=byte(t,0x09ec00+255-(unsigned)p.y);
  unsigned magnitude=(unsigned)(p.right<0?-p.right:p.right)*factor/64;
  p.x=128+(p.right<0?-(int32_t)magnitude:(int32_t)magnitude);
  p.stock_visible=p.x>=-32 && p.x<288;
  return p;
}
static uint16_t angle_sub(uint16_t angle,unsigned amount) {
  return (uint16_t)(angle>=amount?angle-amount:angle-amount+0xc000);
}
static uint16_t angle_add(uint16_t angle,unsigned amount) {
  unsigned result=angle+amount;
  return (uint16_t)(result>=0xc000?result-0xc000:result);
}
static void swap_motion(FzeroActor *a,FzeroActor *b) {
  uint16_t speed=a->speed,momentum=a->momentum;
  a->speed=b->speed;a->momentum=b->momentum;b->speed=speed;b->momentum=momentum;
}
/* $BD51..$BE01, exactly one exchange for the pair. X is the following actor. */
void FzeroActorContactSpeed(FzeroActor *f,FzeroActor *l) {
  f->no_control=(f->no_control&0xff00)|10;
  unsigned difference=((f->facing>>8)+192-(l->facing>>8))%192;
  if (difference>=48 && difference<144) {
    swap_motion(f,l);
    if (((f->speed+l->speed)&65535)<0x200) { f->speed+=0x80;l->speed+=0x80; }
    return;
  }
  unsigned relative=f->speed>=l->speed?f->speed-l->speed:l->speed-f->speed;
  if (f->speed>=l->speed) swap_motion(f,l);
  if (relative<0x100) { f->speed=f->speed>=128?(uint16_t)(f->speed-128):0;l->speed+=128; }
  if (l->speed<0x400) l->speed=0x400;
}
void FzeroActorContactSpin(FzeroActor *a,FzeroActor *b,FzeroActorTables t,uint8_t geometry) {
  /* $BE02 assigns a player table to slot zero and a CPU table to the other
   * slot. Both versus participants use their own PLAYER table under the same
   * verified response interface. Position-derived signs remain stock. */
  unsigned sign=byte(t,0xbe3d+(geometry&6))&0x80;
  a->spin=(uint16_t)(word(t,0xfadd+a->machine*2)|(sign<<8));
  b->spin=(uint16_t)(word(t,0xfadd+b->machine*2)|(sign<<8));
  a->state|=0x40;b->state|=0x40;
}
void FzeroActorContactLateral(FzeroActor *a,FzeroActor *b,FzeroActorTables t,uint8_t geometry,unsigned separation) {
  unsigned code=geometry&6;
  unsigned difference=((b->momentum>>8)+192-(a->momentum>>8))%192;
  if (difference>=48 && difference<144) code|=8;
  if ((a->speed>>8)<(b->speed>>8)) code|=16;
  unsigned entry=word(t,0xbc82+code),amount=(43-separation)<<8;
  bool a_add=entry==0xbcb0 || entry==0xbccc || entry==0xbce8;
  bool b_add=entry==0xbca2 || entry==0xbcbe || entry==0xbce8;
  unsigned sign=(entry==0xbcb0 || entry==0xbcbe || entry==0xbce8)?0x8000:0;
  a->momentum=a_add?angle_add(a->momentum,amount):angle_sub(a->momentum,amount);
  b->momentum=b_add?angle_add(b->momentum,amount):angle_sub(b->momentum,amount);
  unsigned speed=a->speed>b->speed?a->speed:b->speed;
  unsigned magnitude=speed>>2;
  if (magnitude<128) magnitude=128;
  if (magnitude>=0x240) magnitude=0x240;
  a->spin=b->spin=(uint16_t)(sign|magnitude);
  if (a->state&0x40) a->spin=(uint16_t)(sign|0x100);
  if (b->state&0x40) b->spin=(uint16_t)(sign|0x100);
}
/* Shared two-human rule: one latched contact, two health responses from the
 * same initial pair, one momentum/speed exchange, each human's own machine
 * response. Visibility and sprite reservations never gate this function. */
bool FzeroActorResolveContact(FzeroActor *a,FzeroActor *b,FzeroActorTables t,uint8_t *contact) {
  FzeroActorProjection p=FzeroActorProject(a,b,t);
  int32_t dy=188-p.y,dx=p.x-128;
  if (a->height>=0x1000 || b->height>=0x1000 || !p.projectable ||
      dy < -24 || dy>24 || dx<=-40 || dx>=40) { *contact=0;return false; }
  unsigned geometry=(dy<0?4:0)|(dx<0?2:0);
  unsigned separation=(unsigned)(dx<0?-dx:dx);
  bool direct=separation<24;
  uint8_t kind=direct?2:1;
  if (*contact==kind || (!direct && *contact)) return true;
  FzeroActor before_a=*a,before_b=*b;
  if (!a->damage_cooldown) {
    FzeroActorDamageContact(a,&before_b,t,direct);a->damage_cooldown=direct?9:8;
    if (direct) { a->black_flash=9;if (!(a->animation&0xe0)) a->animation=0x85; }
  }
  if (!b->damage_cooldown) {
    FzeroActorDamageContact(b,&before_a,t,direct);b->damage_cooldown=direct?9:8;
    if (direct) { b->black_flash=9;if (!(b->animation&0xe0)) b->animation=0x85; }
  }
  if (direct) {
    FzeroActorContactSpin(a,b,t,(uint8_t)geometry);
    if (dy<0) FzeroActorContactSpeed(b,a);else FzeroActorContactSpeed(a,b);
  } else FzeroActorContactLateral(a,b,t,(uint8_t)geometry,separation);
  *contact=kind;
  return true;
}
