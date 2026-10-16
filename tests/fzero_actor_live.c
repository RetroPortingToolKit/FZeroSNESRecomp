/* Opt-in, headless-only stock observation. Compute expected movement without
 * writing guest memory, then compare at the real finish-line call boundary.
 * It neither replaces stock execution nor writes expected outcomes into RAM. */
#include "fzero_actor.h"
#include "fzero_runtime.h"
#include "fzero_gameplay.h"
#include "fzero_course_runtime.h"
#include "fzero_deluxe.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/cart.h"
#include "snes/snes.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct LiveField { unsigned address; size_t offset, size; const char *name; } LiveField;
#define FIELD(address,name) {address,offsetof(FzeroActor,name),sizeof(((FzeroActor *)0)->name),#name}
static const LiveField fields[]={
#include "fzero_actor_fields.inc"
};
static FzeroActor expected;
static bool enabled, pending, progress_pending;
static unsigned comparisons, progress_comparisons, mismatches;
static uint16_t word(unsigned address) { return g_ram[address]|(uint16_t)g_ram[address+1]<<8; }
static uint8_t stock_table(void *context,uint32_t address) {
  /* Read the immutable table directly: cpu_read8 would change open bus. */
  const uint8_t *rom=context;
  return rom[(address>>16)*0x8000+(address&0x7fff)];
}
static bool eligible(void) {
  return !(g_ram[0x52]>=4 || g_ram[0x54]!=2 || g_ram[0x55]!=3 ||
      g_ram[0x5b] || word(0xf50) || g_ram[0xdc8]>=20 ||
      (FzeroStateModeCurrent()!=kFzeroStateModeStock &&
       !(getenv("FZERO_TEST_ACTOR_IMPORTED") && !FzeroDeluxeActive() && FzeroTracksCurrentCourse())) || FzeroGameplayActive());
}
static void capture_actor(void) {
  memset(&expected,0,sizeof(expected));
  for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
    const LiveField *f=&fields[i];
    uint8_t *p=(uint8_t *)&expected+f->offset;
    if (f->size==1) *p=g_ram[f->address];
    else { uint16_t value=word(f->address); memcpy(p,&value,2); }
  }
}
static void before_progress(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;
  if (!eligible() || !(g_ram[0xb00]&0x80)) return;
  capture_actor();
  FzeroActorProgressTick(&expected,g_ram,(FzeroActorTables){stock_table,g_snes->cart->rom},g_ram[0x51]);
  progress_pending=true;
}
static void before_movement(CpuState *cpu,uint32_t pc) {
  (void)pc;
  if (cpu->X || !eligible()) return;
  capture_actor();
  FzeroActorScratch s={0};
  s.off_gas=word(0x19);s.over_speed=word(0x1b);s.gravity=word(0x14);
  FzeroActorTables t={stock_table,g_snes->cart->rom};
  FzeroActorSlide(&expected);
  FzeroActorDamageSurface(&expected,t);
  FzeroActorDash(&expected);
  FzeroActorTopSpeed(&expected,&s,t);
  FzeroActorGrip(&expected,&s,t);
  FzeroActorSpinState(&expected,&s);
  /* Match the retail guard for this stock-observation oracle. The versus
   * adapter itself deliberately has no camera-visibility eligibility. */
  if ((g_ram[0xb00]&0xbe)==0x88 && g_ram[0xc60]>=0x40) {
    FzeroActorMineEvent event; FzeroActorMine(&expected,t,&event);
  }
  FzeroActorAltitude(&expected,&s,t,g_ram[0x51]);
  FzeroActorSpeed(&expected,&s,t,expected.race_state);
  FzeroActorApplySpin(&expected,&s);
  FzeroActorTurn(&expected,&s);
  FzeroActorApplyGrip(&expected,&s);
  FzeroActorVelocity(&s,t);
  FzeroActorWind(&expected,&s,t,expected.race_state);
  FzeroActorStoreVelocity(&expected,&s);
  FzeroActorMove(&expected,&s);
  pending=true;
}
static void compare_actor(const char *stage,unsigned tick) {
  for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
    const LiveField *f=&fields[i];
    unsigned actual=f->size==1?g_ram[f->address]:word(f->address),value;
    const uint8_t *p=(const uint8_t *)&expected+f->offset;
    if (f->size==1) value=*p;
    else { uint16_t v;memcpy(&v,p,2);value=v; }
    if (actual!=value) {
      if (mismatches<8) fprintf(stderr,"actor-live: %s tick=%u field=%s stock=%04x adapter=%04x\n",stage,tick,f->name,actual,value);
      ++mismatches;
    }
  }
}
static void after_progress(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;
  if (!progress_pending) return;
  progress_pending=false;compare_actor("progress",++progress_comparisons);
}
static void after_movement(CpuState *cpu,uint32_t pc) {
  (void)pc;
  if (cpu->X || !pending) return;
  pending=false;compare_actor("movement",++comparisons);
}
/* Read-only native call-order evidence for full-frame handling qualification. */
static FILE *schedule_trace;
static void schedule(CpuState *cpu,uint32_t pc) {
  if(!eligible() || ((pc==0x00916b || pc==0x00919b) && cpu->X))return;
  fprintf(schedule_trace,"%llu,%u,%06x,%u,%u,%04x,%02x,%04x,%04x,%04x,%04x,%04x,%04x,%04x,%u,%u,%u\n",
      (unsigned long long)cpu->master_cycles,g_ram[0x51],pc,cpu->X,g_ram[0xc3],word(0x65),g_ram[0xb10],
      word(0xb20),word(0xbd0),word(0xbe0),word(0xc10),word(0xb70),word(0xb90),word(0xbc0),g_ram[0xdd],g_ram[0xde],g_ram[0xd51]);
}
static FzeroActor full_expected;
static bool full_enabled,full_pending,full_chain,full_initialized;
static unsigned full_comparisons,full_mismatches;
static uint8_t full_world[0x20000];
static unsigned full_start_recovery;
static bool full_world_dumped;
static void before_full_tick(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;
  full_pending=false;
  if(!eligible() || g_ram[0xc3] || !(g_ram[0xb00]&0x80)) {full_initialized=false;return;}
  if(!full_chain || !full_initialized) for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
    const LiveField *f=&fields[i];uint8_t *p=(uint8_t *)&full_expected+f->offset;
    if(f->size==1)*p=g_ram[f->address];else {uint16_t v=word(f->address);memcpy(p,&v,2);}
  }
  if(!full_chain || !full_initialized) {
    if(!FzeroActorCaptureWorld(full_world,g_ram)){fputs("full-frame course capture failed\n",stderr);exit(12);}
    FzeroActorSetCourseResources(full_world,FzeroTracksCurrentCourse());
    const char *dump=getenv("FZERO_TEST_ACTOR_WORLD");
    if(dump && !full_world_dumped) {
      FILE *file=fopen(dump,"wb");
      if(!file || fwrite(full_world,1,sizeof(full_world),file)!=sizeof(full_world) || fclose(file))exit(12);
      const CpTrack *track=FzeroTracksRuntimeTrack(FzeroTracksMenuIndex(),g_ram[0x53]);
      fprintf(stderr,"actor-frame: authored course=%s features=%u\n",track?track->id:"stock",full_world[FZERO_WORLD_FEATURES]);
      full_world_dumped=true;
    }
  } else FzeroActorLatch(&full_expected,word(0x65));
  full_initialized=true;
  full_start_recovery=full_expected.recovery_active;
  FzeroActorMineEvent mine={0};FzeroActorTick(&full_expected,full_world,
      (FzeroActorTables){stock_table,g_snes->cart->rom},g_ram[0x51],&mine);
  /* Finish-side tracking is owned by the session after the uncoupled tick.
   * It also selects the native boundary/checkpoint direction next tick. */
  FzeroActorCrossFinish(&full_expected,full_world);
  if(full_chain)FzeroActorCommitMine(full_world,&mine);
  full_pending=true;
}
static void after_full_tick(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;if(!full_pending)return;full_pending=false;++full_comparisons;
  /* Handling, motion, surfaces and animation are observed across a complete
   * naturally executed native tick. Audio/HUD/race-award ownership differs. */
  static const char *names[]={"speed","speed_fraction","x","x_fraction","y","y_fraction","vx","vx_fraction","vy","vy_fraction",
    "height","height_velocity","facing","momentum","turning","spin","no_control","health","action","state","surface","surface_id","ground_flags",
    "surface_flags","surface_extra","recovery_active","slide","instant_grip","jump_timer","turn_stage","turn_acceleration","exhaust","exhaust_color","exhaust_frame","exhaust_timer","exhaust_offset","extra_height_index","extra_height",
    "animation","steering_history","animation_scale","animation_transition","ground_timer","boundary_momentum",
    "electric_flash","track_flash","effect_timer","black_flash",
    "checkpoint","checkpoint_angle","status2","wall_angle","finish_side","branch",
    "next_checkpoint_x","next_checkpoint_y","previous_checkpoint_x","previous_checkpoint_y",
    "next_checkpoint_angle","previous_checkpoint_angle","next_checkpoint_flags","previous_checkpoint_flags"};
  for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
    const LiveField *f=&fields[i];bool check=false;
    for(unsigned n=0;n<sizeof(names)/sizeof(*names);++n)if(!strcmp(f->name,names[n])){check=true;break;}
    if(!check)continue;
    unsigned native=f->size==1?g_ram[f->address]:word(f->address),expected_value;
    const uint8_t *p=(const uint8_t *)&full_expected+f->offset;
    if(f->size==1)expected_value=*p;else{uint16_t v;memcpy(&v,p,2);expected_value=v;}
    if(native!=expected_value) {
      if(full_mismatches<40)fprintf(stderr,"actor-frame: tick=%u nmi=%u field=%s native=%04x versus=%04x held=%04x tile=%02x/%02x resource=%u terrain=%02x pos=%u,%u recovery=%u/%u start=%u\n",
          full_comparisons,g_ram[0x51],f->name,native,expected_value,word(0x65),g_ram[0xcd0],full_expected.surface_id,
          full_world[FZERO_WORLD_RESOURCE],full_world[FZERO_WORLD_TERRAIN+full_expected.surface_id],full_expected.x,full_expected.y,
          g_ram[0xee6],full_expected.recovery_active,full_start_recovery);
      ++full_mismatches;
    }
  }
}
void FzeroActorLiveInstall(void) {
  full_enabled=getenv("FZERO_TEST_ACTOR_FRAME_PARITY")!=NULL;
  full_chain=getenv("FZERO_TEST_ACTOR_FRAME_CHAIN")!=NULL;
  if(full_enabled && (!interp_bridge_add_pre_opcode_hook(0x00a1c0,before_full_tick) ||
      !interp_bridge_add_pre_opcode_hook(0x008dd1,after_full_tick))) {fputs("Cannot install full-frame hooks\n",stderr);exit(12);}

  const char *path=getenv("FZERO_TEST_ACTOR_SCHEDULE");
  if(path) {
    schedule_trace=fopen(path,"wb");if(!schedule_trace){perror(path);exit(12);}
    setvbuf(schedule_trace,NULL,_IOLBF,0);
    fputs("master,nmi,pc,X,race_state,held,action,speed,facing,momentum,turning,x,y,height,DD,DE,state\n",schedule_trace);
    const uint32_t entries[]={0x008e36,0x00dd25,0x00b61b,0x00b94e,0x00b7cb,0x00b881,0x00ba13,0x00b5ec,0x009144,0x00916b,0x00919b,0x00ba70,0x00fa1c};
    for(unsigned i=0;i<sizeof(entries)/sizeof(*entries);++i)
      if(!interp_bridge_add_pre_opcode_hook(entries[i],schedule)){fputs("Cannot install schedule hook\n",stderr);exit(12);}
  }

  enabled=getenv("FZERO_TEST_ACTOR_PARITY")!=NULL;
  if (!enabled) return;
  if (!interp_bridge_add_pre_opcode_hook(0x00dd25,before_progress) ||
      !interp_bridge_add_pre_opcode_hook(0x00dda3,after_progress) ||
      !interp_bridge_add_pre_opcode_hook(0x00916b,before_movement) ||
      !interp_bridge_add_pre_opcode_hook(0x00919b,after_movement)) {
    fputs("actor-live: unable to install observation hooks\n",stderr);exit(12);
  }
}
int FzeroActorLiveFinish(void) {
  if(full_enabled)fprintf(stderr,"actor-frame: mode=%s comparisons=%u mismatches=%u\n",full_chain?"persistent":"per-tick",full_comparisons,full_mismatches);
  bool full_ok=!full_enabled || (full_comparisons>=100 && !full_mismatches);
  if (!enabled) return full_ok;
  fprintf(stderr,"actor-live: comparisons=%u mismatches=%u\n",comparisons,mismatches);
  fprintf(stderr,"actor-live: checkpoint comparisons=%u\n",progress_comparisons);
  return full_ok && comparisons>=100 && progress_comparisons>=100 && !mismatches;
}
