#include "fzero_local_controls.h"
#include "fzero_gamepad.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%d: %s (%s)\n",__LINE__,#x,SDL_GetError());exit(1);}}while(0)
/* Real shared core and virtual devices; no guest/assets/speaker output. */
static FzeroVersus *race;
FzeroVersus *FzeroLocalSession(void) {return race;}
const FzeroNativeUi *FzeroLocalUi(void) {return NULL;}
FzeroVersusConfig FzeroLocalConfig(void) {return FzeroVersusCompleted(race)->config;}
void FzeroLocalResetViews(void) {}
static unsigned selection;
bool FzeroLocalMenuReady(void) {return true;}
unsigned FzeroLocalMenuSelection(void) {return selection;}
void FzeroLocalSetMenuSelection(unsigned n) {selection=n;}
void FzeroLocalControlChanged(void) {}
#if SNESRECOMP_SDL3
static uint8_t empty_table(void *context,uint32_t address) {(void)context;(void)address;return 0;}
static SDL_JoystickID attach(const char *name) {
  SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
  desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=15;desc.name=name;
  SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);CHECK(id);return id;
}
static unsigned poll(FzeroLocalControls *c,uint32_t keyboard) {
  SDL_Event e;while(SDL_PollEvent(&e))FzeroLocalControlsEvent(c,&e);
  return FzeroLocalControlsUpdate(c,keyboard);
}
int main(void) {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
  CHECK(SDL_Init(SDL_INIT_GAMEPAD));
  SDL_JoystickID ids[2]={attach("local player one"),attach("local player two")};
  SDL_Joystick *pads[2]={SDL_OpenJoystick(ids[0]),SDL_OpenJoystick(ids[1])};CHECK(pads[0] && pads[1]);
  FILE *f=fopen("local-controls-test.ini","w");CHECK(f);
  fputs("[Local2P.P2]\nControls=DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,X,Y,A,Lb,Rb\n",f);fclose(f);
  FzeroGamepadConfigure("local-controls-test.ini","",25);
  FzeroLocalControls *probe=FzeroLocalControlsCreate((FzeroVersusConfig){0,{0,1}},false);CHECK(probe);
  CHECK(FzeroLocalControlsKeyboardPlayer(probe)==0);poll(probe,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_START,true));poll(probe,0);
  CHECK(FzeroLocalControlsKeyboardPlayer(probe)==1);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_START,false));poll(probe,0);
  poll(probe,8);poll(probe,0);CHECK(FzeroLocalControlsKeyboardPlayer(probe)==1);
  poll(probe,256);CHECK(FzeroLocalControlsInput(probe)==1048576); /* keyboard's actual P2 seat */
  FzeroLocalControlsDestroy(probe);
  FzeroLocalControls *c=FzeroLocalControlsCreate((FzeroVersusConfig){0,{0,1}},false);CHECK(c);
  poll(c,0);
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,true));
  CHECK(!poll(c,0));
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,false));
  poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));poll(c,0);
  FzeroVersusConfig config=FzeroLocalControlsConfig(c);CHECK(config.machine[0]==2 && config.machine[1]==1);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));poll(c,0);
  CHECK(FzeroLocalControlsConfig(c).machine[0]==1); /* Native order, duplicate cars allowed. */
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll(c,0);
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,true));
  CHECK(!poll(c,0));CHECK(FzeroLocalControlsPage(c)==FZERO_LOCAL_CUP);
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,false));poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_SOUTH,true));CHECK(!poll(c,0));
  CHECK(FzeroLocalControlsPage(c)==FZERO_LOCAL_MACHINES);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_SOUTH,false));poll(c,0);
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,true));poll(c,0);
  CHECK(FzeroLocalControlsPage(c)==FZERO_LOCAL_CUP);
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,false));poll(c,0);
  /* Either player can select the shared league. B backs up one page. */
  for(unsigned league=0;league<3;++league) {
    CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_START,true));CHECK(!poll(c,0));
    CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_START,false));poll(c,0);
    CHECK(FzeroLocalControlsPage(c)==FZERO_LOCAL_COURSE);
    for(unsigned course=0;course<5;++course) {
      CHECK(FzeroLocalControlsConfig(c).course==league*5+course);
      CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));poll(c,0);
      CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll(c,0);
    }
    FzeroLocalControlsBack(c);CHECK(!poll(c,0));poll(c,0);
    CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));poll(c,0);
    CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll(c,0);
  }
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_START,true));CHECK(!poll(c,0));
  CHECK(FzeroLocalControlsPage(c)==FZERO_LOCAL_COURSE && FzeroLocalControlsConfig(c).course==5);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_START,false));poll(c,0);
  FzeroLocalControlsBack(c);CHECK(!poll(c,0));CHECK(FzeroLocalControlsPage(c)==FZERO_LOCAL_CUP);poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_START,true));poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_START,false));poll(c,0);
  CHECK(FzeroLocalControlsPage(c)==FZERO_LOCAL_COURSE);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll(c,0);
  CHECK(FzeroLocalControlsConfig(c).course==6);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_START,true));
  CHECK(poll(c,0)&FZERO_LOCAL_START);CHECK(!FzeroLocalControlsSetup(c));
  for(unsigned i=0;i<2;++i)for(unsigned b=0;b<15;++b)CHECK(SDL_SetJoystickVirtualButton(pads[i],b,false));
  poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_SOUTH,true));poll(c,0);
  CHECK(FzeroLocalControlsInput(c)==1);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_WEST,true));poll(c,0);
  CHECK(FzeroLocalControlsInput(c)==4097);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true));
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true));poll(c,0);
  CHECK(FzeroLocalControlsInput(c)==(1025u|(2049u<<12)));
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,false));
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,false));poll(c,0);
  /* Keyboard cannot take either occupied seat or duplicate either input. */
  poll(c,8);poll(c,0);CHECK(FzeroLocalControlsInput(c)==4097);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_SOUTH,false));
  CHECK(SDL_DetachVirtualJoystick(ids[0]));poll(c,0);
  poll(c,8);poll(c,0);poll(c,1);CHECK(FzeroLocalControlsInput(c)==4097);
  static uint8_t world[0x20000];
  FzeroActor spawns[2]={{.x=256,.y=256},{.x=320,.y=256}};
  race=FzeroVersusCreate(FzeroLocalControlsConfig(c),world,spawns,(FzeroActorTables){empty_table,NULL});CHECK(race);
  FzeroVersusReady(race,0,true);FzeroVersusReady(race,1,true);
  const uint16_t start[2]={0x1000,0};CHECK(!FzeroVersusTick(race,start));
  CHECK(FzeroVersusCompleted(race)->pause==FZERO_VS_PAUSE_MANUAL);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_WEST,false));
  poll(c,0);poll(c,8);CHECK(!FzeroVersusCompleted(race)->pause && !FzeroLocalControlsInput(c));poll(c,0);
  SDL_Event focus={0};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;FzeroLocalControlsEvent(c,&focus);
  CHECK(FzeroVersusCompleted(race)->pause==FZERO_VS_PAUSE_FOCUS);
  poll(c,0);poll(c,8);CHECK(!FzeroVersusCompleted(race)->pause);poll(c,0);
  FzeroVersusPause(race,FZERO_VS_PAUSE_MANUAL);poll(c,0);
  poll(c,0x200);CHECK(FzeroVersusCompleted(race)->pause && FzeroVersusCompleted(race)->phase!=FZERO_VS_RESULTS);poll(c,0);
  poll(c,0x20);CHECK(selection==1);poll(c,0);poll(c,8);
  CHECK(FzeroVersusCompleted(race)->phase==FZERO_VS_COUNTDOWN && !FzeroVersusCompleted(race)->pause);poll(c,0);
  FzeroVersusEndRace(race);poll(c,0);poll(c,2);CHECK(!FzeroLocalControlsSetup(c));poll(c,0);
  poll(c,8);CHECK(FzeroVersusCompleted(race)->phase==FZERO_VS_COUNTDOWN && !FzeroLocalControlsInput(c));poll(c,0);
  FzeroVersusEndRace(race);poll(c,0);poll(c,0x20);poll(c,0);
  CHECK(poll(c,8)&FZERO_LOCAL_CHANGE);CHECK(FzeroLocalControlsSetup(c));poll(c,0);
  FzeroLocalControlsBack(c);CHECK(poll(c,0)&FZERO_LOCAL_MAIN);
  FzeroLocalControlsLoaded(c);CHECK(!FzeroLocalControlsSetup(c));poll(c,0);
  CHECK(SDL_DetachVirtualJoystick(ids[1]));poll(c,0);
  CHECK(FzeroVersusCompleted(race)->pause&FZERO_VS_PAUSE_DEVICE2);
  uint64_t tick=FzeroVersusCompleted(race)->tick;
  CHECK(!FzeroVersusTick(race,(const uint16_t[2]){0,0}) && FzeroVersusCompleted(race)->tick==tick);
  /* An occupied keyboard cannot silently become the disconnected P2. */
  poll(c,8);CHECK(FzeroVersusCompleted(race)->pause);poll(c,0);
  FzeroLocalControlsReopen(c);CHECK(FzeroLocalControlsSetup(c));
  FzeroVersusDestroy(race);race=NULL;
  FzeroLocalControlsDestroy(c);
  SDL_CloseJoystick(pads[0]);SDL_CloseJoystick(pads[1]);
  SDL_Quit();remove("local-controls-test.ini");
  puts("local controls: distinct devices/bindings, ready, duplicate machines, replacement, pause/resume/focus/end/rematch/change/load/reset/disconnect passed");return 0;
}
#else
int main(void) {puts("local controls: SDL2 virtual-device fixture unavailable");return 0;}
#endif
