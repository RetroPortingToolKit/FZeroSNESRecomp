#pragma once
/* Opt-in desktop qualification using actual SDL virtual devices and the
 * public human control path. Results are requested through EndRace only to
 * exercise menu routing; this fixture does not certify natural race finishes. */
#if SNESRECOMP_SDL3
static SDL_Joystick *local_ui_pads[2];
static unsigned local_ui_stage,local_ui_step,local_ui_resizes,local_ui_captured;
static unsigned local_ui_countdowns;
static unsigned local_ui_setup_shots,local_ui_page;
static unsigned local_ui_arrival_shots,local_ui_catalog_shots;
static bool local_ui_setup;
static unsigned local_ui_cup;
static bool local_ui_racing;
static bool local_ui_live_captured;
static unsigned local_ui_drive_captured;
static uint16_t local_ui_drive_start[2][2];
static uint64_t local_ui_traffic_tick;
static unsigned local_ui_traffic_contacts;
static const unsigned local_ui_resize_ticks[]={10,65,125,220,260};
static const int local_ui_sizes[][2]={{600,800},{1342,1378},{1280,720},{1600,600},{768,576}};
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
static bool local_ui_tick(FzeroLocalControls *controls,SDL_Window *window) {
  if(!local_ui_pads[0])return false;
  for(unsigned p=0;p<2;++p)for(unsigned b=0;b<15;++b)local_ui_button(p,(SDL_GamepadButton)b,false);
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(FzeroLocalSession());
  bool setup=FzeroLocalControlsSetup(controls);unsigned t=local_ui_step++;
  local_ui_setup=setup;local_ui_page=FzeroLocalControlsPage(controls);
  if(!local_ui_stage && controls) {
    /* Native menu input was an ordinary script. From here the virtual pads
     * exclusively drive joining, selection, pause and mode transitions. */
    if(!FzeroReplayConfigurePlayers(NULL,getenv("FZERO_VIEWPORT_SCRIPT"),2))Die("UI validation replay setup failed");
    int sources[2]={2,2};char guids[2][40];
    for(unsigned i=0;i<2;++i)SDL_GUIDToString(SDL_GetJoystickGUID(local_ui_pads[i]),guids[i],40);
    FzeroLocalControlsAssign(controls,sources,guids);
    local_ui_stage=1;local_ui_step=0;t=0;
  }
  switch(local_ui_stage) {
    case 1:
      if(getenv("FZERO_TEST_MENU_MUSIC")) {
        if(t==360) {fprintf(stderr,"menu-music: held native VS picker for360 UI ticks; samples=%ld\n",g_test_audio_output?ftell(g_test_audio_output)/4:0);return true;}
        if(t>3)break;
      }
      if(t<3)for(unsigned p=0;p<2;++p)local_ui_button(p,SDL_GAMEPAD_BUTTON_START,true);
      if(getenv("FZERO_TEST_LOCAL_UI_ALT_MACHINES") && (t==4 || t==5))
        for(unsigned p=0;p<2;++p)local_ui_button(p,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(getenv("FZERO_TEST_LOCAL_UI_FULL_MACHINES") && (t==4 || t==5)) {
        local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true);local_ui_button(1,SDL_GAMEPAD_BUTTON_DPAD_UP,true);
      }
      if(t>(getenv("FZERO_TEST_LOCAL_UI_FULL_MACHINES")?24u:12u)) {
        /* Title entry retains its real menu soundtrack and creates the race
         * only at Ready. Direct/Change Race entry may already own a frozen
         * countdown; neither path may advance a human race in this picker. */
        if(!setup || (s && s->tick))Die("UI picker advanced a human race");
        local_ui_stage=2;local_ui_step=0;
      }
      break;
    case 2: case 8: {
      unsigned cup=getenv("FZERO_TEST_LOCAL_UI_CUP")?(unsigned)atoi(getenv("FZERO_TEST_LOCAL_UI_CUP")):0;
      local_ui_cup=cup;
      unsigned select=10+(local_ui_stage==2?cup*4:0);
      if(local_ui_stage==2 && ((t==select && !(local_ui_catalog_shots&1)) ||
          (t==select+12 && !(local_ui_catalog_shots&2)))) {local_ui_step=t;break;}
      if(local_ui_stage==2 && t>=6 && t<6+cup*4 && ((t-6)%4)<2)local_ui_button(1,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t<3)for(unsigned p=0;p<2;++p)local_ui_button(p,SDL_GAMEPAD_BUTTON_START,true);
      if(t==select || t==select+1)local_ui_button(1,SDL_GAMEPAD_BUTTON_START,true);
      if(local_ui_stage==8 && (t==select+6 || t==select+7))local_ui_button(1,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t==select+12 || t==select+13)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      if(t>select+16) {
        if(setup || !s || s->config.course!=FzeroLocalCourseId(cup,local_ui_stage==8?1:0))Die("UI car/league/course confirmation failed");
        ++local_ui_stage;local_ui_step=0;
      }
      break;
    }
    case 3: case 5: case 9:
      if(local_ui_stage==3 && s && getenv("FZERO_TEST_LOCAL_UI_DRIVE")) {
        for(unsigned p=0;p<2;++p) {
          if(s->phase==FZERO_VS_RACING)local_ui_button(p,SDL_GAMEPAD_BUTTON_SOUTH,true);
          else {local_ui_drive_start[p][0]=s->racers[p].actor.x;local_ui_drive_start[p][1]=s->racers[p].actor.y;}
        }
        unsigned tick=(unsigned)s->race_tick;
        if(getenv("FZERO_TEST_TRAFFIC_VISIBLE") && local_ui_drive_captured==63 && tick>=225 && !local_ui_traffic_tick) {
          /* Prepared contact encounter, not natural-driving qualification.
           * Place each human on a different moving NPC with fresh health and
           * contact cooldown. Do not set race progress, status or results. */
          FzeroVersusSnapshot *race=(FzeroVersusSnapshot *)(void *)s;
          for(unsigned p=0;p<2;++p) {
            if(!s->traffic[p].active || s->racers[p].status!=FZERO_VS_ACTIVE)Die("Traffic encounter unavailable");
            const FzeroActor *npc=&s->traffic[p].actor;FzeroActor *a=&race->racers[p].actor;
            a->x=npc->x;a->y=npc->y;a->x_fraction=a->y_fraction=0;
            a->facing=a->momentum=npc->facing;a->speed=npc->speed+0x500;a->speed_fraction=0;
            a->vx=a->vx_fraction=a->vy=a->vy_fraction=a->spin=a->no_control=0;
            a->height=npc->height;a->height_velocity=0;a->health=0x800;a->damage_cooldown=0;
            a->state=a->recovery_active=0;a->checkpoint=npc->checkpoint;a->branch=npc->branch;
            FzeroActorCheckpointInitialize(a,FzeroVersusWorld(FzeroLocalSession()));
          }
          local_ui_traffic_tick=s->race_tick;
          fputs("traffic-encounter: position/motion/health fixture; native contacts and rendering remain active\n",stderr);
        } else if(local_ui_traffic_tick && tick>local_ui_traffic_tick && local_ui_traffic_contacts!=3) {
          for(unsigned p=0;p<2;++p)if(s->racers[p].actor.health<0x800 && s->racers[p].actor.damage_cooldown)
            local_ui_traffic_contacts|=1u<<p;
          if(local_ui_traffic_contacts==3)fputs("traffic-encounter: both human impacts PASS\n",stderr);
          else if(tick>local_ui_traffic_tick+5)Die("Traffic encounter did not hit both humans");
        }
        /* Presenter readback can consume several simulation ticks in HD.
         * Hold each input until its image is observed before releasing it. */
        if((tick>=30 && !(local_ui_drive_captured&2)) ||
           (tick>=100 && (local_ui_drive_captured&4) && !(local_ui_drive_captured&8)))for(unsigned p=0;p<2;++p)
          local_ui_button(p,((!(local_ui_drive_captured&2))^p)?SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);
        if(tick>=155 && (local_ui_drive_captured&8) && !(local_ui_drive_captured&16)) {
          local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true);local_ui_button(1,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,true);
          local_ui_button(0,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,true);local_ui_button(1,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,true);
        }
      }
      if(local_ui_stage==3 && s && getenv("FZERO_TEST_LOCAL_UI_RESIZE") && local_ui_resizes<5 &&
         s->tick>=local_ui_resize_ticks[local_ui_resizes]) {
        SDL_SetWindowSize(window,local_ui_sizes[local_ui_resizes][0],local_ui_sizes[local_ui_resizes][1]);
        ++local_ui_resizes;
      }
      if(s && !(local_ui_stage==3 && getenv("FZERO_TEST_LOCAL_UI_DRIVE") && local_ui_drive_captured!=63)) {
        unsigned finish=local_ui_stage==3?(getenv("FZERO_TEST_TRAFFIC_VISIBLE")?600u:
            getenv("FZERO_TEST_LOCAL_UI_DRIVE")?500u:360u):FZERO_VERSUS_COUNTDOWN+24;
        if(s->tick>=finish && s->tick<finish+12)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
        if(s->pause)Die("Multiplayer Start paused the race");
        if(getenv("FZERO_TEST_LOCAL_UI_LIVE")) {
          if(s->tick>=finish+12 && s->phase!=FZERO_VS_RESULTS) {
            if(FzeroLocalMenuOwner()!=1)Die("UI Start did not open live menu");
            ++local_ui_stage;local_ui_step=0;
          }
        } else if(s->tick>=finish+12 && s->phase!=FZERO_VS_RESULTS)FzeroVersusEndRace(FzeroLocalSession());
        if(s->phase==FZERO_VS_RESULTS && FzeroLocalMenuReady()) {++local_ui_stage;local_ui_step=0;}
      }
      break;
    case 4:
      if(getenv("FZERO_TEST_LOCAL_UI_LIVE") && (t==2 || t==3))local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t==6 || t==7)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      if(t>10) {if(!s || s->pause || s->phase!=FZERO_VS_COUNTDOWN)Die("UI Restart Race failed");local_ui_stage=5;local_ui_step=0;}
      break;
    case 6:
      if(t==2 || t==3)local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(getenv("FZERO_TEST_LOCAL_UI_LIVE") && (t==6 || t==7))local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t==10 || t==11)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      if(t>14) {if(!setup)Die("UI Change Race failed");local_ui_stage=7;local_ui_step=0;}
      break;
    case 7:
      if(t==2 || t==3) {local_ui_button(1,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);}
      if(t>8 && s) {
        FzeroVersusConfig c=FzeroLocalControlsConfig(controls);
        unsigned machine=getenv("FZERO_TEST_LOCAL_UI_FULL_MACHINES")?1:getenv("FZERO_TEST_LOCAL_UI_ALT_MACHINES")?10:3;
        if(c.course!=FzeroLocalCourseId(local_ui_cup,0) || c.machine[1]!=machine || s->config.course!=c.course || s->config.machine[1]!=machine || s->tick)Die("UI course/machine preparation failed");
        local_ui_stage=8;local_ui_step=0;
      }
      break;
    case 10:
      if(!controls) {
        if(FzeroLocalEnabled() || g_ram[0x54] || g_ram[0x55]!=1)Die("UI Main Menu return failed");
        local_ui_stage=11;local_ui_step=0;break;
      }
      if(t==2 || t==3 || t==6 || t==7)local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(getenv("FZERO_TEST_LOCAL_UI_LIVE") && (t==10 || t==11))local_ui_button(0,SDL_GAMEPAD_BUTTON_DPAD_DOWN,true);
      if(t==14 || t==15)local_ui_button(0,SDL_GAMEPAD_BUTTON_START,true);
      break;
    case 11:
      if(controls || FzeroLocalEnabled() || g_ram[0x54] || g_ram[0x55]!=1)Die("UI confirmation leaked into single-player");
      if(t>4) {
        if(getenv("FZERO_TEST_LOCAL_UI_LIVE") && !local_ui_live_captured)Die("Live menu was never presented");
        if(getenv("FZERO_TEST_LOCAL_UI_RESIZE") &&
           (local_ui_captured!=31 || local_ui_countdowns!=14 || !local_ui_racing))Die("UI resize/countdown presentation coverage incomplete");
        if(local_ui_setup_shots!=7)Die("UI setup-page presentation coverage incomplete");
        if(local_ui_arrival_shots!=7 || local_ui_catalog_shots!=3)Die("UI arrival/catalog presentation coverage incomplete");
        if(getenv("FZERO_TEST_LOCAL_UI_DRIVE") && local_ui_drive_captured!=63)Die("UI drive animation coverage incomplete");
        fprintf(stderr,"local-ui: PASS native VS entry, two SDL devices, preload/Ready, live/results Restart/Change/Main Menu and single-player return\n");return true;}
      break;
    default:break;
  }
  SDL_PumpEvents();return false;
}

static void local_ui_images(const FzeroPresenter *p,const char *directory,const char *label) {
  if(!directory)return;
  char path[2048];snprintf(path,sizeof(path),"%s/%s-presenter.ppm",directory,label);dump_presenter(p,path);
  const uint32_t *frame=FzeroHdFrame();unsigned scale=FzeroHdScale();
  snprintf(path,sizeof(path),"%s/%s-frame.bmp",directory,label);
  if(!write_frame_bmp(path,frame?(const uint8_t *)frame:p->pixels,p->logical_width*(int)scale,
      (p->host_height?p->host_height:FzeroFrameHeight())*(int)scale))
    Die("UI frame capture failed");
}
/* Observe the actual composed frame and real presenter before swap. The old
 * fixture proved menu transitions while accepting a completely corrupt image. */
static void local_ui_presented(const FzeroPresenter *p) {
  if(!local_ui_pads[0])return;
  const char *directory=getenv("FZERO_TEST_LOCAL_UI_DUMP");
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(FzeroLocalSession());
  static bool title_captured;
  if(s && FzeroLocalMenuOwner() && !local_ui_live_captured) {
    local_ui_live_captured=true;local_ui_images(p,directory,"live-menu");
  }
  char label[64];
  if(local_ui_setup && !(local_ui_setup_shots&(1u<<local_ui_page))) {
    const FzeroNativeUi *ui=FzeroLocalUi();
    if(!ui || !ui->valid || !ui->information_valid || ui->machines_available!=4095)
      Die("VS picker has no complete twelve-machine artwork before Ready");
    if(FzeroGpuHdFrame() || FzeroHdFrame())Die("VS picker is using a retained game frame");
    if(p->host_height!=448)Die("VS picker did not present both native player panes");
    local_ui_setup_shots|=1u<<local_ui_page;
    snprintf(label,sizeof(label),"setup-page-%u",local_ui_page);local_ui_images(p,directory,label);return;
  }
  if(local_ui_setup && local_ui_stage==2 && local_ui_step>=6+local_ui_cup*4 &&
      local_ui_page==FZERO_LOCAL_CUP && !(local_ui_catalog_shots&1)) {
    local_ui_catalog_shots|=1;local_ui_images(p,directory,"selected-league");return;
  }
  if(local_ui_setup && local_ui_stage==2 && local_ui_page==FZERO_LOCAL_COURSE && !(local_ui_catalog_shots&2)) {
    local_ui_catalog_shots|=2;local_ui_images(p,directory,"selected-course");return;
  }
  if(s && local_ui_stage==3 && s->phase==FZERO_VS_COUNTDOWN) {
    static const unsigned ticks[]={20,34,68};
    for(unsigned shot=0;shot<3;++shot)if(!(local_ui_arrival_shots&(1u<<shot)) && s->tick>=ticks[shot]) {
      local_ui_arrival_shots|=1u<<shot;
      fprintf(stderr,"local-ui-arrival: tick=%llu offset=%u countdown=%u\n",(unsigned long long)s->tick,
          FzeroCameraArrivalOffset((unsigned)s->tick),s->countdown);
      snprintf(label,sizeof(label),"arrival-%u",shot);local_ui_images(p,directory,label);return;
    }
  }
  if(s && local_ui_stage==3 && s->phase==FZERO_VS_RACING && getenv("FZERO_TEST_LOCAL_UI_DRIVE")) {
    static bool traffic_captured;
    if(getenv("FZERO_TEST_TRAFFIC_VISIBLE") && !traffic_captured && s->race_tick>=300) {
      traffic_captured=true;local_ui_images(p,directory,"traffic");return;
    }
    static const unsigned ticks[]={15,45,80,115,175,215};
    for(unsigned shot=0;shot<6;++shot)if(!(local_ui_drive_captured&(1u<<shot)) && s->race_tick>=ticks[shot]) {
      if(shot==4 && ((s->racers[0].actor.steering_history!=0 && s->racers[0].actor.steering_history!=10) ||
          (s->racers[1].actor.steering_history!=0 && s->racers[1].actor.steering_history!=10) ||
          !s->racers[0].actor.animation_transition || !s->racers[1].actor.animation_transition))return;
      if(shot==1 || shot==3)for(unsigned seat=0;seat<2;++seat) {
        unsigned expected=((shot==1)^seat)?0x80:0x81;
        if(s->racers[seat].actor.steering_history!=expected)Die("UI shoulder input did not reach native animation");
      }
      for(unsigned seat=0;seat<2;++seat) {
        const FzeroActor *a=&s->racers[seat].actor;
        if(!a->speed || (a->x==local_ui_drive_start[seat][0] && a->y==local_ui_drive_start[seat][1]))Die("UI drive did not move");
        fprintf(stderr,"local-ui-drive: shot=%u seat=%u race=%llu machine=%u pos=%u,%u speed=%u DD=%02x DE=%02x DF=%02x status=%u\n",
          shot,seat,(unsigned long long)s->race_tick,a->machine,a->x,a->y,a->speed,a->steering_history,a->animation_scale,a->animation_transition,s->racers[seat].status);
      }
      local_ui_drive_captured|=1u<<shot;
      snprintf(label,sizeof(label),"drive-%u",shot);local_ui_images(p,directory,label);return;
    }
  }
  if(!s) {
    if(!directory || title_captured || g_ram[0x54] || g_ram[0x55]!=1 || g_ram[0xc3] ||
       (g_snes->ppu->inidisp&15)!=15)return;
    /* Scene flags precede the first complete native title image. Wait for
     * the actual Grand Prix lettering rather than capturing a black frame
     * containing only our added row. */
    const uint32_t *frame=FzeroHdFrame();unsigned scale=FzeroHdScale();
    if(!frame)frame=(const uint32_t *)p->pixels;
    int width=p->logical_width*(int)scale;unsigned ink=0;
    for(int y=104*(int)scale;y<120*(int)scale;++y)
      for(int x=(84+p->viewport.extra)*(int)scale;x<(124+p->viewport.extra)*(int)scale;++x) {
        uint32_t color=frame[(size_t)y*width+x];
        if(((color>>8)&255)>((color>>16)&255)+16)++ink;
      }
    if(ink<64*scale*scale)return;
    snprintf(label,sizeof(label),"main-menu");title_captured=true;
  } else {
    if(local_ui_stage!=3 || !getenv("FZERO_TEST_LOCAL_UI_RESIZE"))return;
    if(s->phase==FZERO_VS_COUNTDOWN && s->countdown<=FZERO_VERSUS_REV_COUNTDOWN)
      local_ui_countdowns|=1u<<((s->countdown+59)/60);
    if(s->phase==FZERO_VS_RACING)local_ui_racing=true;
    if(!local_ui_resizes || s->tick<local_ui_resize_ticks[local_ui_resizes-1]+5 ||
       (local_ui_captured&(1u<<(local_ui_resizes-1))))return;
    int ww,wh,pw,ph;
    SDL_GetWindowSize(p->window,&ww,&wh);SDL_GetWindowSizeInPixels(p->window,&pw,&ph);
    if(ww!=local_ui_sizes[local_ui_resizes-1][0] || wh!=local_ui_sizes[local_ui_resizes-1][1] ||
       p->drawable_width!=pw || p->drawable_height!=ph)return;
    unsigned scale=FzeroHdScale();
    size_t count=(size_t)p->logical_width*448*scale*scale;
    uint32_t *reference=malloc(count*sizeof(*reference));
    if(!reference || !FzeroLocalDraw(reference,count,p->viewport,1,scale))Die("UI frame lost its course assets after resize");
    const uint32_t *actual=FzeroHdFrame();if(!actual)actual=(const uint32_t *)p->pixels;
    int width=p->logical_width*(int)scale;
    for(unsigned pane=0;pane<2;++pane)for(int y=3*(int)scale;y<39*(int)scale;++y)
      for(int x=8*(int)scale;x<80*(int)scale;++x) {
        size_t index=(size_t)(y+pane*224*scale)*width+x;
        if(actual[index]!=reference[index])Die("UI displayed stale HUD after resize");
      }
    free(reference);local_ui_captured|=1u<<(local_ui_resizes-1);
    fprintf(stderr,"local-ui-image: resize=%u output=%dx%d width=%d scale=%u tick=%llu phase=%u countdown=%u pause=%u\n",
        local_ui_resizes,p->drawable_width,p->drawable_height,p->logical_width,scale,
        (unsigned long long)s->tick,s->phase,s->countdown,s->pause);
    snprintf(label,sizeof(label),"resize-%u-phase-%u-count-%u",local_ui_resizes,s->phase,(s->countdown+59)/60);
  }
  local_ui_images(p,directory,label);
}
#else
static void local_ui_attach(void) {}
static bool local_ui_tick(FzeroLocalControls *controls,SDL_Window *window) {(void)controls;(void)window;return false;}
static void local_ui_presented(const FzeroPresenter *p) {(void)p;}
#endif
