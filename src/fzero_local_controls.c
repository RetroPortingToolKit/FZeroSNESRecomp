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
  int seat[2];
  uint32_t previous[2],input[2],keyboard;
  bool setup,ready[2],release[2],scan,scripted,back;
  unsigned menu_tag;
  FzeroLocalPage page;
  unsigned cup,menu_ticks,selection_tick[2];
  int slide[2];
};
FzeroLocalControls *FzeroLocalControlsCreate(FzeroVersusConfig config,bool scripted) {
  FzeroLocalControls *c=calloc(1,sizeof(*c));if(!c)return NULL;
  c->config=config;FzeroLocalCourseLocation(config.course,&c->cup,NULL);c->seat[0]=c->seat[1]=NO_DEVICE;c->setup=!scripted;c->scripted=scripted;c->scan=true;return c;
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
  if(focus_lost && !c->setup && FzeroLocalSession()) {
    FzeroVersusPause(FzeroLocalSession(),FZERO_VS_PAUSE_FOCUS);
    FzeroLocalControlChanged();
    for(unsigned i=0;i<2;++i){c->input[i]=0;c->release[i]=true;}
  }
}
static void scan_pads(FzeroLocalControls *c) {
  for(unsigned i=0;i<MAX_PADS;++i)if(c->pads[i].handle && !SDL_GameControllerGetAttached(c->pads[i].handle)) {
    SDL_GameControllerClose(c->pads[i].handle);c->pads[i].handle=NULL;c->pads[i].start=false;
    for(unsigned p=0;p<2;++p)if(c->seat[p]==(int)i) {
      c->seat[p]=NO_DEVICE;c->ready[p]=false;c->input[p]=0;c->release[p]=true;
      if(c->setup)c->page=FZERO_LOCAL_MACHINES;
      if(FzeroLocalSession())FzeroVersusPause(FzeroLocalSession(),FZERO_VS_PAUSE_DEVICE1<<p);
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
}
static void join(FzeroLocalControls *c,int device) {
  for(unsigned p=0;p<2;++p)if(c->seat[p]==device)return;
  for(unsigned p=0;p<2;++p)if(c->seat[p]==NO_DEVICE) {
    c->seat[p]=device;c->ready[p]=false;c->release[p]=true;break;
  }
}
static unsigned menu_tag(void) {
  const FzeroVersusSnapshot *s=FzeroLocalSession()?FzeroVersusCompleted(FzeroLocalSession()):NULL;
  return s?(s->pause?1:s->phase==FZERO_VS_RESULTS?2:0):0;
}
static void release_inputs(FzeroLocalControls *c) {
  c->release[0]=c->release[1]=true;
  c->previous[0]=c->previous[1]=c->input[0]=c->input[1]=0;
}
static unsigned activate(FzeroLocalControls *c,unsigned choice) {
  FzeroVersus *race=FzeroLocalSession();
  if(c->menu_tag==1 && choice==0) {
    if(c->seat[0]==NO_DEVICE || c->seat[1]==NO_DEVICE)return 0;
    FzeroVersusResume(race);
  } else if(choice==(c->menu_tag==1?1:0)) {
    if(c->menu_tag==1) {FzeroVersusEndRace(race);FzeroVersusResume(race);}
    FzeroVersusRematch(race);FzeroVersusReady(race,0,true);FzeroVersusReady(race,1,true);
  } else if(choice==(c->menu_tag==1?2:1)) {
    c->setup=true;c->page=FZERO_LOCAL_MACHINES;FzeroLocalCourseLocation(c->config.course,&c->cup,NULL);c->ready[0]=c->ready[1]=false;
    release_inputs(c);return FZERO_LOCAL_CHANGE;
  } else {release_inputs(c);return FZERO_LOCAL_MAIN;}
  FzeroLocalSetMenuSelection(0);FzeroLocalControlChanged();release_inputs(c);return 0;
}
void FzeroLocalControlsBack(FzeroLocalControls *c) {if(c)c->back=true;}
static unsigned setup_back(FzeroLocalControls *c) {
  if(c->page==FZERO_LOCAL_COURSE)c->page=FZERO_LOCAL_CUP;
  else if(c->page==FZERO_LOCAL_CUP) {c->page=FZERO_LOCAL_MACHINES;c->ready[0]=c->ready[1]=false;}
  else {release_inputs(c);return FZERO_LOCAL_MAIN;}
  release_inputs(c);return 0;
}
unsigned FzeroLocalControlsUpdate(FzeroLocalControls *c,uint32_t keyboard) {
  if(!c)return 0;
  ++c->menu_ticks;
  if(!c->setup && FzeroLocalSession())c->config=FzeroLocalConfig();
  if(c->scan)scan_pads(c);
  if(!c->scripted && !c->setup && FzeroLocalSession()) {
    unsigned missing=(c->seat[0]==NO_DEVICE?FZERO_VS_PAUSE_DEVICE1:0)|
        (c->seat[1]==NO_DEVICE?FZERO_VS_PAUSE_DEVICE2:0);
    if(missing&~FzeroVersusCompleted(FzeroLocalSession())->pause) {
      FzeroVersusPause(FzeroLocalSession(),missing);FzeroLocalControlChanged();
      c->release[0]=c->release[1]=true;c->input[0]=c->input[1]=0;
    }
  }
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
    if(tag==1)return activate(c,0);
    if(FzeroLocalSession()) {FzeroVersusPause(FzeroLocalSession(),FZERO_VS_PAUSE_MANUAL);FzeroLocalControlChanged();release_inputs(c);}
    return 0;
  }
  for(unsigned p=0;p<2;++p) {
    int device=c->seat[p];
    uint32_t held=device==KEYBOARD?keyboard:device>=0?FzeroGamepadReadPlayer(c->pads[device].handle,p):0;
    if(c->release[p]) {if(!held)c->release[p]=false;held=0;}
    uint32_t pressed=held&~c->previous[p];c->previous[p]=held;c->input[p]=held;
    if(c->setup) {
      if(c->page==FZERO_LOCAL_MACHINES) {
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
    } else if(FzeroLocalSession() && tag && FzeroLocalMenuReady()) {
      unsigned count=tag==1?4:3,choice=FzeroLocalMenuSelection();
      if(choice>=count)choice=0;
      if(pressed&0x30) {choice=(choice+((pressed&0x10)?count-1:1))%count;FzeroLocalSetMenuSelection(choice);}
      if(pressed&0x109)return activate(c,choice);

    }
  }
  if(c->setup && c->ready[0] && c->ready[1]) {
    if(c->page==FZERO_LOCAL_MACHINES) {c->page=FZERO_LOCAL_CUP;release_inputs(c);}
  }
  return 0;
}
FzeroVersusConfig FzeroLocalControlsConfig(const FzeroLocalControls *c) {return c->config;}
uint32_t FzeroLocalControlsInput(const FzeroLocalControls *c) {return c?c->input[0]|(c->input[1]<<12):0;}
bool FzeroLocalControlsSetup(const FzeroLocalControls *c) {return c && c->setup;}
FzeroLocalPage FzeroLocalControlsPage(const FzeroLocalControls *c) {return c?c->page:FZERO_LOCAL_MACHINES;}
unsigned FzeroLocalControlsKeyboardPlayer(const FzeroLocalControls *c) {
  if(c) {
    for(unsigned p=0;p<2;++p)if(c->seat[p]==KEYBOARD)return p;
    for(unsigned p=0;p<2;++p)if(c->seat[p]==NO_DEVICE)return p;
  }
  return 0;
}
void FzeroLocalControlsLoaded(FzeroLocalControls *c) {
  if(!c)return;
  c->config=FzeroLocalConfig();c->setup=false;c->menu_tag=menu_tag();
  c->previous[0]=c->previous[1]=c->input[0]=c->input[1]=0;
  c->release[0]=c->release[1]=true;
}
void FzeroLocalControlsReopen(FzeroLocalControls *c) {
  if(!c)return;
  c->setup=true;c->page=FZERO_LOCAL_MACHINES;FzeroLocalCourseLocation(c->config.course,&c->cup,NULL);c->ready[0]=c->ready[1]=false;
  c->previous[0]=c->previous[1]=c->input[0]=c->input[1]=0;
  c->release[0]=c->release[1]=true;
}
/* The native car page is a 256x224 composition. Give each human a complete
 * copy, using its original car order, portraits, frame and menu alphabet. */
void FzeroLocalControlsDraw(const FzeroLocalControls *c,uint32_t *pixels,int width,int height) {
  if(!c || !pixels || !c->setup || width<256 || height<448)return;
  const FzeroNativeUi *ui=FzeroLocalUi();if(!ui)return;
  FzeroUiCanvas canvas={pixels,width,height,1};int center=width/2,left=center-128;
  FzeroUiRect(canvas,0,0,width,height,0xff000000);
  if(c->page==FZERO_LOCAL_MACHINES) {
    for(unsigned p=0;p<2;++p) {
      FzeroUiCanvas pane={pixels+(size_t)p*224*width,width,224,1};
      FzeroUiMachineSelectScroll(ui,pane,c->config.machine[p],c->menu_ticks-c->selection_tick[p],p,
          c->seat[p]!=NO_DEVICE,c->ready[p],c->slide[p]);
    }
    return;
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
