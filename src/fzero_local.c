#include "fzero_local.h"
#include "fzero_online.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay.h"
#include "fzero_tracks.h"
#include "fzero_course_runtime.h"
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
static bool enabled,setup_started,fast_setup;
static unsigned setup_frame;
static uint64_t epoch;
static FzeroVersusConfig config;
static FzeroVersus *session;
static FzeroVersusVideo *video;
static FzeroSoundState sound;
static FzeroNativeUi ui;
static unsigned menu_selection,result_ticks;
static uint8_t stock_tables[0x80000];
static bool have_stock_tables;
bool FzeroLocalStockTables(const uint8_t *rom,size_t size) {
  if(!rom || size!=sizeof(stock_tables))return false;
  memcpy(stock_tables,rom,size);have_stock_tables=true;return true;
}
unsigned FzeroLocalCupCount(void) {return FzeroTracksRuntimeCount();}
unsigned FzeroLocalCourseCount(unsigned cup) {
  unsigned n=0;while(FzeroTracksRuntimeTrack(cup,n))++n;return n;
}
unsigned FzeroLocalCourseId(unsigned cup,unsigned order) {
  for(unsigned i=0;i<cup;++i)order+=FzeroLocalCourseCount(i);
  return order;
}
bool FzeroLocalCourseLocation(unsigned id,unsigned *cup,unsigned *order) {
  for(unsigned i=0;i<FzeroLocalCupCount();++i) {
    unsigned count=FzeroLocalCourseCount(i);
    if(id<count) {if(cup)*cup=i;if(order)*order=id;return true;}id-=count;
  }
  return false;
}
const char *FzeroLocalCupName(unsigned cup) {
  const CpCup *c=FzeroTracksRuntimeCup(cup,NULL);return c?c->name:"";
}
const char *FzeroLocalCourseName(unsigned id) {
  unsigned cup,order;if(!FzeroLocalCourseLocation(id,&cup,&order))return "";
  const CpTrack *track=FzeroTracksRuntimeTrack(cup,order);return track?track->name:"";
}
static unsigned word(unsigned at) {return g_ram[at]|(unsigned)g_ram[at+1]<<8;}
static uint8_t read_table(void *context,uint32_t address) {
  (void)context;
  unsigned offset=(address>>16)*0x8000+(address&0x7fff);
  if(have_stock_tables)return offset<sizeof(stock_tables)?stock_tables[offset]:0;
  const uint8_t *p=cart_getRomPtr(g_snes->cart,(uint8_t)(address>>16),(uint16_t)address);
  return p?*p:0;
}
static FzeroActorTables tables(void) {return (FzeroActorTables){read_table,NULL};}
static void course_config(CpuState *cpu,uint32_t pc) {
  (void)cpu;(void)pc;FzeroLocalPrepareCourse();FzeroTracksRefreshCourse();
}
bool FzeroLocalEnabled(void) {return enabled;}
FzeroVersus *FzeroLocalSession(void) {return session;}
const FzeroNativeUi *FzeroLocalUi(void) {return enabled && ui.valid?&ui:NULL;}
bool FzeroLocalMenuReady(void) {
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(session);if(!s)return false;
  if(s->pause)return true;
  return FzeroVersusVideoResultsReady(s,result_ticks);
}
unsigned FzeroLocalMenuSelection(void) {return menu_selection;}
void FzeroLocalSetMenuSelection(unsigned choice) {
  if(choice<4 && choice!=menu_selection) {menu_selection=choice;FzeroLocalControlChanged();}
}
void FzeroLocalShutdown(void) {
  if(enabled)interp_bridge_set_scheduler_aot_policy(-1);
  FzeroVersusDestroy(session);FzeroVersusVideoDestroy(video);
  session=NULL;video=NULL;enabled=setup_started=false;setup_frame=0;
  memset(&sound,0,sizeof(sound));
  menu_selection=result_ticks=0;
}
bool FzeroLocalConfigure(FzeroVersusConfig c) {
  if(!FzeroLocalCourseLocation(c.course,NULL,NULL) || c.machine[0]>=4 || c.machine[1]>=4)return false;
  FzeroLocalShutdown();config=c;enabled=fast_setup=true;++epoch;
  if(!FzeroNativeUiInit(&ui,tables())) {FzeroLocalShutdown();return false;}
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
  unsigned previous_phase=FzeroVersusCompleted(session)->phase;
  uint16_t held[2]={auto_word(packed&4095),auto_word((packed>>12)&4095)};
  bool advanced=FzeroVersusTick(session,held);
  const FzeroVersusSnapshot *state=FzeroVersusCompleted(session);
  if(state->phase!=FZERO_VS_RESULTS || previous_phase!=FZERO_VS_RESULTS)result_ticks=0;
  else if(!state->pause && result_ticks<UINT32_MAX)++result_ticks;
  FzeroSoundCommands commands=FzeroSoundTick(&sound,FzeroVersusCompleted(session),advanced);
  for(unsigned port=0;port<4;++port)snes_writeBBus(g_snes,(uint8_t)(0x40+port),commands.ports[port]);
  return true;
}
uint16_t FzeroLocalSetupInput(void) {
  if(!enabled)return 0;
  if(fast_setup) {
    /* Scene-driven confirmations use the real native setup handlers. There
     * are no wall-clock waits between car/class/cup confirmation attempts. */
    return g_ram[0x54]<2 && (setup_frame&7)<2?8:0;
  }
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
  if(!enabled || setup_started || g_ram[0x54]!=2 ||
      (FzeroOnlineEnabled() && FzeroOnlineCompleted()->phase==FZERO_ONLINE_TITLE))return;
  /* At the native $9F08 loader entry, after the ordinary GP menu handoff.
   * Only the selected config changes; decompression and legal grid setup
   * remain native, and no physics outcomes are injected. */
  unsigned cup,order;if(!FzeroLocalCourseLocation(config.course,&cup,&order))return;
  const CpPack *pack=NULL;const CpCup *selected=FzeroTracksRuntimeCup(cup,&pack);
  FzeroTracksRuntimeSelect(cup);
  g_ram[0x52]=config.machine[0];g_ram[0xd1]=config.machine[0]*2;
  g_ram[0x53]=(uint8_t)order;
  g_ram[0x90]=(uint8_t)(!strcmp(pack->adapter,"fzero-course-v1")?0:selected->slot);
  setup_started=true;
}
void FzeroLocalCapture(void) {
  if(!enabled || session || !setup_started || g_ram[0x54]!=2 || (g_ram[0x55]!=3 && !(fast_setup && g_ram[0x55]==2 && g_ram[0x56]==2)) ||
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
  FzeroActorSetCourseResources(world,FzeroTracksCurrentCourse());
  if(FzeroDeluxeActive() && !FzeroTracksCurrentCourse()) {
    FzeroActorSetNativePalette(world,g_ram[0x14c03]);
    world[FZERO_WORLD_NATIVE_SHORTCUT]=(uint8_t)(g_ram[0x14c2a]+1);
  }
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
  FzeroVersusVideoMenu(video,menu_selection,result_ticks);
  if(FzeroOnlineEnabled())return session && FzeroVersusVideoDrawPlayer(video,out,capacity,viewport,alpha,scale,(unsigned)FzeroOnlineView());
  return session && FzeroVersusVideoDraw(video,out,capacity,viewport,alpha,scale);
}
void FzeroLocalResetViews(void) {
  if(!video)return;
  FzeroVersusVideoReset(video);
  FzeroVersusVideoMenu(video,menu_selection,result_ticks);
  (void)FzeroVersusVideoCapture(video,NULL,session,tables());
}
void FzeroLocalPublish(void) {
  FzeroVersusVideoMenu(video,menu_selection,result_ticks);
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
enum {LOCAL_HEADER=80};
size_t FzeroLocalSaveSize(void) {return LOCAL_HEADER+FzeroVersusSaveSize();}
static void content_identity(uint8_t hash[32]) {
  memset(hash,0,32);
  if(FzeroTracksStateActive())memcpy(hash,FzeroTracksActiveHash(),32);
}
bool FzeroLocalSave(void *output,size_t size) {
  if(!enabled || !output || size!=FzeroLocalSaveSize())return false;
  uint8_t *p=output;memset(p,0,size);memcpy(p,"FZLOCAL2",8);
  put32(p+8,4);put32(p+12,FZERO_VERSUS_RULES);put32(p+16,(uint32_t)size);
  put32(p+20,session?(FzeroVersusCompleted(session)->phase==FZERO_VS_RESULTS?result_ticks:0):setup_frame);
  p[24]=(uint8_t)config.course;p[25]=(uint8_t)(config.course>>8);p[26]=config.machine[0];p[27]=config.machine[1];
  p[28]=(uint8_t)(setup_started|(fast_setup?2:0));p[29]=session!=NULL;
  p[30]=sound.music;p[31]=sound.landing;
  p[32]=(uint8_t)((sound.phase==255?8:sound.phase)|(menu_selection<<4));
  content_identity(p+48);
  return !session || FzeroVersusSave(session,p+LOCAL_HEADER,size-LOCAL_HEADER);
}
static bool decode(const void *input,size_t size,FzeroVersus **candidate,FzeroVersusConfig *c) {
  if(!input || size!=FzeroLocalSaveSize())return false;
  const uint8_t *p=input;uint8_t identity[32];content_identity(identity);
  unsigned id=p[24]|(unsigned)p[25]<<8;
  if(memcmp(p,"FZLOCAL2",8) || get32(p+8)!=4 || get32(p+12)!=FZERO_VERSUS_RULES ||
      get32(p+16)!=size || !FzeroLocalCourseLocation(id,NULL,NULL) || p[26]>=4 || p[27]>=4 ||
      p[28]>3 || p[29]>1 || (p[29] && !(p[28]&1)) || (p[30]&0x70) || p[31]>12 ||
      (p[32]&15)>8 || (p[32]>>4)>3 || memcmp(p+48,identity,32))return false;
  for(unsigned i=33;i<48;++i)if(p[i])return false;
  *c=(FzeroVersusConfig){(uint16_t)id,{p[26],p[27]}};*candidate=NULL;
  if(p[29])return (*candidate=FzeroVersusDecode(p+LOCAL_HEADER,size-LOCAL_HEADER,tables(),c))!=NULL;
  for(size_t i=LOCAL_HEADER;i<size;++i)if(p[i])return false;
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
  const uint8_t *p=input;setup_frame=session?0:get32(p+20);setup_started=(p[28]&1)!=0;fast_setup=(p[28]&2)!=0;
  result_ticks=session?get32(p+20):0;menu_selection=p[32]>>4;
  sound=(FzeroSoundState){p[30],p[31],(p[32]&15)==8?255:p[32]&15};
  interp_bridge_set_scheduler_aot_policy(0);
  interp_bridge_set_pre_opcode_hook(0x009f08,course_config);
  return true;
}
