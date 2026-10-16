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
  bool setup,ready[2],release[2],scan,scripted;
};
FzeroLocalControls *FzeroLocalControlsCreate(FzeroVersusConfig config,bool scripted) {
  FzeroLocalControls *c=calloc(1,sizeof(*c));if(!c)return NULL;
  c->config=config;c->seat[0]=c->seat[1]=NO_DEVICE;c->setup=!scripted;c->scripted=scripted;c->scan=true;return c;
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
  if(focus_lost && FzeroLocalSession()) {
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
unsigned FzeroLocalControlsUpdate(FzeroLocalControls *c,uint32_t keyboard) {
  if(!c)return 0;
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
  unsigned action=0;
  for(unsigned p=0;p<2;++p) {
    int device=c->seat[p];
    uint32_t held=device==KEYBOARD?keyboard:device>=0?FzeroGamepadReadPlayer(c->pads[device].handle,p):0;
    if(c->release[p]) {if(!held)c->release[p]=false;held=0;}
    uint32_t pressed=held&~c->previous[p];c->previous[p]=held;c->input[p]=held;
    if(c->setup) {
      if(pressed&0x30) {c->config.machine[p]=(uint8_t)((c->config.machine[p]+((pressed&0x10)?3:1))%4);c->ready[p]=false;}
      if(!p && (pressed&0xc0)) {
        c->config.course=(uint8_t)((c->config.course+((pressed&0x40)?14:1))%15);
        c->ready[0]=c->ready[1]=false;
      }
      if(pressed&9)c->ready[p]=!c->ready[p];
    } else if(FzeroLocalSession()) {
      const FzeroVersusSnapshot *v=FzeroVersusCompleted(FzeroLocalSession());
      if(v->phase==FZERO_VS_RESULTS) {
        if(pressed&1) {
          FzeroVersusRematch(FzeroLocalSession());FzeroVersusReady(FzeroLocalSession(),0,true);FzeroVersusReady(FzeroLocalSession(),1,true);
          FzeroLocalControlChanged();c->release[0]=c->release[1]=true;c->input[0]=c->input[1]=0;
        }
        if(pressed&2){c->setup=true;c->ready[0]=c->ready[1]=false;action|=FZERO_LOCAL_CHANGE;}
      } else if(v->pause) {
        if((pressed&8) && c->seat[0]!=NO_DEVICE && c->seat[1]!=NO_DEVICE) {
          FzeroVersusResume(FzeroLocalSession());c->release[0]=c->release[1]=true;c->input[0]=c->input[1]=0;
          FzeroLocalControlChanged();
        }
        if(pressed&0x200) {FzeroVersusEndRace(FzeroLocalSession());FzeroVersusResume(FzeroLocalSession());FzeroLocalControlChanged();}
      }
    }
  }
  if(c->setup && c->ready[0] && c->ready[1]) {
    c->setup=false;c->release[0]=c->release[1]=true;c->input[0]=c->input[1]=0;action|=FZERO_LOCAL_START;
  }
  return action;
}
FzeroVersusConfig FzeroLocalControlsConfig(const FzeroLocalControls *c) {return c->config;}
uint32_t FzeroLocalControlsInput(const FzeroLocalControls *c) {return c?c->input[0]|(c->input[1]<<12):0;}
bool FzeroLocalControlsSetup(const FzeroLocalControls *c) {return c && c->setup;}
void FzeroLocalControlsLoaded(FzeroLocalControls *c) {
  if(!c)return;
  c->config=FzeroLocalConfig();c->setup=false;
  c->previous[0]=c->previous[1]=c->input[0]=c->input[1]=0;
  c->release[0]=c->release[1]=true;
}
void FzeroLocalControlsReopen(FzeroLocalControls *c) {
  if(!c)return;
  c->setup=true;c->ready[0]=c->ready[1]=false;
  c->previous[0]=c->previous[1]=c->input[0]=c->input[1]=0;
  c->release[0]=c->release[1]=true;
}
void FzeroLocalControlsDraw(const FzeroLocalControls *c,uint32_t *pixels,int width,int height) {
  if(!c || !pixels || !c->setup || width<256 || height<448)return;
  const FzeroNativeUi *ui=FzeroLocalUi();if(!ui)return;
  FzeroUiCanvas canvas={pixels,width,height,1};int center=width/2;
  FzeroUiRect(canvas,0,0,width,height,0xff000000);
  FzeroUiCenter(ui,canvas,16,"VERSUS MODE",FZERO_UI_MENU,FZERO_UI_YELLOW);
  FzeroUiCenter(ui,canvas,43,"SELECT COURSE",FZERO_UI_SMALL,FZERO_UI_CYAN);
  FzeroUiTile(ui,canvas,center-112,60,0x2f,FZERO_UI_CYAN,false,false);
  FzeroUiTile(ui,canvas,center+104,60,0x2f,FZERO_UI_CYAN,true,false);
  FzeroUiCenter(ui,canvas,56,FzeroUiCourseName(c->config.course),FZERO_UI_MENU,FZERO_UI_CYAN);
  for(unsigned p=0;p<2;++p) {
    int y=96+(int)p*164,device=c->seat[p];
    FzeroUiInk ink=device==NO_DEVICE?FZERO_UI_GREY:FZERO_UI_CYAN;
    char label[32];snprintf(label,sizeof(label),"PLAYER %u",p+1);
    FzeroUiText(ui,canvas,center-104,y,label,FZERO_UI_MENU,FZERO_UI_YELLOW);
    if(device!=NO_DEVICE)FzeroUiText(ui,canvas,center+8,y+5,device==KEYBOARD?"KEYBOARD":"CONTROL PAD",FZERO_UI_SMALL,FZERO_UI_GREY);
    for(unsigned m=0;m<4;++m) {
      int row=y+24+(int)m*24;bool selected=m==c->config.machine[p];
      FzeroUiMachine(ui,canvas,center-88,row,m,device!=NO_DEVICE && selected);
      if(selected)FzeroUiTile(ui,canvas,center-108,row+4,0x2f,ink,true,false);
      FzeroUiText(ui,canvas,center-48,row+4,FzeroUiMachineName(m),FZERO_UI_SMALL,selected?ink:FZERO_UI_GREY);
    }
    FzeroUiCenter(ui,canvas,y+124,device==NO_DEVICE?"PRESS START TO JOIN":c->ready[p]?"READY":"PRESS START WHEN READY",
        FZERO_UI_SMALL,c->ready[p]?FZERO_UI_YELLOW:FZERO_UI_CYAN);
  }
  FzeroUiCenter(ui,canvas,416,"UP DOWN SELECT MACHINE",FZERO_UI_SMALL,FZERO_UI_CYAN);
  FzeroUiCenter(ui,canvas,432,"P1 LEFT RIGHT SELECT COURSE",FZERO_UI_SMALL,FZERO_UI_GREY);
}
