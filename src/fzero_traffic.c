#include "fzero_traffic.h"
#include "snes/interp816.h"
#include <string.h>

/* Execute only the cartridge's CPU-car checkpoint/steering/physics routines.
 * No game scheduler, PPU/APU, host clock, devices or live guest state is used.
 * This portable callback core is the same implementation as the native actor
 * oracle. Multiplayer owns population/contact; single-player's camera-driven
 * catch-up/population scheduler is not called. Incidental native lap bookkeeping
 * inside the physics walk stays private and never feeds human race results. */
typedef struct Bus {
  FzeroTraffic *traffic;uint8_t *world;FzeroActorTables tables;
  unsigned multiplicand,dividend,product,quotient;bool invalid;
} Bus;
static FzeroActor pose(const FzeroTraffic *t,unsigned i);
static unsigned word(const uint8_t *p,unsigned at) {return p[at]|(unsigned)p[at+1]<<8;}
static void put(uint8_t *p,unsigned at,unsigned v) {p[at]=(uint8_t)v;p[at+1]=(uint8_t)(v>>8);}
static int ram_address(uint32_t address) {
  unsigned bank=address>>16,at=address&65535;
  if(bank==0x7e || bank==0x7f)return (int)(address-0x7e0000);
  return (bank&127)<64 && at<0x2000?(int)at:-1;
}
static uint8_t read_bus(void *opaque,uint32_t address) {
  Bus *b=opaque;int at=ram_address(address);
  if(at>=0)return at<0x2000?b->traffic->ram[at]:b->world[at];
  if(address>=0x4214 && address<=0x4217)
    return (uint8_t)((address<0x4216?b->quotient:b->product)>>((address&1)*8));
  if((address&65535)>=0x8000)return b->tables.read(b->tables.context,address);
  b->invalid=true;return 0;
}
static void write_bus(void *opaque,uint32_t address,uint8_t value) {
  Bus *b=opaque;int at=ram_address(address);
  if(at>=0) {
    if(at<0x2000)b->traffic->ram[at]=value;
    else if(at>=0x10000)b->world[at]=value; /* Native mine/crater events. */
    else b->invalid=true;
    return;
  }
  switch(address) {
    case 0x4202:b->multiplicand=value;return;
    case 0x4203:b->product=b->multiplicand*value;return;
    case 0x4204:b->dividend=(b->dividend&0xff00)|value;return;
    case 0x4205:b->dividend=(b->dividend&255)|(unsigned)value<<8;return;
    case 0x4206:b->quotient=value?b->dividend/value:65535;b->product=value?b->dividend%value:b->dividend;return;
  }
  b->invalid=true;
}
static bool call(Bus *b,unsigned pc,unsigned x,unsigned y) {
  Interp816 cpu={0};cpu.mem=b;cpu.read=read_bus;cpu.write=write_bus;
  cpu.sp=0x1fd;cpu.pc=(uint16_t)pc;cpu.x=(uint16_t)x;cpu.y=(uint16_t)y;cpu.mf=cpu.xf=true;
  b->traffic->ram[0x1fe]=b->traffic->ram[0x1ff]=255;
  for(unsigned step=0;step<50000 && !b->invalid;++step) {
    if(!cpu.k && cpu.pc==0x8e36 && b->world[FZERO_WORLD_RESOURCE] && cpu.x>=2 && cpu.x<=8) {
      /* The same imported-terrain contract as the live guest's $8E36 hook.
       * Keep native checkpoint/physics code, using this course's properties. */
      unsigned slot=cpu.x;uint8_t *p=b->traffic->ram;FzeroActor a=pose(b->traffic,(slot-2)/2);
      a.surface_id=p[0xcd0+slot];a.ground_timer=p[0xcc1+slot];
      FzeroActorCourseSurface(&a,b->world);
      p[0xd70+slot]=a.surface_extra;p[0xd50+slot]=a.surface;
      p[0xcc0+slot]=a.ground_flags;p[0xcc1+slot]=a.ground_timer;
      put(p,0xc00+slot,word(p,0xcc0+slot));put(p,0xb20+slot,a.speed);
      cpu.mf=cpu.xf=true;cpu.pc=0x8e28;
    }
    interp816_runOpcode(&cpu);
    if(cpu.sp==0x1ff)return !b->invalid && !cpu.pc && !cpu.k;
  }
  return false;
}
static FzeroActor pose(const FzeroTraffic *t,unsigned i) {
  const uint8_t *p=t->ram;unsigned x=2+i*2;
  FzeroActor a={0};a.health=0x800;
#define W(at,name) a.name=(uint16_t)word(p,(at)+x)
  W(0xb20,speed);W(0xd60,speed_fraction);W(0xb70,x);W(0xb80,x_fraction);W(0xb90,y);W(0xba0,y_fraction);
  W(0xbd0,facing);W(0xbe0,momentum);W(0xbc0,height);W(0xbb0,height_velocity);W(0xc10,turning);W(0xc20,spin);W(0xd20,no_control);
#undef W
  a.checkpoint=p[0xd00+x];a.branch=p[0x1130+x];return a;
}
static void motion(FzeroTraffic *t,unsigned i,const FzeroActor *a) {
  unsigned x=2+i*2;uint8_t *p=t->ram;
  put(p,0xb20+x,a->speed);put(p,0xbe0+x,a->momentum);put(p,0xc20+x,a->spin);put(p,0xc10+x,a->turning);put(p,0xd20+x,a->no_control);
}
void FzeroTrafficDescribe(const FzeroTraffic *t,FzeroTrafficPose out[FZERO_TRAFFIC_COUNT]) {
  for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i) {
    out[i].actor=pose(t,i);out[i].active=(uint8_t)(t->enabled && (t->present&(1u<<i)) && (t->ram[0xb02+i*2]&128));
  }
}
void FzeroTrafficReset(FzeroTraffic *t) {
  memcpy(t->ram,t->seed,sizeof(t->ram));t->present=0;memset(t->contacts,0,sizeof(t->contacts));
}
bool FzeroTrafficInit(FzeroTraffic *t,uint8_t world[0x20000],const uint8_t native[0x20000],FzeroActorTables tables) {
  if(!t || !world || !native || !tables.read || tables.read(tables.context,0xde57)!=0xe2 ||
      tables.read(tables.context,0x9144)!=8)return false;
  memset(t,0,sizeof(*t));
  memcpy(t->seed+0xad,native+0xad,5);
  memcpy(t->seed+0x1000,native+0x1000,0x56); /* Authored class/CPU performance. */
  t->seed[0x1075]=native[0x1075];
  t->seed[0x10d6]=native[0x10d6];t->seed[0x10d8]=native[0x10d8];t->seed[0x10da]=native[0x10da];
  memcpy(t->seed+0x1200,native+0x1200,0xc00); /* Checkpoints and all three CPU lines. */
  /* Actors 1..4 are generic bumpers. Their authored lane/speed/turn tables
   * come from this course, including imported opponent data. */
  for(unsigned x=0;x<12;x+=2)t->seed[0xb00+x]=0;
  t->seed[0x1060]=t->seed[0x1062]=0;t->enabled=1;
  memcpy(world+FZERO_TRAFFIC_ART,native+0x2000,0x6000);
  memcpy(world+FZERO_TRAFFIC_PALETTE,native+0x500,0x200);
  FzeroTrafficReset(t);return true;
}
static unsigned separation(unsigned a,unsigned b,unsigned period) {
  unsigned d=(a+period-b)&(period-1);return d>period/2?period-d:d;
}
static bool near(const FzeroActor *a,const FzeroActor *b,unsigned radius) {
  unsigned dx=separation(a->x,b->x,8192),dy=separation(a->y,b->y,4096);
  return dx*dx+dy*dy<radius*radius;
}
static bool in_view(const FzeroActor *a,const FzeroActor *human,FzeroActorTables tables) {
  FzeroActorProjection p=FzeroActorProject(human,a,tables);
  /* Fixed maximum-width frustum plus sprite padding, independent of either
   * host's viewport/settings. Do not visibly spawn/recycle in a wide view. */
  return p.projectable && p.x>=-320 && p.x<=576;
}
static bool spawn(Bus *b,unsigned id,const FzeroActor humans[2]) {
  FzeroTraffic *t=b->traffic;uint8_t *p=t->ram;unsigned slot=2+id*2,count=(unsigned)p[0xad]+1;
  const FzeroActor *anchor=&humans[id&1];
  for(unsigned delta=2+id;delta<count && delta<32;++delta) {
    p[0x1130+slot]=(uint8_t)(id%3);p[0xb01+slot]=8;
    unsigned gate=(anchor->checkpoint+delta)%count;
    if(!call(b,0xe309,slot,gate))return false;
    FzeroActor candidate={0};candidate.x=(uint16_t)word(p,0x1080+slot);candidate.y=(uint16_t)word(p,0x108e +slot);
    if(candidate.x>=8192 || candidate.y>=4096 || near(&candidate,&humans[0],680) || near(&candidate,&humans[1],680) ||
        in_view(&candidate,&humans[0],b->tables) || in_view(&candidate,&humans[1],b->tables))continue;
    bool occupied=false;
    for(unsigned j=0;j<FZERO_TRAFFIC_COUNT;++j)if(t->present&(1u<<j)) {
      FzeroActor other=pose(t,j);if(near(&candidate,&other,80))occupied=true;
    }
    if(occupied)continue;
    put(p,0xb70+slot,candidate.x);put(p,0xb90+slot,candidate.y);
    put(p,0xb80+slot,0);put(p,0xba0+slot,0);put(p,0xbc0+slot,0);put(p,0xbb0+slot,0);
    put(p,0xb20+slot,0x500);put(p,0xd60+slot,0);put(p,0xc20+slot,0);put(p,0xc10+slot,0);put(p,0xd20+slot,0);
    unsigned facing=(unsigned)p[0x109d+slot]<<8;if(facing>=0xc000)continue;
    put(p,0xbd0+slot,facing);put(p,0xbe0+slot,facing);
    p[0xb00+slot]=0xc8;p[0xb10+slot]=0x80;p[0xd71+slot]=0x94;p[0xd00+slot]=(uint8_t)gate;
    t->present|=(uint8_t)(1u<<id);t->contacts[0][id]=t->contacts[1][id]=0;
    return true;
  }
  return !b->invalid;
}
bool FzeroTrafficTick(FzeroTraffic *t,uint8_t world[0x20000],const FzeroActor humans[2],uint64_t tick,FzeroActorTables tables) {
  if(!t->enabled)return true;
  Bus b={.traffic=t,.world=world,.tables=tables};uint8_t *p=t->ram;
  /* A deterministic reference for native speed tables, never a camera. Both
   * humans participate equally in spawn distance/visibility decisions. */
  put(p,0xb70,humans[0].x);put(p,0xb90,humans[0].y);put(p,0xb20,humans[0].speed);
  put(p,0xbd0,humans[0].facing);put(p,0xbe0,humans[0].momentum);p[0xd00]=humans[0].checkpoint;
  p[0x51]=(uint8_t)tick;p[0xb00]=0;p[0xc3]=0;
  for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i) {
    unsigned slot=2+i*2;
    /* A persistent shared field, independent of either player's camera or
     * distance. Replace only a car removed by native physics. Repeatedly
     * recreating distant cars at the same checkpoint would freeze them. */
    if((t->present&(1u<<i)) && !(p[0xb00+slot]&128)) {
      t->present&=(uint8_t)~(1u<<i);p[0xb00+slot]=0;
    }
    if(!(t->present&(1u<<i)) && !spawn(&b,i,humans))return false;
    if(!(t->present&(1u<<i)))continue;
    if(!call(&b,0xe309,slot,p[0xd00+slot]) || !call(&b,0xe5cf,slot,0) || !call(&b,0xde57,slot,0))return false;
  }
  return call(&b,0x9144,0,0);
}
void FzeroTrafficContact(FzeroTraffic *t,unsigned player,FzeroActor *human,FzeroActorTables tables) {
  if(!t || !human || player>=2 || !tables.read)return;
  for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i) {
    if(!t->enabled || !(t->present&(1u<<i)) || !(t->ram[0xb02+i*2]&128)) {t->contacts[player][i]=0;continue;}
    FzeroActor body=pose(t,i);
    FzeroActorResolveContact(human,&body,tables,&t->contacts[player][i]);motion(t,i,&body);
  }
}
bool FzeroTrafficValid(const FzeroTraffic *t,const uint8_t world[0x20000]) {
  if(t->enabled>1 || (t->present>>FZERO_TRAFFIC_COUNT) || (!t->enabled && t->present))return false;
  if(t->enabled && (t->ram[0xad]!=world[0xad] || t->seed[0xad]!=world[0xad]))return false;
  if(t->ram[0xb00] || t->ram[0xb0a])return false;
  for(unsigned slot=0;slot<12;slot+=2)if(t->seed[0xb00+slot])return false;
  for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i) {
    if(t->contacts[0][i]>2 || t->contacts[1][i]>2)return false;
    if(t->present&(1u<<i)) {
      FzeroActor a=pose(t,i);
      if(a.x>=8192 || a.y>=4096 || a.facing>=0xc000 || a.momentum>=0xc000 || a.checkpoint>world[0xad] || a.branch>2)return false;
    }
  }
  return true;
}
