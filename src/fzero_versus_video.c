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
    for(unsigned i=0;i<2;++i)
      if(!FzeroShipBuild(&v->ships[pane][i],a,&s->racers[i].actor,tables)) {
        FzeroVersusVideoReset(v);return false;
      }
    const FzeroActor *old=&v->previous.racers[pane].actor;
    if(fabs(remainder((double)a->x-old->x,8192))>128 ||
       fabs(remainder((double)a->y-old->y,4096))>128 || a->recovery_active!=old->recovery_active)
      v->interpolate=false;
  }
  v->valid=true;return true;
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
      if(s->racers[i].actor.black_flash && (s->tick&2))pixel=0xffd8d8e0;
      rect(p,viewport,scale,dx,dy,1,1,pixel);
    }
    if(s->racers[i].actor.boost_timer && (s->tick&1))
      rect(p,viewport,scale,ship->ground_x+viewport.extra-2,y+47,4,4,0xff80e8ff);
    char label[4];snprintf(label,sizeof(label),"P%u",i+1);
    FzeroUiText(ui,canvas,ship->ground_x+viewport.extra-8,y+10,label,FZERO_UI_SMALL,ink[i]);
  }
  /* Native instruments overlay each complete scenery view. Their cartridge
   * outline pixels preserve contrast without reserving a black HUD band. */
  char line[64],timer[32];
  unsigned lap=r->completed_laps+1;if(lap>5)lap=5;
  snprintf(line,sizeof(line),"P%u  LAP %u",pane+1,lap);
  FzeroUiText(ui,canvas,8,3,line,FZERO_UI_SMALL,ink[pane]);
  FzeroUiText(ui,canvas,8,18,"TIME",FZERO_UI_SMALL,FZERO_UI_YELLOW);
  uint64_t tick=r->status!=FZERO_VS_ACTIVE?r->finish_tick:s->race_tick;
  time_text(timer,tick);FzeroUiText(ui,canvas,8,31,timer,FZERO_UI_HUD,FZERO_UI_YELLOW);
  snprintf(line,sizeof(line),"%3u",FzeroDisplaySpeed(r->actor.speed));
  FzeroUiText(ui,canvas,viewport.width-64,31,line,FZERO_UI_HUD,FZERO_UI_YELLOW);
  for(unsigned i=0;i<3;++i)FzeroUiHudTile(ui,canvas,viewport.width-40+(int)i*8,31,0x0a+i,FZERO_UI_YELLOW,false);
  unsigned power=(r->actor.health&0x8000)?0:r->actor.health;
  if(power>0x800)power=0x800;
  int right=viewport.width-80,filled=(int)(power/32);
  FzeroUiText(ui,canvas,right,1,"POWER",FZERO_UI_SMALL,FZERO_UI_YELLOW);
  FzeroUiRect(canvas,right,12,64,8,0xff420000);
  FzeroUiRect(canvas,right,12,filled,8,power<0x200 && (s->tick&8)?0xff840000:0xffff0000);
  for(unsigned i=0;i<8;++i) {
    unsigned amount=filled>(int)i*8?(unsigned)(filled-(int)i*8):0;
    unsigned tile=amount>=8?0x10:amount?0x18-amount:0x19;
    FzeroUiHudTile(ui,canvas,right+(int)i*8,8,tile,FZERO_UI_YELLOW,false);
    FzeroUiHudTile(ui,canvas,right+(int)i*8,16,tile,FZERO_UI_YELLOW,true);
  }
  for(unsigned i=0;i<r->actor.boosts && i<3;++i)
    FzeroUiHudTile(ui,canvas,viewport.width/2-8+(int)i*8,31,0x33,FZERO_UI_CYAN,false);
  const char *message=NULL;
  if(s->pause)message="PAUSED";
  else if(s->phase==FZERO_VS_READY)message="READY";
  else if(s->phase==FZERO_VS_COUNTDOWN) {
    snprintf(line,sizeof(line),"%u",(s->countdown+59)/60);message=line;
  } else if(r->status==FZERO_VS_FINISHED)message="FINISHED";
  else if(r->status==FZERO_VS_DNF)message="CRASH OUT";
  if(s->phase==FZERO_VS_RESULTS) {
    const char *result=s->winner==FZERO_VS_TIE?"TIE":s->winner==FZERO_VS_PLAYER1?"PLAYER 1 WINS":
        s->winner==FZERO_VS_PLAYER2?"PLAYER 2 WINS":"NO FINISHERS";
    FzeroUiFrame(ui,canvas,(viewport.width-224)/2,64,224,112);
    FzeroUiCenter(ui,canvas,80,result,FZERO_UI_MENU,FZERO_UI_YELLOW);
    FzeroUiCenter(ui,canvas,112,r->status==FZERO_VS_FINISHED?"FINISHED":"CRASH OUT",FZERO_UI_SMALL,ink[pane]);
    FzeroUiCenter(ui,canvas,136,"B  REMATCH",FZERO_UI_SMALL,FZERO_UI_CYAN);
    FzeroUiCenter(ui,canvas,152,"Y  CHANGE RACE",FZERO_UI_SMALL,FZERO_UI_CYAN);
  } else if(s->pause) {
    FzeroUiFrame(ui,canvas,(viewport.width-224)/2,64,224,112);
    FzeroUiCenter(ui,canvas,80,"PAUSE",FZERO_UI_MENU,FZERO_UI_YELLOW);
    FzeroUiCenter(ui,canvas,112,(s->pause&12)?"PRESS START TO JOIN":"START  CONTINUE",FZERO_UI_SMALL,FZERO_UI_CYAN);
    FzeroUiCenter(ui,canvas,144,"X  END RACE",FZERO_UI_SMALL,FZERO_UI_CYAN);
  } else if(message) {
    int w=(int)strlen(message)*8;
    FzeroUiFrame(ui,canvas,(viewport.width-w-32)/2,64,w+32,40);
    FzeroUiCenter(ui,canvas,76,message,FZERO_UI_MENU,ink[pane]);
  }
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
  return true;
}
