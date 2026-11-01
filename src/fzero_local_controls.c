#include "fzero_local_controls.h"
#include "fzero_gamepad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {NO_DEVICE=-2,KEYBOARD=-1,MAX_PADS=16};
typedef struct LocalPad {SDL_GameController *handle;SDL_JoystickID id;bool start;} LocalPad;
struct FzeroLocalControls {
  LocalPad pads[MAX_PADS];
  FzeroVersusConfig config;
  int seat[FZERO_MAX_PLAYERS];
  int source[FZERO_MAX_PLAYERS];
  char guid[FZERO_MAX_PLAYERS][40];
  bool assigned;
  uint32_t previous[FZERO_MAX_PLAYERS],input[FZERO_MAX_PLAYERS],keyboard;
  bool setup,ready[FZERO_MAX_PLAYERS],release[FZERO_MAX_PLAYERS],scan,scripted,back;
  unsigned menu_tag;
  FzeroLocalPage page;
  unsigned cup,menu_ticks,selection_tick[FZERO_MAX_PLAYERS];
  int slide[FZERO_MAX_PLAYERS];
};
FzeroLocalControls *FzeroLocalControlsCreate(FzeroVersusConfig config,bool scripted) {
  if(FzeroVersusPlayers(config)<2 || FzeroVersusPlayers(config)>FZERO_MAX_PLAYERS)return NULL;
  FzeroLocalControls *c=calloc(1,sizeof(*c));if(!c)return NULL;
  config.player_count=(uint8_t)FzeroVersusPlayers(config);c->config=config;FzeroLocalCourseLocation(config.course,&c->cup,NULL);for(unsigned p=0;p<FZERO_MAX_PLAYERS;++p)c->seat[p]=NO_DEVICE;c->setup=!scripted;c->scripted=scripted;c->scan=true;return c;
}
void FzeroLocalControlsAssignPlayers(FzeroLocalControls *c,const int sources[],const char guids[][40],unsigned count) {
  if(!c || c->scripted || count>FZERO_MAX_PLAYERS)return;
  c->assigned=true;c->scan=true;
  for(unsigned p=0;p<FZERO_MAX_PLAYERS;++p) {
    if(p>=count) {c->source[p]=0;continue;}
    c->source[p]=sources[p];snprintf(c->guid[p],sizeof(c->guid[p]),"%s",guids[p]);
    c->seat[p]=p<c->config.player_count && sources[p]==1?KEYBOARD:NO_DEVICE;
    c->previous[p]=c->input[p]=0;c->release[p]=true;
  }
}
void FzeroLocalControlsDestroy(FzeroLocalControls *c) {
  if(!c)return;
  for(unsigned i=0;i<MAX_PADS;++i)if(c->pads[i].handle)SDL_GameControllerClose(c->pads[i].handle);
  free(c);
}
void FzeroLocalControlsEvent(FzeroLocalControls *c,const SDL_Event *event) {
  if(!c)return;
  if(event->type==SDL_CONTROLLERDEVICEADDED || event->type==SDL_CONTROLLERDEVICEREMOVED ||
      event->type==SDL_CONTROLLERDEVICEREMAPPED)c->scan=true;
#if SNESRECOMP_SDL3
  bool focus_lost=event->type==SDL_EVENT_WINDOW_FOCUS_LOST;
#else
  bool focus_lost=event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST;
#endif
  (void)focus_lost; /* Multiplayer keeps advancing when focus changes. */
}
static void scan_pads(FzeroLocalControls *c) {
  for(unsigned i=0;i<MAX_PADS;++i)if(c->pads[i].handle && !SDL_GameControllerGetAttached(c->pads[i].handle)) {
    SDL_GameControllerClose(c->pads[i].handle);c->pads[i].handle=NULL;c->pads[i].start=false;
    for(unsigned p=0;p<c->config.player_count;++p)if(c->seat[p]==(int)i) {
      c->seat[p]=NO_DEVICE;c->ready[p]=false;c->input[p]=0;c->release[p]=true;
      if(c->setup)c->page=FZERO_LOCAL_MACHINES;
      if(FzeroLocalSession())FzeroLocalControlChanged();
    }
  }
#if SNESRECOMP_SDL3
  int count=0;SDL_JoystickID *ids=SDL_GetGamepads(&count);
#else
  int count=SDL_NumJoysticks();
#endif
  for(int n=0;n<count;++n) {
#if SNESRECOMP_SDL3
    SDL_JoystickID id=ids[n];
#else
    if(!SDL_IsGameController(n))continue;
    SDL_JoystickID id=SDL_JoystickGetDeviceInstanceID(n);
#endif
    bool known=false;int slot=-1;
    for(unsigned i=0;i<MAX_PADS;++i) {
      if(c->pads[i].handle && c->pads[i].id==id)known=true;
      if(!c->pads[i].handle && slot<0)slot=(int)i;
    }
    if(known || slot<0)continue;
#if SNESRECOMP_SDL3
    SDL_GameController *pad=SDL_OpenGamepad(id);
#else
    SDL_GameController *pad=SDL_GameControllerOpen(n);
#endif
    if(pad)c->pads[slot]=(LocalPad){pad,id,false};
  }
#if SNESRECOMP_SDL3
  SDL_free(ids);
#endif
  c->scan=false;
  if(c->assigned)for(unsigned p=0;p<c->config.player_count;++p)if(c->source[p]==1 && c->seat[p]==NO_DEVICE) {
    c->seat[p]=KEYBOARD;c->release[p]=true;
  }
  if(c->assigned)for(unsigned p=0;p<c->config.player_count;++p)if(c->source[p]==2 && c->seat[p]==NO_DEVICE) {
    for(unsigned i=0;i<MAX_PADS;++i)if(c->pads[i].handle && true) {
      bool used=false;for(unsigned other=0;other<c->config.player_count;++other)if(c->seat[other]==(int)i)used=true;
      if(used)continue;
      char guid[40];
#if SNESRECOMP_SDL3
      SDL_GUIDToString(SDL_GetJoystickGUID(SDL_GetGamepadJoystick(c->pads[i].handle)),guid,sizeof(guid));
#else
      SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(c->pads[i].handle)),guid,sizeof(guid));
#endif
      if(c->guid[p][0] && SDL_strcasecmp(guid,c->guid[p]))continue;
      c->seat[p]=(int)i;c->release[p]=true;break;
    }
  }
}
static void join(FzeroLocalControls *c,int device) {
  for(unsigned p=0;p<c->config.player_count;++p)if(c->seat[p]==device)return;
  for(unsigned p=0;p<c->config.player_count;++p)if(c->seat[p]==NO_DEVICE) {
    c->seat[p]=device;c->ready[p]=false;c->release[p]=true;break;
  }
}
static unsigned menu_tag(void) {
  const FzeroVersusSnapshot *s=FzeroLocalSession()?FzeroVersusCompleted(FzeroLocalSession()):NULL;
  return s && s->phase==FZERO_VS_RESULTS?2:FzeroLocalMenuOwner()?1:0;
}
static void release_inputs(FzeroLocalControls *c) {
  for(unsigned p=0;p<FZERO_MAX_PLAYERS;++p)c->release[p]=true;
  memset(c->previous,0,sizeof(c->previous));memset(c->input,0,sizeof(c->input));
}
static unsigned activate(FzeroLocalControls *c,unsigned choice) {
  FzeroVersus *race=FzeroLocalSession();
  unsigned owner=FzeroLocalMenuOwner();
  bool live=FzeroVersusCompleted(race)->phase!=FZERO_VS_RESULTS;
  FzeroLocalSetMenuOwner(0);
  if(live) {
    if(choice==0) {
      if(owner && owner<=c->config.player_count) {c->release[owner-1]=true;c->input[owner-1]=0;}
      return 0;
    }
    --choice;
  }
  if(choice==0) {
    if(live)FzeroVersusRestart(race);else FzeroVersusRematch(race);
    for(unsigned p=0;p<c->config.player_count;++p)FzeroVersusReady(race,p,true);
  } else if(choice==1) {
    c->setup=true;c->page=FZERO_LOCAL_MACHINES;FzeroLocalCourseLocation(c->config.course,&c->cup,NULL);memset(c->ready,0,sizeof(c->ready));
    release_inputs(c);return FZERO_LOCAL_CHANGE;
  } else {release_inputs(c);return FZERO_LOCAL_MAIN;}
  FzeroLocalSetMenuSelection(0);FzeroLocalControlChanged();release_inputs(c);return 0;
}
void FzeroLocalControlsBack(FzeroLocalControls *c) {if(c)c->back=true;}
static unsigned setup_back(FzeroLocalControls *c) {
  if(c->page==FZERO_LOCAL_COURSE)c->page=FZERO_LOCAL_CUP;
  else if(c->page==FZERO_LOCAL_COUNT) {c->page=FZERO_LOCAL_MACHINES;
    c->config.player_count=2;
  }
  else if(c->page==FZERO_LOCAL_CUP) {c->page=FZERO_LOCAL_MACHINES;memset(c->ready,0,sizeof(c->ready));}
  else {release_inputs(c);return FZERO_LOCAL_MAIN;}
  release_inputs(c);return 0;
}
unsigned FzeroLocalControlsUpdateKeyboards(FzeroLocalControls *c,const uint32_t keyboards[FZERO_MAX_PLAYERS]) {
  if(!c || !keyboards)return 0;
  uint32_t keyboard=keyboards[FzeroLocalControlsKeyboardPlayer(c)];
  ++c->menu_ticks;
  if(!c->setup && FzeroLocalSession())c->config=FzeroLocalConfig();
  if(c->scan)scan_pads(c);
  if((keyboard&8) && !(c->keyboard&8))join(c,KEYBOARD);
  c->keyboard=keyboard;
  for(unsigned i=0;i<MAX_PADS;++i)if(c->pads[i].handle) {
    bool start=SDL_GameControllerGetButton(c->pads[i].handle,SDL_CONTROLLER_BUTTON_START)!=0;
    if(start && !c->pads[i].start)join(c,(int)i);
    c->pads[i].start=start;
  }
  unsigned tag=menu_tag();
  if(tag!=c->menu_tag) {c->menu_tag=tag;FzeroLocalSetMenuSelection(0);}
  if(c->back) {
    c->back=false;
    if(c->setup)return setup_back(c);
    if(tag==2) {release_inputs(c);return FZERO_LOCAL_MAIN;}
    if(FzeroLocalSession())FzeroLocalSetMenuOwner(tag?0:FzeroLocalControlsKeyboardPlayer(c)+1);
    return 0;
  }
  for(unsigned p=0;p<c->config.player_count;++p) {
    int device=c->seat[p];
    uint32_t held=device==KEYBOARD?keyboards[p]:device>=0?FzeroGamepadReadPlayer(c->pads[device].handle,p):0;
    if(c->release[p]) {if(!held)c->release[p]=false;held=0;}
    uint32_t pressed=held&~c->previous[p];c->previous[p]=held;c->input[p]=held;
    if(c->setup) {
      if(c->page==FZERO_LOCAL_MACHINES) {
        if(p==0 && !c->ready[0] && (pressed&0x200)) {
          c->page=FZERO_LOCAL_COUNT;release_inputs(c);return 0;
        }
        if(pressed&1) {
          if(c->ready[p])c->ready[p]=false;
          else return setup_back(c);
        }
        bool sliding=c->slide[p] && c->menu_ticks-c->selection_tick[p]<12;
        if(!c->ready[p] && !sliding && (pressed&0xf4)) {
          c->config.machine[p]=(uint8_t)FzeroMachineMove(c->config.machine[p],(uint16_t)pressed,&c->slide[p]);
          c->selection_tick[p]=c->menu_ticks;
        }
        if(!sliding && (pressed&0x108))c->ready[p]=true;
      } else if(c->page==FZERO_LOCAL_COUNT) {
        if(p)continue;
        if(pressed&1) {c->page=FZERO_LOCAL_MACHINES;release_inputs(c);return 0;}
        if(pressed&0xf0) {
          unsigned n=c->config.player_count;
          n=(pressed&0x50)?(n==2?4:n-1):(n==4?2:n+1);
          c->config.player_count=(uint8_t)n;c->scan=true;memset(c->ready,0,sizeof(c->ready));
          for(unsigned i=n;i<FZERO_MAX_PLAYERS;++i) {c->seat[i]=NO_DEVICE;c->input[i]=0;}
        }
        if(pressed&0x108) {c->page=FZERO_LOCAL_MACHINES;release_inputs(c);return 0;}
      } else {
        if(pressed&1)return setup_back(c);
        if(pressed&0x30) {
          int delta=pressed&0x10?-1:1;
          if(c->page==FZERO_LOCAL_CUP) {
            unsigned count=FzeroLocalCupCount();c->cup=(c->cup+count+delta)%count;
          } else {
            unsigned order=0,count=FzeroLocalCourseCount(c->cup);
            FzeroLocalCourseLocation(c->config.course,NULL,&order);
            c->config.course=(uint16_t)FzeroLocalCourseId(c->cup,(order+count+delta)%count);
          }
        }
        if(pressed&0x108) {
          if(c->page==FZERO_LOCAL_CUP) {
            unsigned previous=0;FzeroLocalCourseLocation(c->config.course,&previous,NULL);
            if(previous!=c->cup)c->config.course=(uint16_t)FzeroLocalCourseId(c->cup,0);
            c->page=FZERO_LOCAL_COURSE;release_inputs(c);return 0;
          }
          c->setup=false;release_inputs(c);return FZERO_LOCAL_START;
        }
        /* One shared cursor move per frame even if both pads press. */
        if(pressed)return 0;
      }
    } else if(FzeroLocalSession()) {
      bool results=FzeroVersusCompleted(FzeroLocalSession())->phase==FZERO_VS_RESULTS;
      unsigned owner=FzeroLocalMenuOwner();
      if(!results && (pressed&8) && !owner) {
        FzeroLocalSetMenuOwner(p+1);c->input[p]=0;continue;
      }
      if(!results && owner==p+1)c->input[p]=0;
      if((!results && owner!=p+1) || !FzeroLocalMenuReady())continue;
      if(!results && (pressed&1)) {FzeroLocalSetMenuOwner(0);continue;}
      unsigned count=results?3:4,choice=FzeroLocalMenuSelection();
      if(choice>=count)choice=0;
      if(pressed&0x30) {choice=(choice+((pressed&0x10)?count-1:1))%count;FzeroLocalSetMenuSelection(choice);}
      if(pressed&0x109)return activate(c,choice);

    }
  }
  bool all_ready=true;for(unsigned p=0;p<c->config.player_count;++p)if(!c->ready[p] || c->seat[p]==NO_DEVICE)all_ready=false;
  if(c->setup && all_ready) {
    if(c->page==FZERO_LOCAL_MACHINES) {c->page=FZERO_LOCAL_CUP;release_inputs(c);}
  }
  return 0;
}
/* Compatibility entry point: one supplied key word may intentionally drive
 * every explicitly configured keyboard seat. Automatic joining remains one seat. */
unsigned FzeroLocalControlsUpdate(FzeroLocalControls *c,uint32_t keyboard) {
  uint32_t keyboards[FZERO_MAX_PLAYERS];
  for(unsigned p=0;p<FZERO_MAX_PLAYERS;++p)keyboards[p]=keyboard;
  return FzeroLocalControlsUpdateKeyboards(c,keyboards);
}
FzeroVersusConfig FzeroLocalControlsConfig(const FzeroLocalControls *c) {return c->config;}
uint32_t FzeroLocalControlsInput(const FzeroLocalControls *c) {return c?c->input[0]|(c->input[1]<<12):0;}
bool FzeroLocalControlsSetup(const FzeroLocalControls *c) {return c && c->setup;}
FzeroLocalPage FzeroLocalControlsPage(const FzeroLocalControls *c) {return c?c->page:FZERO_LOCAL_MACHINES;}
unsigned FzeroLocalControlsKeyboardPlayer(const FzeroLocalControls *c) {
  if(c) {
    for(unsigned p=0;p<c->config.player_count;++p)if(c->seat[p]==KEYBOARD)return p;
    for(unsigned p=0;p<c->config.player_count;++p)if(c->seat[p]==NO_DEVICE)return p;
  }
  return 0;
}
void FzeroLocalControlsLoaded(FzeroLocalControls *c) {
  if(!c)return;
  c->config=FzeroLocalConfig();c->setup=false;c->menu_tag=menu_tag();c->scan=true;
  memset(c->previous,0,sizeof(c->previous));memset(c->input,0,sizeof(c->input));
  for(unsigned p=0;p<FZERO_MAX_PLAYERS;++p)c->release[p]=true;
}
void FzeroLocalControlsReopen(FzeroLocalControls *c) {
  if(!c)return;
  c->setup=true;c->page=FZERO_LOCAL_MACHINES;FzeroLocalCourseLocation(c->config.course,&c->cup,NULL);memset(c->ready,0,sizeof(c->ready));
  memset(c->previous,0,sizeof(c->previous));memset(c->input,0,sizeof(c->input));
  for(unsigned p=0;p<FZERO_MAX_PLAYERS;++p)c->release[p]=true;
}
/* The native car page is a 256x224 composition. Give each human a complete
 * copy, using its original car order, portraits, frame and menu alphabet. */
void FzeroLocalControlsDraw(const FzeroLocalControls *c,uint32_t *pixels,int width,int height) {
  if(!c || !pixels || !c->setup || width<256 || height<448)return;
  const FzeroNativeUi *ui=FzeroLocalUi();if(!ui)return;
  FzeroUiCanvas canvas={pixels,width,height,1};int center=width/2,left=center-128;
  FzeroUiRect(canvas,0,0,width,height,0xff000000);
  if(c->page==FZERO_LOCAL_COUNT) {
    char label[32];snprintf(label,sizeof(label),"%u PLAYERS",c->config.player_count);
    FzeroUiCenter(ui,canvas,152,"SELECT PLAYERS",FZERO_UI_MENU,FZERO_UI_YELLOW);
    FzeroUiCenter(ui,canvas,192,label,FZERO_UI_MENU,FZERO_UI_CYAN);return;
  }
  if(c->page==FZERO_LOCAL_MACHINES) {
    unsigned cols=c->config.player_count>2?2:1;int pw=width/(int)cols;
    uint32_t *scratch=calloc((size_t)pw*224,4);if(!scratch)return;
    for(unsigned p=0;p<c->config.player_count;++p) {
      memset(scratch,0,(size_t)pw*224*4);
      FzeroUiCanvas pane={scratch,pw,224,1};
      FzeroUiMachineSelectScroll(ui,pane,c->config.machine[p],c->menu_ticks-c->selection_tick[p],p,
          c->seat[p]!=NO_DEVICE,c->ready[p],c->slide[p]);
      if(!p)FzeroUiText(ui,pane,8,208,"Y: SELECT PLAYERS",FZERO_UI_SMALL,FZERO_UI_YELLOW);
      unsigned x=p%cols*pw,y=p/cols*224;
      for(unsigned row=0;row<224;++row)memcpy(pixels+(y+row)*width+x,scratch+row*pw,(size_t)pw*4);
    }
    free(scratch);return;
  }
  /* One shared native-style league/course page, after both cars are chosen. */
  int top=112;
  FzeroUiCenter(ui,canvas,top+8,"VS MODE",FZERO_UI_MENU,FZERO_UI_YELLOW);
  bool cup=c->page==FZERO_LOCAL_CUP;
  FzeroUiFrame(ui,canvas,left+8,top+48,240,168);
  FzeroUiCenter(ui,canvas,top+32,cup?"SELECT LEAGUE":"SELECT COURSE",FZERO_UI_MENU,FZERO_UI_YELLOW);
  unsigned selected=c->cup;
  if(!cup) {
    FzeroLocalCourseLocation(c->config.course,NULL,&selected);
    char heading[25];snprintf(heading,sizeof(heading),"%.24s",FzeroLocalCupName(c->cup));
    FzeroUiCenter(ui,canvas,top+56,heading,FZERO_UI_SMALL,FZERO_UI_YELLOW);
  }
  unsigned count=cup?FzeroLocalCupCount():FzeroLocalCourseCount(c->cup);
  unsigned first=selected<5?0:selected-4;
  for(unsigned row=0;row<5 && first+row<count;++row) {
    unsigned item=first+row;bool chosen=item==selected;
    int y=top+80+(int)row*24;
    if(chosen)FzeroUiTile(ui,canvas,left+24,y+4,0x2f,FZERO_UI_YELLOW,true,false);
    const char *name=cup?FzeroLocalCupName(item):FzeroLocalCourseName(FzeroLocalCourseId(c->cup,item));
    unsigned length=(unsigned)strlen(name),offset=0;
    if(chosen && length>24) {
      unsigned step=(c->menu_ticks/20)%(length-24+12);offset=step<6?0:step-6;
      if(offset>length-24)offset=length-24;
    }
    char label[25];snprintf(label,sizeof(label),"%.24s",name+offset);
    FzeroUiText(ui,canvas,left+40,y,label,FZERO_UI_MENU,chosen?FZERO_UI_CYAN:FZERO_UI_GREY);
  }
}

void FzeroLocalControlsInputs(const FzeroLocalControls *c,uint16_t inputs[FZERO_MAX_PLAYERS]) {
  for(unsigned i=0;i<FZERO_MAX_PLAYERS;++i)inputs[i]=c && i<c->config.player_count?(uint16_t)c->input[i]:0;
}

void FzeroLocalControlsAssign(FzeroLocalControls *c,const int sources[2],const char guids[2][40]) {
  FzeroLocalControlsAssignPlayers(c,sources,guids,2);
}
