#include "fzero_versus_video.h"
#include "fzero_ship.h"
#include "fzero_native_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FzeroVersusVideo {
  FzeroRenderView *views[2];
  FzeroVersusSnapshot state, previous;
  FzeroShip ships[2][2], old_ships[2][2];
  FzeroNativeUi ui;
  bool valid, interpolate;
  unsigned menu_selection,result_ticks;
};
FzeroVersusVideo *FzeroVersusVideoCreate(void) {
  FzeroVersusVideo *v=calloc(1,sizeof(*v));
  if(!v)return NULL;
  v->views[0]=FzeroRenderViewCreate();v->views[1]=FzeroRenderViewCreate();
  if(!v->views[0] || !v->views[1]) {FzeroVersusVideoDestroy(v);return NULL;}
  return v;
}
void FzeroVersusVideoDestroy(FzeroVersusVideo *v) {
  if(!v)return;
  FzeroRenderViewDestroy(v->views[0]);FzeroRenderViewDestroy(v->views[1]);free(v);
}
void FzeroVersusVideoReset(FzeroVersusVideo *v) {
  if(!v)return;
  v->valid=v->interpolate=false;
  for(unsigned i=0;i<2;++i)FzeroRenderViewReset(v->views[i]);
}
bool FzeroVersusVideoCapture(FzeroVersusVideo *v,const FzeroRenderView *assets,
                             const FzeroVersus *session,FzeroActorTables tables) {
  if(!v || !session || !tables.read)return false;
  if(!v->ui.valid && !FzeroNativeUiInit(&v->ui,tables))return false;
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(session);
  v->interpolate=v->valid && s->tick==v->state.tick+1 &&
      !memcmp(&s->config,&v->state.config,sizeof(s->config)) && s->phase==v->state.phase;
  v->previous=v->state;memcpy(v->old_ships,v->ships,sizeof(v->ships));v->state=*s;
  for(unsigned pane=0;pane<2;++pane) {
    const FzeroActor *a=&s->racers[pane].actor;
    FzeroCamera c;
    if(!FzeroCameraBuild(&c,a,tables) ||
       !FzeroRenderViewCaptureRace(v->views[pane],assets,FzeroVersusWorld(session),a,&c,(unsigned)s->tick)) {
      FzeroVersusVideoReset(v);return false;
    }
    for(unsigned i=0;i<2;++i) {
      FzeroActor grid_pose=*a;
      if(i==pane && s->phase<=FZERO_VS_COUNTDOWN) {
        grid_pose.steering_history=5;grid_pose.animation_scale=3;
      }
      const FzeroActor *camera=i==pane && s->phase<=FZERO_VS_COUNTDOWN?&grid_pose:a;
      const FzeroActor *actor=i==pane?camera:&s->racers[i].actor;
      if(!FzeroShipBuild(&v->ships[pane][i],camera,actor,tables)) {
        FzeroVersusVideoReset(v);return false;
      }
    }
    const FzeroActor *old=&v->previous.racers[pane].actor;
    if(fabs(remainder((double)a->x-old->x,8192))>128 ||
       fabs(remainder((double)a->y-old->y,4096))>128 || a->recovery_active!=old->recovery_active)
      v->interpolate=false;
  }
  v->valid=true;return true;
}
void FzeroVersusVideoMenu(FzeroVersusVideo *v,unsigned selection,unsigned result_ticks) {
  if(v) {v->menu_selection=selection;v->result_ticks=result_ticks;}
}
static void rect(uint32_t *p,FzeroViewport v,unsigned scale,int x,int y,int w,int h,uint32_t color) {
  FzeroUiRect((FzeroUiCanvas){p,v.width*(int)scale,224*(int)scale,scale},x,y,w,h,color);
}
static void time_text(char out[32],uint64_t tick) {
  uint64_t centiseconds=(uint64_t)((double)tick*100/FZERO_SIMULATION_HZ);
  snprintf(out,32,"%02u'%02u\"%02u",(unsigned)(centiseconds/6000)%100,
      (unsigned)(centiseconds/100)%60,(unsigned)(centiseconds%100));
}
static void overlay(FzeroVersusVideo *v,uint32_t *p,FzeroViewport viewport,unsigned pane,double alpha,unsigned scale) {
  const FzeroVersusSnapshot *s=&v->state;
  const FzeroVersusRacer *r=&s->racers[pane];
  FzeroUiCanvas canvas={p,viewport.width*(int)scale,224*(int)scale,scale};
  const FzeroNativeUi *ui=&v->ui;
  const FzeroUiInk ink[2]={FZERO_UI_CYAN,FZERO_UI_YELLOW};
  /* Stable actor IDs are drawn in depth order in each independent camera. */
  unsigned first=v->ships[pane][0].ground_y>v->ships[pane][1].ground_y?1:0;
  for(unsigned n=0;n<2;++n) {
    unsigned i=n?1-first:first;
    const FzeroShip *ship=&v->ships[pane][i],*old=&v->old_ships[pane][i];
    if(!ship->visible)continue;
    if(s->racers[i].status==FZERO_VS_DNF) {
      uint64_t age=s->race_tick-s->racers[i].finish_tick;
      if(s->phase==FZERO_VS_RESULTS)age+=v->result_ticks;
      FzeroUiExplosion(ui,canvas,ship->ground_x+viewport.extra,ship->y+48,ship->size,(unsigned)(age>255?255:age));
      continue;
    }
    int x=ship->x,y=ship->y;
    if(v->interpolate && old->visible && old->layout==ship->layout && old->graphic==ship->graphic &&
       abs(old->x-x)<64 && abs(old->y-y)<32) {
      x=(int)round(old->x+alpha*(x-old->x));y=(int)round(old->y+alpha*(y-old->y));
    }
    /* Contact flashes and boost exhaust belong to the actor in BOTH views.
     * These host effects use only the completed actor state and race tick. */
    int shadow_w=24-(int)ship->size*2;if(shadow_w<2)shadow_w=2;
    if(ship->ground_y>=0 && ship->ground_y<224)
      rect(p,viewport,scale,ship->ground_x+viewport.extra-shadow_w/2,ship->ground_y-2,shadow_w,2,0xff202028);
    for(int sy=0;sy<64;++sy)for(int sx=0;sx<64;++sx) {
      uint32_t pixel=ship->pixels[sy*64+sx];if(!pixel)continue;
      int dx=x+sx+viewport.extra,dy=y+sy;
      if(dy<0 || dy>=224 || dx<0 || dx>=viewport.width)continue;
      rect(p,viewport,scale,dx,dy,1,1,pixel);
    }
    if(s->racers[i].actor.speed>=0x400 && !(s->racers[i].actor.state&0x80))
      FzeroUiSpark(ui,canvas,ship->ground_x+viewport.extra,y+48,ship->size,s->racers[i].actor.animation_transition);
    FzeroUiContact(ui,canvas,ship->ground_x+viewport.extra,y+48,ship->size,s->racers[i].actor.effect_timer);
    FzeroUiPlayerTag(ui,canvas,ship->ground_x+viewport.extra-8,y+2,i,ink[i]);
  }
  if(r->status!=FZERO_VS_DNF)FzeroUiPit(ui,canvas,viewport.extra,&r->actor);
  /* Native instruments overlay each complete scenery view, with transparent
   * font backing rather than a reserved HUD band. */
  char line[64],timer[32];
  unsigned lap=r->completed_laps+1;if(lap>5)lap=5;
  FzeroUiPlayerTag(ui,canvas,8,1,pane,ink[pane]);
  snprintf(line,sizeof(line),"LAP %u",lap);
  FzeroUiText(ui,canvas,32,5,line,FZERO_UI_SMALL,ink[pane]);
  uint64_t tick=r->status!=FZERO_VS_ACTIVE?r->finish_tick:s->race_tick;
  if(r->status==FZERO_VS_DNF)snprintf(timer,sizeof(timer),"--'--\"--");else time_text(timer,tick);
  FzeroUiRaceClock(ui,canvas,8,23,timer);
  FzeroUiRaceInstruments(ui,canvas,viewport.width-112,0,r->actor.health,FzeroDisplaySpeed(r->actor.speed),s->tick);
  FzeroUiBoosts(ui,canvas,viewport.width-48,208,r->actor.boosts,(r->actor.state&8)!=0);
  const char *message=NULL;
  if(s->pause)message="PAUSED";
  else if(s->phase==FZERO_VS_READY)message="READY";
  else if(s->phase==FZERO_VS_COUNTDOWN) {
    snprintf(line,sizeof(line),"%u",(s->countdown+59)/60);message=line;
  } else if(r->status==FZERO_VS_FINISHED)message="FINISHED";
  else if(r->status==FZERO_VS_DNF) {
    FzeroUiCrashOut(ui,canvas,64);
  }
  if(!s->pause && s->phase!=FZERO_VS_RESULTS && message) {
    int w=(int)strlen(message)*8;
    FzeroUiFrame(ui,canvas,(viewport.width-w-32)/2,64,w+32,40);
    FzeroUiCenter(ui,canvas,76,message,FZERO_UI_MENU,ink[pane]);
  }
}
static void menu(FzeroVersusVideo *v,uint32_t *out,FzeroViewport viewport,unsigned scale) {
  const FzeroVersusSnapshot *s=&v->state;
  if(!s->pause && s->phase!=FZERO_VS_RESULTS)return;
  if(!s->pause)for(unsigned i=0;i<2;++i)
    if(s->racers[i].status==FZERO_VS_DNF && s->race_tick-s->racers[i].finish_tick+v->result_ticks<48)return;
  FzeroUiCanvas c={out,viewport.width*(int)scale,448*(int)scale,scale};
  const FzeroNativeUi *ui=&v->ui;int center=viewport.width/2;
  bool paused=s->pause!=0;unsigned count=paused?4:3;
  const char *heading=paused?"PAUSE":s->winner==FZERO_VS_TIE?"TIE":
      s->winner==FZERO_VS_PLAYER1?"PLAYER 1 WINS":s->winner==FZERO_VS_PLAYER2?"PLAYER 2 WINS":"NO FINISHERS";
  static const char *pause[]={"CONTINUE","RESTART RACE","CHANGE RACE","MAIN MENU"};
  static const char *results[]={"RACE AGAIN","CHANGE RACE","MAIN MENU"};
  bool disconnected=paused && (s->pause&12);
  FzeroUiFrame(ui,c,center-112,paused?120:88,224,paused?(disconnected?184:160):264);
  FzeroUiCenter(ui,c,paused?136:104,heading,FZERO_UI_MENU,FZERO_UI_YELLOW);
  if(!paused)for(unsigned i=0;i<2;++i) {
    const FzeroVersusRacer *r=&s->racers[i];char line[64],timer[32];int y=140+(int)i*48;
    bool won=s->winner==i+1;
    snprintf(line,sizeof(line),"P%u  %s",i+1,FzeroUiMachineName(r->actor.machine));
    FzeroUiText(ui,c,center-96,y,line,FZERO_UI_SMALL,i?FZERO_UI_YELLOW:FZERO_UI_CYAN);
    const char *outcome=r->status==FZERO_VS_DNF?"CRASHED OUT":
        r->status==FZERO_VS_SURVIVED?"SURVIVED":won?"FINISHED 1ST":"FINISHED 2ND";
    if(s->winner==FZERO_VS_TIE && r->status==FZERO_VS_FINISHED)outcome="TIED";
    if(r->status==FZERO_VS_FINISHED)time_text(timer,r->finish_tick);
    else snprintf(timer,sizeof(timer),"--'--\"--");
    FzeroUiText(ui,c,center-96,y+16,outcome,FZERO_UI_SMALL,FZERO_UI_CYAN);
    FzeroUiRaceClock(ui,c,center+24,y+16,timer);
  }
  for(unsigned i=0;i<count;++i) {
    int y=(paused?172:264)+(int)i*24;bool selected=i==v->menu_selection;
    if(selected)FzeroUiTile(ui,c,center-96,y+4,0x2f,FZERO_UI_YELLOW,true,false);
    FzeroUiText(ui,c,center-80,y,paused?pause[i]:results[i],FZERO_UI_MENU,selected?FZERO_UI_YELLOW:FZERO_UI_CYAN);
  }
  if(disconnected)FzeroUiCenter(ui,c,284,"PRESS START TO REJOIN",FZERO_UI_SMALL,FZERO_UI_CYAN);
}
bool FzeroVersusVideoDraw(FzeroVersusVideo *v,uint32_t *out,size_t capacity,FzeroViewport viewport,double alpha,unsigned scale) {
  if(!v || !v->valid || !out || viewport.width<256 || viewport.width>FZERO_MAX_WIDTH ||
     (scale!=1 && !FzeroValidHdScale(scale)) || capacity<(size_t)viewport.width*448*scale*scale)return false;
  if(!isfinite(alpha) || alpha>1)alpha=1;
  if(alpha<0)alpha=0;
  if(!v->interpolate)alpha=1;
  size_t pane_size=(size_t)viewport.width*224*scale*scale;
  for(unsigned i=0;i<2;++i) {
    uint32_t *p=out+i*pane_size;
    bool ok=scale==1?FzeroRenderViewDraw(v->views[i],p,viewport,alpha):
        FzeroRenderViewDrawPresentation(v->views[i],NULL,p,pane_size,viewport,alpha,scale);
    if(!ok)return false;
    overlay(v,p,viewport,i,alpha,scale);
  }
  FzeroUiRect((FzeroUiCanvas){out,viewport.width*(int)scale,448*(int)scale,scale},
      0,223,viewport.width,1,0xff000000);
  menu(v,out,viewport,scale);
  return true;
}
