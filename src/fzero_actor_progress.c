#include "fzero_actor.h"

typedef struct Gate { uint16_t x, y; uint8_t angle, flags; } Gate;
typedef struct GateSide {
  uint16_t difference, minor;
  uint8_t quadrant, special;
  bool crossed;
} GateSide;

static uint16_t word(const uint8_t *world, unsigned address) {
  return world[address] | (uint16_t)world[address+1] << 8;
}
static unsigned table(FzeroActorTables t, unsigned address) { return t.read(t.context,address); }
static Gate lookup(FzeroActor *a, const uint8_t *world, unsigned index) {
  /* $E309 human branch. $1130's high bit selects the authored branch. */
  if (world[0x10da] && (a->branch & 0x80) && (world[0x1700+index] & 0x20))
    index = (index - world[0x10d6] + world[0x10d8]) & 255;
  Gate gate={word(world,0x1200+index*2),word(world,0x1400+index*2),world[0x1600+index],world[0x1700+index]};
  a->checkpoint_angle=gate.angle;
  return gate;
}
void FzeroActorCheckpointInitialize(FzeroActor *a, const uint8_t world[0x20000]) {
  Gate next=lookup(a,world,a->checkpoint);
  unsigned previous=a->checkpoint ? a->checkpoint-1 : world[0xad];
  Gate prev=lookup(a,world,previous);
  a->next_checkpoint_x=next.x;a->next_checkpoint_y=next.y;
  a->next_checkpoint_angle=next.angle;a->next_checkpoint_flags=next.flags;
  a->previous_checkpoint_x=prev.x;a->previous_checkpoint_y=prev.y;
  a->previous_checkpoint_angle=prev.angle;a->previous_checkpoint_flags=prev.flags;
}

/* $E7A8..$E845. Axis/diagonal equality uses inclusive gate tables. Coordinates
 * here are authored checkpoint coordinates, matching the stock unsigned
 * subtract/absolute semantics; this is independent of display projection. */
static GateSide side(uint16_t px,uint16_t py,uint16_t gx,uint16_t gy,unsigned octant,FzeroActorTables t) {
  GateSide s={0,0,0,0,false};
  unsigned dx=px>gx ? px-gx : gx-px,dy=py>gy ? py-gy : gy-py;
  if (!dx && !dy) { s.crossed=true;return s; }
  s.quadrant=(gx<px ? 1 : 0)|(gy<py ? 2 : 0);
  if (!dy) s.special=gx<px ? 0x87 : 0x85;
  else if (!dx) s.special=0x84|s.quadrant;
  else if (dx==dy) s.special=0x80|s.quadrant;
  else {
    if (dx>dy) { s.quadrant|=4;s.difference=(uint16_t)(dx-dy);s.minor=(uint16_t)dy; }
    else { s.difference=(uint16_t)(dy-dx);s.minor=(uint16_t)dx; }
  }
  s.crossed=s.special ? (table(t,0xe846+(s.special&127)) & (1u<<octant))!=0
                      : (table(t,0xe856+octant) & (1u<<s.quadrant))!=0;
  return s;
}
/* $E85E..$E8B0's bounded octant approximation, including its three bins. */
static uint8_t direction(GateSide s,FzeroActorTables t) {
  if (s.special) return (uint8_t)table(t,(s.special&4 ? 0xe8b1 : 0xe8b5)+(s.special&3));
  unsigned bucket=s.difference<s.minor ? 2 : s.difference<3u*s.minor ? 1 : 0;
  return (uint8_t)table(t,0xe8b9+s.quadrant*3+bucket);
}
static unsigned distance(unsigned a,unsigned b) {
  unsigned d=a>b?a-b:b-a;
  return d<96?d:192-d;
}
/* $E778. Stock halves each angle before averaging; preserve the lost bits. */
static unsigned boundary_average(unsigned old,unsigned wall) {
  unsigned d=old>wall?old-wall:wall-old;
  unsigned angle=(old>>1)+(wall>>1)+(d>=96?96:0);
  return angle>=192?angle-192:angle;
}
static void rebound(FzeroActor *a) {
  unsigned old=a->momentum>>8,angle=old;
  a->slide=a->next_checkpoint_angle; /* $1D is scratch until physics recomputes slide. */
  if (a->ground_flags&0x80) angle=a->wall_angle;
  else if (a->ground_flags&0x40) angle=boundary_average(old,a->wall_angle);
  else if (a->ground_flags&0x20) {
    unsigned normal=a->next_checkpoint_angle;
    if (normal>=96) normal-=96;
    unsigned reflected=(normal*2+192-old)%192;
    angle=distance(old,a->wall_angle)>=distance(reflected,a->wall_angle)
        ?reflected:boundary_average(old,a->wall_angle);
  } else return;
  a->momentum=(uint16_t)(angle<<8);
}
int FzeroActorCheckpointForward(FzeroActor *a,const uint8_t world[0x20000],FzeroActorTables t) {
  GateSide s=side(a->x,a->y,a->next_checkpoint_x,a->next_checkpoint_y,a->next_checkpoint_flags&7,t);
  int delta=0;
  if (s.crossed) {
    a->checkpoint=a->checkpoint==world[0xad]?0:(uint8_t)(a->checkpoint+1);
    Gate next=lookup(a,world,a->checkpoint);
    a->previous_checkpoint_x=a->next_checkpoint_x;a->previous_checkpoint_y=a->next_checkpoint_y;
    a->previous_checkpoint_angle=a->next_checkpoint_angle;a->previous_checkpoint_flags=a->next_checkpoint_flags;
    a->next_checkpoint_x=next.x;a->next_checkpoint_y=next.y;
    a->next_checkpoint_angle=next.angle;a->next_checkpoint_flags=next.flags;
    delta=1;
  } else if (!(a->status2&1)) a->wall_angle=direction(s,t);
  rebound(a);
  return delta;
}
int FzeroActorCheckpointBackward(FzeroActor *a,const uint8_t world[0x20000],FzeroActorTables t) {
  GateSide s=side(a->x,a->y,a->previous_checkpoint_x,a->previous_checkpoint_y,a->previous_checkpoint_flags&7,t);
  int delta=0;
  if (!s.crossed) {
    a->checkpoint=a->checkpoint?(uint8_t)(a->checkpoint-1):world[0xad];
    unsigned previous=a->checkpoint?a->checkpoint-1:world[0xad];
    Gate prev=lookup(a,world,previous);
    a->next_checkpoint_x=a->previous_checkpoint_x;a->next_checkpoint_y=a->previous_checkpoint_y;
    a->next_checkpoint_angle=a->previous_checkpoint_angle;a->next_checkpoint_flags=a->previous_checkpoint_flags;
    a->previous_checkpoint_x=prev.x;a->previous_checkpoint_y=prev.y;
    a->previous_checkpoint_angle=prev.angle;a->previous_checkpoint_flags=prev.flags;
    delta=-1;
  } else if (a->status2&1) a->wall_angle=direction(s,t);
  rebound(a);
  return delta;
}
int FzeroActorProgressTick(FzeroActor *a,const uint8_t world[0x20000],FzeroActorTables t,uint8_t tick) {
  if (a->recovery_active) return 0;
  if (((a->surface_extra&0x80) && (a->branch&0x80)) ||
      (!(a->surface_extra&0x80) && (a->surface_extra&0x40) && !(a->branch&0x80))) {
    a->branch^=0x80;FzeroActorCheckpointInitialize(a,world);
  }
  unsigned difference=distance(a->momentum>>8,a->next_checkpoint_angle);
  if (!(a->status2&1)) { if (difference>=72) a->status2|=1; }
  else if (difference<24) a->status2&=0xfe;
  bool forward=a->finish_side ? !(a->status2&1) : (tick&1)!=0;
  return forward?FzeroActorCheckpointForward(a,world,t):FzeroActorCheckpointBackward(a,world,t);
}
int FzeroActorCrossFinish(FzeroActor *a,const uint8_t world[0x20000]) {
  unsigned row=a->y>>8;
  if ((a->x>>8)!=world[0xae] || (row!=world[0xaf] && ((row-1)&255)!=world[0xaf])) {
    a->finish_side=0;return 0;
  }
  int crossing=0;
  if ((a->x&255)<128) {
    if (a->finish_side && !(a->finish_side&0x80)) crossing=-1;
    a->finish_side=0x80;
  } else {
    if (a->finish_side&0x80) crossing=1;
    a->finish_side=0x40;
  }
  return crossing;
}
/* $D9C8..$DAB7. Stock authored shortcut recovery follows takeoff/landing
 * world coordinates; no opponent rank or display projection is involved. */
void FzeroActorRecoveryTrigger(FzeroActor *a,const uint8_t world[0x20000],FzeroActorTables t) {
  if (!a->jump_watch) {
    if ((a->state&0x81)==0x80) { a->jump_watch=255;a->takeoff_x=a->x;a->takeoff_y=a->y; }
    return;
  }
  if ((a->state&0x80) || (a->race_state&0x40)) return;
  a->jump_watch=0;a->landing_x=a->x;a->landing_y=a->y;
  unsigned index=world[0x1075];
  unsigned entry=table(t,0xdab8+index)|(table(t,0xdab9+index)<<8);
  int checkpoint=-1;
  switch (entry) {
    case 0xda0b:
      if (a->landing_y>=0x400) checkpoint=a->takeoff_x>=0x1300 && a->takeoff_x<0x1680?15:20;
      break;
    case 0xda3a:
      if (a->takeoff_x<0x1080 && a->landing_y<0x9f8 && a->landing_x>=0xb00) { checkpoint=46;break; }
      /* fall through */
    case 0xda25:
      if (a->landing_x>=0x1700 && a->landing_y<0xa00) checkpoint=26;
      break;
    case 0xda58:
      if (a->landing_x>=0x1b00 && a->landing_y>=0x400) { checkpoint=28;break; }
      /* fall through */
    case 0xda6c:
      if (a->landing_x>=0x1400 && a->landing_y>=0x600) checkpoint=55;
      break;
    case 0xda80:
      if (a->landing_x>=0x1e00) checkpoint=78;
      break;
  }
  if (checkpoint<0) return;
  a->checkpoint=(uint8_t)checkpoint;a->branch=0;FzeroActorCheckpointInitialize(a,world);
  --a->recovery_active;a->recovery_angle=a->next_checkpoint_angle;
  a->recovery_x=(uint16_t)(a->next_checkpoint_x+a->previous_checkpoint_x)>>1;
  a->recovery_y=(uint16_t)(a->next_checkpoint_y+a->previous_checkpoint_y)>>1;
}
