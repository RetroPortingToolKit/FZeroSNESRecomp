#pragma once
#include "fzero_versus.h"
#include <math.h>

/* Default headless driver: choose ordinary pad inputs from completed
 * actor/checkpoint observations. Never edit actors, world, progress or clocks.
 * This is not part of the shipped human controls or simulation profile.
 * The separately flagged teleport lifecycle fixture is defined below. */
static unsigned local_driver_word(const uint8_t *world,unsigned at) {
  return world[at]|(unsigned)world[at+1]<<8;
}
static void local_driver_point(const uint8_t *world,const FzeroActor *actor,unsigned gate,double *x,double *y) {
  if(world[0x10da] && (actor->branch&0x80) && (world[0x1700+gate]&0x20))
    gate=(gate-world[0x10d6]+world[0x10d8])&255;
  *x=local_driver_word(world,0x1200+gate*2);*y=local_driver_word(world,0x1400+gate*2);
}
static uint32_t local_driver_player(const FzeroVersus *session,unsigned player) {
  const FzeroVersusSnapshot *race=FzeroVersusCompleted(session);
  const FzeroVersusRacer *racer=&race->racers[player];const FzeroActor *actor=&racer->actor;
  if(racer->status!=FZERO_VS_ACTIVE || (player && race->race_tick<100))return 0;
  if(race->phase==FZERO_VS_COUNTDOWN)return 1;
  const uint8_t *world=FzeroVersusWorld(session);
  double x=actor->x,y=actor->y,tx=actor->next_checkpoint_x,ty=actor->next_checkpoint_y;
  double look=100+actor->speed/20.0,dx=remainder(tx-x,8192),dy=remainder(ty-y,4096),distance=hypot(dx,dy);
  unsigned gate=actor->checkpoint;tx=x+dx;ty=y+dy;
  for(unsigned n=0;distance<look && n<=(unsigned)world[0xad];++n) {
    look-=distance;x=tx;y=ty;gate=gate==world[0xad]?0:gate+1;
    local_driver_point(world,actor,gate,&tx,&ty);
    dx=remainder(tx-x,8192);dy=remainder(ty-y,4096);distance=hypot(dx,dy);tx=x+dx;ty=y+dy;
    if(distance<0.1)break;
  }
  if(distance>0.1) {
    tx=x+dx*look/distance;ty=y+dy*look/distance;
    double side=player?16:-16;tx-=dy/distance*side;ty+=dx/distance*side;
  }
  dx=remainder(tx-actor->x,8192);dy=remainder(ty-actor->y,4096);
  double angle=atan2(dx,-dy)*192/(2*3.141592653589793);
  double error=remainder(angle-actor->facing/256.0,192);
  double steer=error+0.3*remainder(angle-actor->momentum/256.0,192);
  uint32_t input=steer>1.2?128:steer< -1.2?64:0;
  unsigned target=fabs(error)>16?0x280:fabs(error)>8?0x400:0x780;
  bool jump=(actor->state&0x80)!=0 || actor->surface==4;
  double forward=actor->momentum/256.0*(2*3.141592653589793)/192;
  for(unsigned ahead=16;!jump && ahead<=128;ahead+=16) {
    unsigned sx=(unsigned)((int)actor->x+(int)round(sin(forward)*ahead))&8191;
    unsigned sy=(unsigned)((int)actor->y-(int)round(cos(forward)*ahead))&4095;
    unsigned tile=FzeroActorSampleSurface(world,(uint16_t)sx,(uint16_t)sy);
    jump=tile==0x9e || tile==0xa1 || tile==0xa2;
  }
  /* Ordinary stock jump controls: accelerate into ramps and hold Down in
   * flight. The earlier conservative bot braked before the long gaps. */
  if(jump)return input|1|((actor->state&0x80)?32:0);
  return input|(actor->speed>target?2:1);
}
static uint32_t FzeroLocalDriverInput(const FzeroVersus *session) {
  return session?local_driver_player(session,0)|(local_driver_player(session,1)<<12):0;
}

#include "fzero_local_teleport.h"
