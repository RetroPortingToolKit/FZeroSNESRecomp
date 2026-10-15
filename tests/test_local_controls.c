#include "fzero_local_controls.h"
#include "fzero_gamepad.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%d: %s (%s)\n",__LINE__,#x,SDL_GetError());exit(1);}}while(0)
/* The controller adapter does not need a live guest or a renderer. */
FzeroVersus *FzeroLocalSession(void) {return NULL;}
void FzeroLocalResetViews(void) {}
#if SNESRECOMP_SDL3
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
  FzeroLocalControls *c=FzeroLocalControlsCreate((FzeroVersusConfig){0,{0,1}},false);CHECK(c);
  poll(c,0);
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,true));
  CHECK(!poll(c,0));
  for(unsigned i=0;i<2;++i)CHECK(SDL_SetJoystickVirtualButton(pads[i],SDL_GAMEPAD_BUTTON_START,false));
  poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_DPAD_DOWN,true));poll(c,0);
  FzeroVersusConfig config=FzeroLocalControlsConfig(c);CHECK(config.machine[0]==1 && config.machine[1]==1);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_DPAD_DOWN,false));poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_SOUTH,true));
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_WEST,true));
  CHECK(poll(c,0)&FZERO_LOCAL_START);CHECK(!FzeroLocalControlsSetup(c));
  for(unsigned i=0;i<2;++i)for(unsigned b=0;b<15;++b)CHECK(SDL_SetJoystickVirtualButton(pads[i],b,false));
  poll(c,0);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_SOUTH,true));poll(c,0);
  CHECK(FzeroLocalControlsInput(c)==1);
  CHECK(SDL_SetJoystickVirtualButton(pads[1],SDL_GAMEPAD_BUTTON_WEST,true));poll(c,0);
  CHECK(FzeroLocalControlsInput(c)==4097);
  /* Keyboard cannot take either occupied seat or duplicate either input. */
  poll(c,8);poll(c,0);CHECK(FzeroLocalControlsInput(c)==4097);
  CHECK(SDL_SetJoystickVirtualButton(pads[0],SDL_GAMEPAD_BUTTON_SOUTH,false));
  CHECK(SDL_DetachVirtualJoystick(ids[0]));poll(c,0);
  poll(c,8);poll(c,0);poll(c,1);CHECK(FzeroLocalControlsInput(c)==4097);
  FzeroLocalControlsDestroy(c);
  SDL_CloseJoystick(pads[0]);SDL_CloseJoystick(pads[1]);CHECK(SDL_DetachVirtualJoystick(ids[1]));
  SDL_Quit();remove("local-controls-test.ini");
  puts("local controls: distinct devices, independent bindings, ready, duplicate machines and replacement keyboard passed");return 0;
}
#else
int main(void) {puts("local controls: SDL2 virtual-device fixture unavailable");return 0;}
#endif
