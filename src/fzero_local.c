#include "fzero_local.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay.h"
#include "fzero_tracks.h"
#include "fzero_sound.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/cart.h"
#include "snes/snes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Snes *g_snes;
static bool enabled,setup_started;
static unsigned setup_frame;
static uint64_t epoch;
static FzeroVersusConfig config;
static FzeroVersus *session;
static FzeroVersusVideo *video;
static FzeroSoundState sound;
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
  memset(&sound,0,sizeof(sound));
}
bool FzeroLocalConfigure(FzeroVersusConfig c) {
  if(c.course>=15 || c.machine[0]>=4 || c.machine[1]>=4 ||
      FzeroDeluxeActive() || FzeroGameplayActive() || FzeroTracksActive())return false;
  FzeroLocalShutdown();config=c;enabled=true;++epoch;
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
  bool advanced=FzeroVersusTick(session,held);
  FzeroSoundCommands commands=FzeroSoundTick(&sound,FzeroVersusCompleted(session),advanced);
  for(unsigned port=0;port<4;++port)snes_writeBBus(g_snes,(uint8_t)(0x40+port),commands.ports[port]);
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
  uint8_t *world=malloc(0x20000);
  if(!world)return;
  FzeroActorCaptureWorld(world,g_ram);
  FzeroVersus *candidate=FzeroVersusCreate(config,world,spawns,tables());free(world);
  FzeroVersusVideo *display=FzeroVersusVideoCreate();
  if(!candidate || !display || !FzeroVersusVideoCapture(display,NULL,candidate,tables())) {
    FzeroVersusDestroy(candidate);FzeroVersusVideoDestroy(display);return;
  }
  session=candidate;video=display;
  FzeroVersusReady(session,0,true);FzeroVersusReady(session,1,true);
  FzeroSoundInit(&sound,g_ram[0x46]);
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
FzeroVersusConfig FzeroLocalConfig(void) {return config;}
uint64_t FzeroLocalEpoch(void) {return epoch;}
void FzeroLocalControlChanged(void) {++epoch;FzeroLocalResetViews();}
bool FzeroLocalSelectSaveRoot(void) {
  char root[96];
  if(snprintf(root,sizeof(root),"%s/local-2p",RtlSaveRoot())>=(int)sizeof(root))return false;
  RtlEnsureSaveDir();RtlSetSaveRoot(root);RtlEnsureSaveDir();
  RtlSetQuickSaveRoot(root);return true;
}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t get32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
size_t FzeroLocalSaveSize(void) {return 32+FzeroVersusSaveSize();}
bool FzeroLocalSave(void *output,size_t size) {
  if(!enabled || !output || size!=FzeroLocalSaveSize())return false;
  uint8_t *p=output;memset(p,0,size);memcpy(p,"FZLOCAL1",8);
  put32(p+8,2);put32(p+12,FZERO_VERSUS_RULES);put32(p+16,(uint32_t)size);put32(p+20,setup_frame);
  p[24]=config.course;p[25]=config.machine[0];p[26]=config.machine[1];
  p[27]=setup_started;p[28]=session!=NULL;
  p[29]=sound.music;p[30]=sound.landing;p[31]=sound.phase;
  return !session || FzeroVersusSave(session,p+32,size-32);
}
static bool decode(const void *input,size_t size,FzeroVersus **candidate,FzeroVersusConfig *c) {
  if(!input || size!=FzeroLocalSaveSize())return false;
  const uint8_t *p=input;
  unsigned version=get32(p+8);
  if(memcmp(p,"FZLOCAL1",8) || (version!=1 && version!=2) || get32(p+12)!=FZERO_VERSUS_RULES ||
      get32(p+16)!=size || p[24]>=15 || p[25]>=4 || p[26]>=4 ||
      p[27]>1 || p[28]>1 || (p[28] && !p[27]) ||
      (version==1 && (p[29] || p[30] || p[31])) ||
      (version==2 && ((p[29]&0x70) || p[30]>12 || (p[31]>7 && p[31]!=255))))return false;
  *c=(FzeroVersusConfig){p[24],{p[25],p[26]}};*candidate=NULL;
  if(p[28])return (*candidate=FzeroVersusDecode(p+32,size-32,tables(),c))!=NULL;
  for(size_t i=32;i<size;++i)if(p[i])return false;
  return true;
}
bool FzeroLocalValidate(const void *input,size_t size) {
  FzeroVersus *candidate;FzeroVersusConfig c;
  bool ok=decode(input,size,&candidate,&c);
  if(ok)FzeroVersusDestroy(candidate);return ok;
}
bool FzeroLocalRestore(const void *input,size_t size) {
  if(!enabled)return false;
  FzeroVersus *candidate;FzeroVersusConfig c;
  if(!decode(input,size,&candidate,&c))return false;
  FzeroVersusVideo *display=candidate?FzeroVersusVideoCreate():NULL;
  if(candidate && !display){FzeroVersusDestroy(candidate);return false;}
  FzeroVersusDestroy(session);FzeroVersusVideoDestroy(video);
  session=candidate;video=display;config=c;
  const uint8_t *p=input;setup_frame=get32(p+20);setup_started=p[27]!=0;
  if(get32(p+8)==1)FzeroSoundInit(&sound,g_ram[0x46]);
  else sound=(FzeroSoundState){p[29],p[30],p[31]};
  interp_bridge_set_scheduler_aot_policy(0);
  interp_bridge_set_pre_opcode_hook(0x009f08,course_config);
  return true;
}
