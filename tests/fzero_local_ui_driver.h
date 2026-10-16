#pragma once
/* Opt-in desktop qualification using actual SDL virtual devices and the
 * public human control path. No racer, world or progress injection. */
#if SNESRECOMP_SDL3
static SDL_Joystick *local_ui_pads[2];
static unsigned local_ui_stage,local_ui_step;
static void local_ui_attach(void) {
  if(!getenv("FZERO_TEST_LOCAL_UI"))return;
  for(unsigned p=0;p<2;++p) {
    SDL_VirtualJoystickDesc desc;SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=15;
    desc.name=p?"F-Zero UI validation P2":"F-Zero UI validation P1";
    SDL_JoystickID id=SDL_AttachVirtualJoystick(&desc);
    local_ui_pads[p]=id?SDL_OpenJoystick(id):NULL;
    if(!local_ui_pads[p])Die("Cannot attach UI validation gamepads");
    SDL_SetJoystickVirtualAxis(local_ui_pads[p],SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
    SDL_SetJoystickVirtualAxis(local_ui_pads[p],SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
  }
}
static void local_ui_button(unsigned player,SDL_GamepadButton button,bool held) {
  SDL_SetJoystickVirtualButton(local_ui_pads[player],button,held);
}
static bool local_ui_tick(FzeroLocalControls *controls) {
  if(!local_ui_pads[0])return false;
  for(unsigned p=0;p<2;++p)for(unsigned b=0;b<15;++b)local_ui_button(p,(SDL_GamepadButton)b,false);
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(FzeroLocalSession());
  bool setup=FzeroLocalControlsSetup(controls);unsigned t=local_ui_step++;
  if(!local_ui_stage && controls) {
    /* Native menu input was an ordinary script. From here the virtual pads
     * exclusively drive joining, selection, pause and mode transitions. */
    if(!FzeroReplayConfigurePlayers(NULL,getenv("FZERO_VIEWPORT_SCRIPT"),2))Die("UI validation replay setup failed");
    local_ui_stage=1;local_ui_step=0;t=0;
  }
  switch(local_ui_stage) {
    case 1:
      if(t<3)for(unsigned p=0;p<2;++p)local_ui_button(p,SDL_GAMEPAD_BUTTON_START,true);
      if(t>8 && s) {if(!setup || s->tick)Die("UI preload advanced a human race");local_ui_stage=2;local_ui_step=0;}
      break;
    case 2: case 8:
      if(t<3)for(unsigned p=0;p<2;++p)local_ui_button(p,SDL_GAMEPAD_BUTTON_START,true);
      if(t>5) {if(setup)Die("UI Ready failed");++local_ui_stage;local_ui_step=0;}
      break;
    case 3: case 5: case 9:
      if(s && s->tick>=(local_ui_stage==3?200u:12u))local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      if(s && s->pause) {++local_ui_stage;local_ui_step=0;}
      break;
    case 4:
      if(t==2 || t==3)local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t==6 || t==7)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      if(t>10) {if(!s || s->pause || s->phase!=FZERO_VS_COUNTDOWN)Die("UI Restart Race failed");local_ui_stage=5;local_ui_step=0;}
      break;
    case 6:
      if(t==2 || t==3 || t==6 || t==7)local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t==10 || t==11)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      if(t>14) {if(!setup)Die("UI Change Race failed");local_ui_stage=7;local_ui_step=0;}
      break;
    case 7:
      if(t==2 || t==3) {local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true);local_ui_button(1,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);}
      if(t>8 && s) {
        FzeroVersusConfig c=FzeroLocalControlsConfig(controls);
        if(c.course!=1 || c.machine[1]!=2 || s->config.course!=1 || s->config.machine[1]!=2 || s->tick)Die("UI course/machine preparation failed");
        local_ui_stage=8;local_ui_step=0;
      }
      break;
    case 10:
      if(!controls) {
        if(FzeroLocalEnabled() || g_ram[0x54] || g_ram[0x55]!=1)Die("UI Main Menu return failed");
        local_ui_stage=11;local_ui_step=0;break;
      }
      if(t==2 || t==3 || t==6 || t==7 || t==10 || t==11)local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t==14 || t==15)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      break;
    case 11:
      if(controls || FzeroLocalEnabled() || g_ram[0x54] || g_ram[0x55]!=1)Die("UI confirmation leaked into single-player");
      if(t>4) {fprintf(stderr,"local-ui: PASS native VS entry, two SDL devices, preload/Ready, cursor Restart/Change/Main Menu and single-player return\n");return true;}
      break;
    default:break;
  }
  SDL_PumpEvents();return false;
}
#else
static void local_ui_attach(void) {}
static bool local_ui_tick(FzeroLocalControls *controls) {(void)controls;return false;}
#endif
