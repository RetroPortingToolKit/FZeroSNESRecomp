#include "fzero_gamepad.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s (%s)\n", __LINE__, #x, SDL_GetError()); exit(1); } } while (0)

#if SNESRECOMP_SDL3
static SDL_JoystickID attach(const char *name) {
  SDL_VirtualJoystickDesc desc;
  SDL_INIT_INTERFACE(&desc);
  desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
  desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
  desc.nbuttons = 15;
  desc.name = name;
  SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
  CHECK(id != 0);
  return id;
}

static void pump(SDL_GameController **pad) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) FzeroGamepadEvent(pad, &event);
}

int main(void) {
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  CHECK(SDL_Init(SDL_INIT_GAMEPAD));
  SDL_JoystickID decoy = attach("unselected controller");
  SDL_JoystickID selected = attach("selected controller");
  SDL_Joystick *joy = SDL_OpenJoystick(selected);
  CHECK(joy);
  char guid[40];
  SDL_GUIDToString(SDL_GetJoystickGUID(joy), guid, sizeof(guid));
  FILE *cfg = fopen("gamepad-test.ini", "w");
  CHECK(cfg);
  fprintf(cfg, "[GamepadMap]\nControls = DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb\n"
               "[Controller.%s]\nControls = ,DpadDown,DpadLeft,DpadRight,Back,Start,L2,X,Y,A,Lb,Rb\nDeadzone = 3\n", guid);
  fclose(cfg);
  FzeroGamepadConfigure("gamepad-test.ini", guid, 25);
  SDL_GameController *pad = NULL;
  FzeroGamepadRefresh(&pad);
  CHECK(pad && SDL_GetGamepadID(pad) == selected);
  pump(&pad);
  CHECK(SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_WEST, true));
  pump(&pad);
  CHECK(FzeroGamepadRead(pad) == 1u); /* remapped SNES B */
  CHECK(SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_WEST, false));
  CHECK(SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_DPAD_UP, true));
  pump(&pad);
  CHECK(FzeroGamepadRead(pad) == 0); /* empty first field stays unbound */
  CHECK(SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_DPAD_UP, false));
  CHECK(SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, 2000));
  CHECK(SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767));
  pump(&pad);
  CHECK(FzeroGamepadRead(pad) == (0x0080u | 0x0100u)); /* profile deadzone + L2 */
  int shortcuts[2] = {
    RECOMP_LAUNCHER_PAD_BUTTON(SDL_GAMEPAD_BUTTON_WEST),
    RECOMP_LAUNCHER_PAD_BUTTON_COMBO((1u << SDL_GAMEPAD_BUTTON_WEST) | (1u << SDL_GAMEPAD_BUTTON_NORTH))
  };
  uint32_t active;
  CHECK(SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_WEST, true));
  pump(&pad);
  CHECK(FzeroGamepadReadShortcuts(pad, shortcuts, 2, &active) == (0x0080u | 0x0100u));
  CHECK(active == 1u); /* single shortcut consumed, incomplete chord ignored */
  CHECK(SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_NORTH, true));
  pump(&pad);
  CHECK(FzeroGamepadReadShortcuts(pad, shortcuts, 2, &active) == (0x0080u | 0x0100u));
  CHECK(active == 3u); /* complete chord consumed */
  shortcuts[0] = RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1);
  shortcuts[1] = RECOMP_LAUNCHER_PAD_AXIS(SDL_GAMEPAD_AXIS_LEFTX, 1);
  CHECK(FzeroGamepadReadShortcuts(pad, shortcuts, 2, &active) == (1u | (1u << 9)));
  CHECK(active == 3u); /* trigger and stick shortcut suppress guest equivalents */
  shortcuts[0] = shortcuts[1] = 0;
  CHECK(FzeroGamepadReadShortcuts(pad, shortcuts, 2, &active) ==
        (0x0080u | 0x0100u | 1u | (1u << 9)));
  CHECK(active == 0); /* explicit unbind remains unbound */
  /* Recomp-ui stores two active seat maps independently of a device GUID.
   * Identical controllers must still honor different bindings/deadzones. */
  cfg=fopen("gamepad-test.ini","w");CHECK(cfg);
  fprintf(cfg,"[GamepadMap]\nControls=DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb\n"
      "ControlsP2=DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,X,Y,A,Lb,Rb\n"
      "[Controller.%s]\nControls=,DpadDown,DpadLeft,DpadRight,Back,Start,L2,X,Y,A,Lb,Rb\nDeadzone=3\n"
      "[Controller]\nDeadzoneP1=10\nDeadzoneP2=80\n",guid);
  fclose(cfg);FzeroGamepadConfigure("gamepad-test.ini",guid,25);
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_NORTH,false));
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_WEST,false));
  CHECK(SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768));
  CHECK(SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTX,10000));pump(&pad);
  CHECK(FzeroGamepadReadPlayer(pad,0)==128 && !FzeroGamepadReadPlayer(pad,1));
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_SOUTH,true));pump(&pad);
  CHECK(FzeroGamepadReadPlayer(pad,0)==129 && FzeroGamepadReadPlayer(pad,1)==2);
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_SOUTH,false));
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_WEST,true));pump(&pad);
  CHECK(FzeroGamepadReadPlayer(pad,0)==130 && FzeroGamepadReadPlayer(pad,1)==1);
  CHECK(FzeroGamepadRead(pad)==129); /* local maps do not replace the SP device profile */
  cfg=fopen("gamepad-test.ini","a");CHECK(cfg);
  fputs("[Local2P.P2]\nDeadzone=2\nControls=DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb\n",cfg);
  fclose(cfg);FzeroGamepadConfigure("gamepad-test.ini",guid,25);
  CHECK(FzeroGamepadReadPlayer(pad,1)==130); /* explicit local overrides remain final */
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_WEST,false));
  CHECK(SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTX,0));pump(&pad);
  CHECK(SDL_DetachVirtualJoystick(decoy));
  pump(&pad);
  CHECK(pad && SDL_GetGamepadID(pad) == selected); /* other device removal */
  SDL_CloseJoystick(joy);
  /* Long unchanged steering is valid input; release and new buttons must
   * remain responsive after more than seven minutes' worth of reads. */
  CHECK(SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTX,32767));pump(&pad);
  for(unsigned frame=0;frame<8*60*60;++frame)CHECK(FzeroGamepadRead(pad)&0x80);
  CHECK(SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFTX,0));pump(&pad);
  CHECK(!(FzeroGamepadRead(pad)&0xc0));
  FILE *remap=fopen("gamepad-test.ini","a");CHECK(remap);
  fputs("\n[Local2P.P1]\nControls=DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,X,Y,A,Lb,Rb\n",remap);fclose(remap);
  SDL_Event remapped={0};remapped.type=SDL_EVENT_GAMEPAD_REMAPPED;remapped.gdevice.which=selected;
  FzeroGamepadEvent(&pad,&remapped);
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_WEST,true));pump(&pad);
  CHECK(FzeroGamepadReadPlayer(pad,0)&1);
  CHECK(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_WEST,false));pump(&pad);
  CHECK(SDL_DetachVirtualJoystick(selected));
  pump(&pad);
  CHECK(!pad && FzeroGamepadRead(pad) == 0);
  selected = attach("selected controller");
  pump(&pad);
  CHECK(pad && SDL_GetGamepadID(pad) == selected); /* same GUID, new instance */
  SDL_CloseGamepad(pad);
  SDL_Quit();
  remove("gamepad-test.ini");
  puts("F-Zero gamepad: selection, bindings, per-seat launcher maps/deadzones, local overrides, removal and reconnect passed");
  return 0;
}
#endif
