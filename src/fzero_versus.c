#include "fzero_versus.h"
#include "fzero_machine.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

struct FzeroVersus {
  FzeroVersusSnapshot state;
  FzeroActor spawns[FZERO_MAX_PLAYERS];
  uint8_t world[0x20000], initial_world[0x20000];
  FzeroActorTables tables;
  FzeroTraffic traffic;
};
static bool config_valid(FzeroVersusConfig c) {
  unsigned n=FzeroVersusPlayers(c);
  if(c.course==UINT16_MAX || n<2 || n>FZERO_MAX_PLAYERS)return false;
  for(unsigned i=0;i<n;++i)if(c.machine[i]>=FZERO_MACHINE_COUNT)return false;
  return true;
}
static bool same_config(FzeroVersusConfig a,FzeroVersusConfig b) {
  unsigned n=FzeroVersusPlayers(a);
  return a.course==b.course && n==FzeroVersusPlayers(b) && !memcmp(a.machine,b.machine,n);
}
static void clear_inputs(FzeroVersus *s) {
  for (unsigned i=0;i<s->state.config.player_count;++i) s->state.racers[i].actor.held=s->state.racers[i].actor.pressed=s->state.racers[i].actor.released=0;
}
static void reset(FzeroVersus *s) {
  FzeroVersusConfig config=s->state.config;
  memset(&s->state,0,sizeof(s->state));s->state.config=config;s->state.countdown=FZERO_VERSUS_COUNTDOWN;
  memcpy(s->world,s->initial_world,sizeof(s->world));
  for (unsigned i=0;i<s->state.config.player_count;++i) s->state.racers[i].actor=s->spawns[i];
  FzeroTrafficReset(&s->traffic);FzeroTrafficDescribe(&s->traffic,s->state.traffic);
}
FzeroVersus *FzeroVersusCreate(FzeroVersusConfig c,const uint8_t world[0x20000],
                              const FzeroActor spawns[],FzeroActorTables tables) {
  if (!config_valid(c) || !world || !spawns || !tables.read || tables.cgp_originals>15)return NULL;
  c.player_count=(uint8_t)FzeroVersusPlayers(c);
  for(unsigned i=0;i<c.player_count;++i) {
    if(!FzeroMachineAvailable(tables,c.machine[i]) || spawns[i].x>8191 || spawns[i].y>4095 ||
        spawns[i].facing>=0xc000 || spawns[i].checkpoint>world[0xad])return NULL;
    for(unsigned j=0;j<i;++j)if(spawns[i].x==spawns[j].x && spawns[i].y==spawns[j].y)return NULL;
  }
  FzeroVersus *s=calloc(1,sizeof(*s));if (!s) return NULL;
  s->state.config=c;s->tables=tables;memcpy(s->world,world,sizeof(s->world));memcpy(s->initial_world,world,sizeof(s->initial_world));
  for (unsigned i=0;i<s->state.config.player_count;++i) {
    FzeroActor *a=&s->spawns[i];
    /* Copy the named spawn pose only. CPU control/health/history cannot leak
     * into a human selected for the second slot. */
    a->x=spawns[i].x;a->y=spawns[i].y;a->facing=spawns[i].facing;a->momentum=a->facing;
    a->checkpoint=spawns[i].checkpoint;a->branch=spawns[i].branch;
    a->machine=c.machine[i];a->health=0x800;
    FzeroMachineReader reader;FzeroActorTables t=FzeroMachineTables(&reader,tables,a->machine);
    unsigned slot=FzeroMachineSlot(a->machine);
    a->acceleration_index=t.read(t.context,0xfa79+slot);
    a->turning_index=t.read(t.context,0xfa7d+slot);
    FzeroActorCheckpointInitialize(a,s->world);
  }
  reset(s);return s;
}
void FzeroVersusDestroy(FzeroVersus *s) { free(s); }
bool FzeroVersusEnableTraffic(FzeroVersus *s,const uint8_t native[0x20000]) {
  if(!s || s->state.phase!=FZERO_VS_READY || s->state.tick ||
      !FzeroTrafficInit(&s->traffic,s->world,native,s->tables))return false;
  memcpy(s->initial_world,s->world,sizeof(s->world));reset(s);return true;
}
const FzeroVersusSnapshot *FzeroVersusCompleted(const FzeroVersus *s) { return s?&s->state:NULL; }
const uint8_t *FzeroVersusWorld(const FzeroVersus *s) { return s?s->world:NULL; }
void FzeroVersusReady(FzeroVersus *s,unsigned player,bool ready) {
  if (!s || player>=s->state.config.player_count || s->state.phase!=FZERO_VS_READY) return;
  if (ready) s->state.ready|=(uint8_t)(1u<<player);else s->state.ready&=(uint8_t)~(1u<<player);
  if (s->state.ready==FzeroVersusReadyMask(s->state.config)) { clear_inputs(s);s->state.phase=FZERO_VS_COUNTDOWN; }
}
void FzeroVersusPause(FzeroVersus *s,unsigned reason) {
  /* Retain the interface for older host callers, without a race-stopping
   * side effect. Neither focus/device loss nor Start may freeze multiplayer. */
  (void)s;(void)reason;
}
void FzeroVersusResume(FzeroVersus *s) { if (s) { clear_inputs(s);s->state.pause=0; } }
bool FzeroVersusRaceComplete(const FzeroVersusRacer *racers,unsigned count) {
  if(!racers || !count)return false;
  unsigned active=0,remaining=0;
  for(unsigned i=0;i<count;++i) {
    active+=racers[i].status==FZERO_VS_ACTIVE;
    remaining+=racers[i].status!=FZERO_VS_DNF;
  }
  return !active || remaining<=1;
}
static bool ahead(const FzeroVersusSnapshot *v,unsigned i,unsigned j) {
  const FzeroVersusRacer *a=&v->racers[i],*b=&v->racers[j];
  if(a->status==FZERO_VS_FINISHED || b->status==FZERO_VS_FINISHED) {
    if(a->status!=b->status)return a->status==FZERO_VS_FINISHED;
    return a->finish_tick<b->finish_tick;
  }
  if(a->status==FZERO_VS_DNF || b->status==FZERO_VS_DNF) {
    if(a->status!=b->status)return b->status==FZERO_VS_DNF;
    return i<j;
  }
  unsigned own=(unsigned)a->actor.lap*256+a->actor.checkpoint;
  unsigned other=(unsigned)b->actor.lap*256+b->actor.checkpoint;
  return own>other || (own==other && i<j);
}
unsigned FzeroVersusRank(const FzeroVersusSnapshot *v,unsigned player) {
  if(!v || player>=FzeroVersusPlayers(v->config))return 0;
  unsigned rank=1;
  for(unsigned i=0;i<FzeroVersusPlayers(v->config);++i)if(i!=player && ahead(v,i,player))++rank;
  return rank;
}
FzeroRearWarning FzeroVersusRearWarning(const FzeroVersusSnapshot *v,
    unsigned player,FzeroActorTables tables) {
  FzeroRearWarning warning={0};
  if(!v || !tables.read || v->phase!=FZERO_VS_RACING ||
      player>=v->config.player_count || v->config.player_count>FZERO_MAX_PLAYERS ||
      v->racers[player].status!=FZERO_VS_ACTIVE)return warning;
  /* $D952's distance metric and $EA1F's range, initialized from $02:FBC2.
   * VS has several independent cameras: select the closest active human
   * physically behind this racer, rather than the stock single-player rank
   * list. Positive body-forward is behind in the native projection. */
  unsigned nearest=tables.read(tables.context,0x02fbc2)|
      (unsigned)tables.read(tables.context,0x02fbc3)<<8;
  for(unsigned i=0;i<v->config.player_count;++i) {
    if(i==player || v->racers[i].status!=FZERO_VS_ACTIVE)continue;
    FzeroActorProjection p=FzeroActorProject(&v->racers[player].actor,&v->racers[i].actor,tables);
    if(p.forward<=0)continue;
    unsigned dx=(unsigned)(p.right<0?-p.right:p.right)/8;
    unsigned dy=(unsigned)p.forward/8,distance=dx*dx+dy*dy;
    if(distance>=nearest)continue;
    nearest=distance;warning.visible=true;warning.distance=distance;
    /* $EA27's lateral placement, centered and clamped inside the stock pane.
     * Use the completed pose directly so load/rollback and repeated renders
     * do not need a separate, frame-rate-dependent position history. */
    warning.x=128+p.right*4;
    if(warning.x<16)warning.x=16;
    if(warning.x>240)warning.x=240;
  }
  return warning;
}
static unsigned result_winner(const FzeroVersusSnapshot *v) {
  unsigned winner=0,count=0;
  for(unsigned i=0;i<v->config.player_count;++i) {
    if(v->racers[i].status==FZERO_VS_DNF || FzeroVersusRank(v,i)!=1)continue;
    winner=i+1;++count;
  }
  return count>1?FZERO_VS_TIE:winner;
}
static void results(FzeroVersus *s) {
  if(!FzeroVersusRaceComplete(s->state.racers,s->state.config.player_count))return;
  for(unsigned i=0;i<s->state.config.player_count;++i) {
    FzeroVersusRacer *r=&s->state.racers[i];
    if(r->status==FZERO_VS_ACTIVE) {r->status=FZERO_VS_SURVIVED;r->finish_tick=s->state.race_tick;}
  }
  s->state.phase=FZERO_VS_RESULTS;
  s->state.winner=result_winner(&s->state);
  clear_inputs(s);
}
void FzeroVersusEndRace(FzeroVersus *s) {
  if (!s || s->state.phase==FZERO_VS_READY || s->state.phase==FZERO_VS_RESULTS) return;
  for (unsigned i=0;i<s->state.config.player_count;++i) if (s->state.racers[i].status==FZERO_VS_ACTIVE) {
    s->state.racers[i].status=FZERO_VS_DNF;s->state.racers[i].finish_tick=s->state.race_tick;
  }
  s->state.contact=0;results(s);
}
bool FzeroVersusRematch(FzeroVersus *s) {
  if (!s || s->state.phase!=FZERO_VS_RESULTS) return false;
  reset(s);return true;
}
bool FzeroVersusRestart(FzeroVersus *s) {
  if(!s)return false;
  reset(s);return true;
}
static void progress(FzeroVersus *s,unsigned player,int gates) {
  FzeroVersusRacer *r=&s->state.racers[player];FzeroActor *a=&r->actor;
  r->gates+=gates;
  int32_t limit=((int32_t)s->world[0xad]+1)*6;
  if (r->gates>limit) r->gates=limit;
  if (r->gates< -limit) r->gates= -limit;
  /* A signed gate balance makes backwards travel repay its progress. The
   * stock finish strip alone cannot award a lap or repeatedly farm boosts. */
  int crossing=FzeroActorCrossFinish(a,s->world);
  if (crossing<0 || !a->finish_side) r->crossing_tick=0;
  if (crossing==1) {
    if (!r->started) { r->started=1;r->gates=0;a->lap=1;return; }
    if (r->gates>=(int)s->world[0xad]) { r->crossing_tick=s->state.race_tick;r->crossing_pose=*a; }
  }
  /* The final checkpoint is evaluated before movement, while the finish
   * strip is evaluated afterwards. Allow that last gate to confirm on the
   * following tick, preserving the actual crossing time and terminal pose. */
  if (!r->crossing_tick || r->gates<(int)s->world[0xad]+1) return;
  uint64_t crossed=r->crossing_tick;r->crossing_tick=0;r->gates-=s->world[0xad]+1;
  r->lap_ticks[r->completed_laps++]=crossed-r->lap_start;
  r->lap_start=crossed;a->lap=(uint8_t)(r->completed_laps+1);
  r->lap_boost=(uint8_t)(r->completed_laps<FZERO_VERSUS_LAPS && a->boosts<3);
  if (r->completed_laps==FZERO_VERSUS_LAPS) {
    r->status=FZERO_VS_FINISHED;r->finish_tick=crossed;*a=r->crossing_pose;
    a->lap=FZERO_VERSUS_LAPS+1;a->held=a->pressed=a->released=0;
  } else a->boost_award=1;
}
bool FzeroVersusTick(FzeroVersus *s,const uint16_t held[]) {
  if (!s || !held || s->state.pause || s->state.phase==FZERO_VS_READY || s->state.phase==FZERO_VS_RESULTS) return false;
  for (unsigned i=0;i<s->state.config.player_count;++i) FzeroActorLatch(&s->state.racers[i].actor,held[i]);
  ++s->state.tick;
  if (s->state.phase==FZERO_VS_COUNTDOWN) {
    if(s->state.countdown>FZERO_VERSUS_REV_COUNTDOWN) {--s->state.countdown;return true;}
    for (unsigned i=0;i<s->state.config.player_count;++i) {
      FzeroActor *a=&s->state.racers[i].actor;FzeroActorPreRace(a,s->tables);FzeroActorPreRace(a,s->tables);
    }
    if (!--s->state.countdown) {
      s->state.phase=FZERO_VS_RACING;
      for (unsigned i=0;i<s->state.config.player_count;++i) FzeroActorLaunch(&s->state.racers[i].actor);
    }
    return true;
  }
  ++s->state.race_tick;
  int gates[FZERO_MAX_PLAYERS]={0};
  memset(s->state.mines,0,sizeof(s->state.mines));
  for (unsigned i=0;i<s->state.config.player_count;++i) {
    FzeroVersusRacer *r=&s->state.racers[i];
    if (r->status==FZERO_VS_ACTIVE) {
      gates[i]=FzeroActorTick(&r->actor,s->world,s->tables,(uint8_t)s->state.tick,&s->state.mines[i]);
      if (r->actor.race_state&0x40) { r->status=FZERO_VS_DNF;r->finish_tick=s->state.race_tick; }
    }
    else r->actor.held=r->actor.pressed=r->actor.released=0;
  }
  s->state.contact=0;
  unsigned pair=0;
  for(unsigned i=0;i<s->state.config.player_count;++i)for(unsigned j=i+1;j<s->state.config.player_count;++j,++pair) {
    uint8_t *latch=&s->state.contacts[pair];
    if(s->state.racers[i].status==FZERO_VS_ACTIVE && s->state.racers[j].status==FZERO_VS_ACTIVE)
      FzeroActorResolveContact(&s->state.racers[i].actor,&s->state.racers[j].actor,s->tables,latch);
    else *latch=0;
    if(*latch>s->state.contact)s->state.contact=*latch;
  }
  FzeroActor references[FZERO_MAX_PLAYERS];
  for(unsigned i=0;i<s->state.config.player_count;++i)references[i]=s->state.racers[i].actor;
  if(!FzeroTrafficTickPlayers(&s->traffic,s->world,references,s->state.config.player_count,s->state.tick,s->tables))return false;
  for(unsigned i=0;i<s->state.config.player_count;++i)if(s->state.racers[i].status==FZERO_VS_ACTIVE)
    FzeroTrafficContact(&s->traffic,i,&s->state.racers[i].actor,s->tables);
  FzeroTrafficDescribe(&s->traffic,s->state.traffic);
  for (unsigned i=0;i<s->state.config.player_count;++i) FzeroActorCommitMine(s->world,&s->state.mines[i]);
  for (unsigned i=0;i<s->state.config.player_count;++i) {
    FzeroVersusRacer *r=&s->state.racers[i];
    if (r->status==FZERO_VS_ACTIVE && (r->actor.race_state&0x40)) {
      r->status=FZERO_VS_DNF;r->finish_tick=s->state.race_tick;
    }
    if (r->status==FZERO_VS_ACTIVE) progress(s,i,gates[i]);
    else r->actor.held=r->actor.pressed=r->actor.released=0;
  }
  results(s);return true;
}

/* Wire format uses the field manifest rather than dumping C structs. This
 * avoids padding, endian and bool-layout dependencies in rewind/replay. */
typedef struct ActorField { size_t offset,size; } ActorField;
#define FIELD(address,name) {offsetof(FzeroActor,name),sizeof(((FzeroActor *)0)->name)}
static const ActorField actor_fields[]={
#include "fzero_actor_fields.inc"
};
#undef FIELD
typedef struct Stream { uint8_t *output;const uint8_t *input;size_t position;uint64_t hash;bool invalid; } Stream;
static uint64_t hash_bytes(uint64_t hash,const uint8_t *bytes,size_t size) {
  for (size_t i=0;i<size;++i) { hash^=bytes[i];hash*=UINT64_C(1099511628211); }
  return hash;
}
static void block(Stream *x,const uint8_t *source,uint8_t *target,size_t size) {
  if (x->input && target) { memcpy(target,x->input+x->position,size); }
  else if (source) {
    if (x->output) memcpy(x->output+x->position,source,size);
    x->hash=hash_bytes(x->hash,source,size);
  }
  x->position+=size;
}
static uint64_t scalar(Stream *x,uint64_t value,unsigned size) {
  uint8_t bytes[8]={0};
  for (unsigned i=0;i<size;++i) bytes[i]=(uint8_t)(value>>(i*8));
  block(x,bytes,bytes,size);
  if (x->input) { value=0;for (unsigned i=0;i<size;++i) value|=(uint64_t)bytes[i]<<(i*8); }
  return value;
}
static void actor_codec(Stream *x,const FzeroActor *source,FzeroActor *target) {
  for (unsigned i=0;i<sizeof(actor_fields)/sizeof(*actor_fields);++i) {
    ActorField f=actor_fields[i];uint16_t value=0;
    if (source) {
      const uint8_t *p=(const uint8_t *)source+f.offset;
      if (f.size==1) value=*p;else memcpy(&value,p,2);
    }
    value=(uint16_t)scalar(x,value,(unsigned)f.size);
    if (target) {
      uint8_t *p=(uint8_t *)target+f.offset;
      if (f.size==1) *p=(uint8_t)value;else memcpy(p,&value,2);
    }
  }
  unsigned flash=(unsigned)scalar(x,source?source->body_flash:0,1);
  if(target) {if(flash>1)x->invalid=true;target->body_flash=(uint8_t)flash;}
  unsigned pose=(unsigned)scalar(x,source?source->pit_pose:0,1);
  if(target) {if(pose>0x22)x->invalid=true;target->pit_pose=(uint8_t)pose;}
  unsigned beam=(unsigned)scalar(x,source?source->pit_recovery:0,1);
  if(target) {if(beam>1)x->invalid=true;target->pit_recovery=(uint8_t)beam;}
}
static size_t codec(Stream *x,const FzeroVersus *source,FzeroVersus *target) {
#define S(field,n) do { uint64_t v=scalar(x,source?source->field:0,n);if(target)target->field=v; } while(0)
  S(state.config.course,2);S(state.config.player_count,1);
  for(unsigned i=0;i<FZERO_MAX_PLAYERS;++i) {S(state.config.machine[i],1);}
  S(state.tick,8);S(state.race_tick,8);S(state.countdown,4);
  S(state.phase,1);S(state.pause,1);S(state.ready,1);S(state.contact,1);S(state.winner,1);
  for(unsigned i=0;i<FZERO_CONTACT_PAIRS;++i) {S(state.contacts[i],1);}
  for (unsigned i=0;i<FZERO_MAX_PLAYERS;++i) {
    actor_codec(x,source?&source->spawns[i]:NULL,target?&target->spawns[i]:NULL);
    actor_codec(x,source?&source->state.racers[i].actor:NULL,target?&target->state.racers[i].actor:NULL);
    actor_codec(x,source?&source->state.racers[i].crossing_pose:NULL,target?&target->state.racers[i].crossing_pose:NULL);
    S(state.racers[i].finish_tick,8);S(state.racers[i].lap_start,8);
    S(state.racers[i].crossing_tick,8);
    for (unsigned lap=0;lap<FZERO_VERSUS_LAPS;++lap) { S(state.racers[i].lap_ticks[lap],8); }
    /* Signed values are converted explicitly, without implementation-defined
     * uint32_t-to-int32_t conversion for a backwards gate balance. */
    uint32_t bits=(uint32_t)(source?source->state.racers[i].gates:0);
    bits=(uint32_t)scalar(x,bits,4);
    if (target) target->state.racers[i].gates=bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);
    S(state.racers[i].status,1);S(state.racers[i].completed_laps,1);S(state.racers[i].started,1);
    S(state.racers[i].lap_boost,1);
    uint64_t hit=scalar(x,source?source->state.mines[i].hit:0,1);
    if (target) { if(hit>1)x->invalid=true;target->state.mines[i].hit=hit!=0; }
    S(state.mines[i].x,2);S(state.mines[i].y,2);S(state.mines[i].surface_id,1);
  }
#undef S
  block(x,source?source->traffic.ram:NULL,target?target->traffic.ram:NULL,0x2000);
  block(x,source?source->traffic.seed:NULL,target?target->traffic.seed:NULL,0x2000);
#define T(field) do {uint64_t v=scalar(x,source?source->traffic.field:0,1);if(target)target->traffic.field=(uint8_t)v;} while(0)
  T(enabled);T(present);
  for(unsigned p=0;p<FZERO_MAX_PLAYERS;++p)for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i)T(contacts[p][i]);
#undef T
  block(x,source?source->world:NULL,target?target->world:NULL,0x20000);
  block(x,source?source->initial_world:NULL,target?target->initial_world:NULL,0x20000);
  return x->position;
}
size_t FzeroVersusSaveSize(void) { Stream x={0};return 32+codec(&x,NULL,NULL); }
static void put32(uint8_t *p,uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(i*8)); }
static uint32_t get32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static const uint8_t magic[8]={'F','Z','V','S','0','1','0',0};
static uint64_t integrity(const uint8_t *data,size_t size) {
  uint64_t hash=hash_bytes(UINT64_C(14695981039346656037),data,24);
  return hash_bytes(hash,data+32,size-32);
}
bool FzeroVersusSave(const FzeroVersus *s,void *output,size_t size) {
  if (!s || !output || size!=FzeroVersusSaveSize()) return false;
  uint8_t *p=output;memcpy(p,magic,8);put32(p+8,1);put32(p+12,FZERO_VERSUS_RULES);put32(p+16,(uint32_t)size);put32(p+20,s->tables.cgp_originals);
  Stream x={.output=p+32};codec(&x,s,NULL);
  uint64_t hash=integrity(p,size);for (unsigned i=0;i<8;++i) p[24+i]=(uint8_t)(hash>>(i*8));
  return true;
}
static bool actor_valid(const FzeroActor *a,const FzeroVersus *s,unsigned player) {
  FzeroMachineReader reader;FzeroActorTables t=FzeroMachineTables(&reader,s->tables,a->machine);
  unsigned slot=FzeroMachineSlot(a->machine);
  return a->machine==s->state.config.machine[player] && a->x<8192 && a->y<4096 &&
         a->facing<0xc000 && a->momentum<0xc000 && a->checkpoint<=s->world[0xad] &&
         a->acceleration_index==t.read(t.context,0xfa79+slot) &&
         a->turning_index==t.read(t.context,0xfa7d+slot);
}
static bool valid(const FzeroVersus *s) {
  const FzeroVersusSnapshot *v=&s->state;
  if(!FzeroTrafficValid(&s->traffic,s->world))return false;
  if (v->config.player_count<2 || v->config.player_count>4 || !config_valid(v->config) || !FzeroMachineAvailable(s->tables,v->config.machine[0]) ||
      !FzeroMachineAvailable(s->tables,v->config.machine[1]) || v->phase>FZERO_VS_RESULTS || v->pause || v->ready>FzeroVersusReadyMask(v->config) ||
      v->contact>2 || v->winner>FZERO_VS_TIE || v->countdown>FZERO_VERSUS_COUNTDOWN || v->race_tick>v->tick) return false;
  if (v->phase==FZERO_VS_READY && (v->ready==FzeroVersusReadyMask(v->config) || v->countdown!=FZERO_VERSUS_COUNTDOWN || v->tick)) return false;
  if (v->phase==FZERO_VS_COUNTDOWN && (v->ready!=FzeroVersusReadyMask(v->config) || !v->countdown || v->race_tick)) return false;
  if (v->phase==FZERO_VS_RACING && (v->ready!=FzeroVersusReadyMask(v->config) || v->countdown)) return false;
  if (v->phase!=FZERO_VS_RESULTS && v->winner!=FZERO_VS_NO_WINNER) return false;
  for (unsigned i=0;i<s->state.config.player_count;++i) {
    if(!FzeroMachineAvailable(s->tables,v->config.machine[i]))return false;
    const FzeroVersusRacer *r=&v->racers[i];
    int32_t limit=((int32_t)s->world[0xad]+1)*6;
    if (!actor_valid(&s->spawns[i],s,i) || !actor_valid(&r->actor,s,i) || r->status>FZERO_VS_SURVIVED ||
        r->completed_laps>FZERO_VERSUS_LAPS || r->started>1 || r->lap_boost>1 ||
        (r->lap_boost && (!r->completed_laps || r->completed_laps==FZERO_VERSUS_LAPS)) ||
        r->gates< -limit || r->gates>limit ||
        r->lap_start>v->race_tick || r->finish_tick>v->race_tick ||
        r->crossing_tick>v->race_tick ||
        (r->crossing_tick && (r->crossing_tick<r->lap_start || !r->started ||
          !actor_valid(&r->crossing_pose,s,i))) ||
        (r->status==FZERO_VS_FINISHED && (!r->started || r->completed_laps!=FZERO_VERSUS_LAPS || !r->finish_tick)) ||
        (r->status==FZERO_VS_ACTIVE && r->finish_tick) ||
        (r->status==FZERO_VS_SURVIVED && (v->phase!=FZERO_VS_RESULTS ||
          (r->actor.race_state&0x40))) ||
        (r->status!=FZERO_VS_FINISHED && r->completed_laps==FZERO_VERSUS_LAPS) ||
        (v->mines[i].hit && (v->mines[i].x>=8192 || v->mines[i].y>=4096 ||
          v->mines[i].surface_id<0xc8 || v->mines[i].surface_id>=0xcc))) return false;
    uint64_t total=0;
    for (unsigned lap=0;lap<FZERO_VERSUS_LAPS;++lap) {
      if (r->lap_ticks[lap]>v->race_tick-total || (lap>=r->completed_laps && r->lap_ticks[lap])) return false;
      total+=r->lap_ticks[lap];
    }
    if (r->status!=FZERO_VS_ACTIVE && (r->actor.held || r->actor.pressed || r->actor.released)) return false;
  }
  for(unsigned i=0;i<FZERO_CONTACT_PAIRS;++i)if(v->contacts[i]>2)return false;
  if (v->phase==FZERO_VS_RESULTS) {
    for(unsigned i=0;i<v->config.player_count;++i)if(v->racers[i].status==FZERO_VS_ACTIVE)return false;
    if (v->winner!=result_winner(v)) return false;
  }
  return true;
}
FzeroVersus *FzeroVersusDecode(const void *input,size_t size,FzeroActorTables tables,const FzeroVersusConfig *expected) {
  if (!tables.read || !input || size!=FzeroVersusSaveSize()) return NULL;
  const uint8_t *p=input;uint64_t stored=0;
  for (unsigned i=0;i<8;++i) stored|=(uint64_t)p[24+i]<<(i*8);
  if (memcmp(p,magic,8) || get32(p+8)!=1 || get32(p+12)!=FZERO_VERSUS_RULES || get32(p+16)!=size ||
      get32(p+20)!=tables.cgp_originals || tables.cgp_originals>15 || stored!=integrity(p,size)) return NULL;
  FzeroVersus *candidate=calloc(1,sizeof(*candidate));if (!candidate) return NULL;
  candidate->tables=tables;Stream x={.input=p+32};codec(&x,NULL,candidate);
  FzeroTrafficDescribe(&candidate->traffic,candidate->state.traffic);
  bool ok=!x.invalid && valid(candidate) && (!expected || same_config(candidate->state.config,*expected));
  if (!ok) {free(candidate);return NULL;}
  return candidate;
}
bool FzeroVersusRestore(FzeroVersus *s,const void *input,size_t size,const FzeroVersusConfig *expected) {
  if (!s) return false;
  FzeroVersus *candidate=FzeroVersusDecode(input,size,s->tables,expected);
  if (!candidate) return false;
  *s=*candidate;free(candidate);return true;
}
uint64_t FzeroVersusHash(const FzeroVersus *s) {
  if (!s) return 0;
  Stream x={.hash=UINT64_C(14695981039346656037)};
  scalar(&x,FZERO_VERSUS_RULES,4);scalar(&x,s->tables.cgp_originals,1);codec(&x,s,NULL);return x.hash;
}
