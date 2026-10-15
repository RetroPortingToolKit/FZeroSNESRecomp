/* Opt-in, headless-only stock observation. Compute expected movement without
 * writing guest memory, then compare at the real finish-line call boundary.
 * It neither replaces stock execution nor writes expected outcomes into RAM. */
#include "fzero_actor.h"
#include "fzero_runtime.h"
#include "fzero_gameplay.h"
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
static bool enabled, pending;
static unsigned comparisons, mismatches;
static uint16_t word(unsigned address) { return g_ram[address]|(uint16_t)g_ram[address+1]<<8; }
static uint8_t stock_table(void *context,uint32_t address) {
  /* Read the immutable table directly: cpu_read8 would change open bus. */
  const uint8_t *rom=context;
  return rom[(address>>16)*0x8000+(address&0x7fff)];
}
static void before_movement(CpuState *cpu,uint32_t pc) {
  (void)pc;
  if (cpu->X || g_ram[0x52]>=4 || g_ram[0x54]!=2 || g_ram[0x55]!=3 ||
      g_ram[0x5b] || word(0xf50) || g_ram[0xdc8]>=20 ||
      FzeroStateModeCurrent()!=kFzeroStateModeStock || FzeroGameplayActive()) return;
  memset(&expected,0,sizeof(expected));
  for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
    const LiveField *f=&fields[i];
    uint8_t *p=(uint8_t *)&expected+f->offset;
    if (f->size==1) *p=g_ram[f->address];
    else { uint16_t value=word(f->address); memcpy(p,&value,2); }
  }
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
static void after_movement(CpuState *cpu,uint32_t pc) {
  (void)pc;
  if (cpu->X || !pending) return;
  pending=false; ++comparisons;
  for (unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
    const LiveField *f=&fields[i];
    unsigned actual=f->size==1?g_ram[f->address]:word(f->address),value;
    const uint8_t *p=(const uint8_t *)&expected+f->offset;
    if (f->size==1) value=*p;
    else { uint16_t v;memcpy(&v,p,2);value=v; }
    if (actual!=value) {
      if (mismatches<8) fprintf(stderr,"actor-live: tick=%u field=%s stock=%04x adapter=%04x\n",comparisons,f->name,actual,value);
      ++mismatches;
    }
  }
}
void FzeroActorLiveInstall(void) {
  enabled=getenv("FZERO_TEST_ACTOR_PARITY")!=NULL;
  if (!enabled) return;
  if (!interp_bridge_add_pre_opcode_hook(0x00916b,before_movement) ||
      !interp_bridge_add_pre_opcode_hook(0x00919b,after_movement)) {
    fputs("actor-live: unable to install observation hooks\n",stderr);exit(12);
  }
}
int FzeroActorLiveFinish(void) {
  if (!enabled) return 1;
  fprintf(stderr,"actor-live: comparisons=%u mismatches=%u\n",comparisons,mismatches);
  return comparisons>=100 && !mismatches;
}
