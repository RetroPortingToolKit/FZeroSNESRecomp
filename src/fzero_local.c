#include "fzero_local.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay.h"
#include "fzero_tracks.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/cart.h"
#include "snes/snes.h"
#include <stdio.h>

extern Snes *g_snes;
static bool enabled,setup_started;
static unsigned setup_frame;
static FzeroVersusConfig config;
static FzeroVersus *session;
static FzeroVersusVideo *video;
static unsigned word(unsigned at) {return g_ram[at]|(unsigned)g_ram[at+1]<<8;}
static uint8_t read_table(void *context,uint32_t address) {
  (void)context;
  const uint8_t *p=cart_getRomPtr(g_snes->cart,(uint8_t)(address>>16),(uint16_t)address);
  return p?*p:0;
}
static FzeroActorTables tables(void) {return (FzeroActorTables){read_table,NULL};}
static void course_config(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;FzeroLocalPrepareCourse();
}
bool FzeroLocalEnabled(void) {return enabled;}
FzeroVersus *FzeroLocalSession(void) {return session;}
void FzeroLocalShutdown(void) {
  if(enabled)interp_bridge_set_scheduler_aot_policy(-1);
  FzeroVersusDestroy(session);FzeroVersusVideoDestroy(video);
  session=NULL;video=NULL;enabled=setup_started=false;setup_frame=0;
}
bool FzeroLocalConfigure(FzeroVersusConfig c) {
  if(c.course>=15 || c.machine[0]>=4 || c.machine[1]>=4 ||
      FzeroDeluxeActive() || FzeroGameplayActive() || FzeroTracksActive())return false;
  FzeroLocalShutdown();config=c;enabled=true;
  /* Resource loader hooks are interpreted-opcode hooks. Keep native setup
   * on the byte interpreter so a compiled call cannot bypass configuration.
   * Gameplay never executes this CPU after the authoritative handoff. */
  interp_bridge_set_scheduler_aot_policy(0);
  interp_bridge_set_pre_opcode_hook(0x009f08,course_config);
  return true;
}
static uint16_t auto_word(uint32_t runner) {
  uint16_t result=0;
  for(unsigned i=0;i<12;++i)if(runner&(1u<<i))result|=(uint16_t)(0x8000u>>i);
  return result;
}
bool FzeroLocalTick(uint32_t packed) {
  if(!session)return false;
  uint16_t held[2]={auto_word(packed&4095),auto_word((packed>>12)&4095)};
  (void)FzeroVersusTick(session,held);
  return true;
}
uint16_t FzeroLocalSetupInput(void) {
  if(!enabled)return 0;
  /* Ordinary native menu inputs reach the normal GP handoff. Keep CPU stack,
   * scratch, asset and fade setup owned by their original menu routines. */
  const unsigned presses[]={320,800,920,1090,1150};
  for(unsigned i=0;i<sizeof(presses)/sizeof(*presses);++i)
    if(setup_frame>=presses[i] && setup_frame<=presses[i]+6)return 8;
  return setup_frame>1400 && setup_frame%90<6?8:0;
}
void FzeroLocalSetupTick(unsigned frame) {
  setup_frame=frame;
}
void FzeroLocalPrepareCourse(void) {
  if(!enabled || setup_started || g_ram[0x54]!=2)return;
  /* At the native $9F08 loader entry, after the ordinary GP menu handoff.
   * Only the selected config changes; decompression and legal grid setup
   * remain native, and no physics outcomes are injected. */
  g_ram[0x52]=config.machine[0];g_ram[0xd1]=config.machine[0]*2;
  g_ram[0x53]=config.course%5;g_ram[0x90]=config.course/5;
  setup_started=true;
}
void FzeroLocalCapture(void) {
  if(!enabled || session || !setup_started || g_ram[0x54]!=2 || g_ram[0x55]!=3 ||
      g_ram[0xc3] || !FzeroRendererHasFrame())return;
  FzeroActor spawns[2]={{0},{0}};
  for(unsigned i=0;i<2;++i) {
    unsigned offset=i*2;
    spawns[i].x=(uint16_t)word(0xb70+offset);spawns[i].y=(uint16_t)word(0xb90+offset);
    spawns[i].facing=(uint16_t)word(0xbd0+offset);
    spawns[i].checkpoint=g_ram[0xd00+offset];spawns[i].branch=(uint16_t)word(0x1130+offset);
  }
  FzeroVersus *candidate=FzeroVersusCreate(config,g_ram,spawns,tables());
  FzeroVersusVideo *display=FzeroVersusVideoCreate();
  if(!candidate || !display || !FzeroVersusVideoCapture(display,NULL,candidate,tables())) {
    FzeroVersusDestroy(candidate);FzeroVersusVideoDestroy(display);return;
  }
  session=candidate;video=display;
  FzeroVersusReady(session,0,true);FzeroVersusReady(session,1,true);
  fprintf(stderr,"[local-versus] course=%u machines=%u,%u grid=%u,%u / %u,%u shared session ready\n",
      config.course,config.machine[0],config.machine[1],spawns[0].x,spawns[0].y,spawns[1].x,spawns[1].y);
}
bool FzeroLocalDraw(uint32_t *out,size_t capacity,FzeroViewport viewport,double alpha,unsigned scale) {
  return session && FzeroVersusVideoDraw(video,out,capacity,viewport,alpha,scale);
}
void FzeroLocalResetViews(void) {
  if(!video)return;
  FzeroVersusVideoReset(video);
  (void)FzeroVersusVideoCapture(video,NULL,session,tables());
}
void FzeroLocalPublish(void) {
  if(video)(void)FzeroVersusVideoCapture(video,NULL,session,tables());
}
