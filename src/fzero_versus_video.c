#include "fzero_versus_video.h"
#include "fzero_ship.h"
#include "snes_overlay_draw.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FzeroVersusVideo {
  FzeroRenderView *views[2];
  FzeroVersusSnapshot state, previous;
  FzeroShip ships[2][2], old_ships[2][2];
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
  snes_ovl_fill_rect(p,v.width*(int)scale,224*(int)scale,x*(int)scale,y*(int)scale,w*(int)scale,h*(int)scale,color);
}
static void text(uint32_t *p,FzeroViewport v,unsigned scale,int x,int y,const char *s,uint32_t color) {
  snes_ovl_draw_text(p,v.width*(int)scale,224*(int)scale,x*(int)scale,y*(int)scale,s,color,(int)scale);
}
static void time_text(char out[32],uint64_t tick) {
  uint64_t centiseconds=(uint64_t)((double)tick*100/FZERO_SIMULATION_HZ);
  snprintf(out,32,"%02u:%02u.%02u",(unsigned)(centiseconds/6000)%100,
      (unsigned)(centiseconds/100)%60,(unsigned)(centiseconds%100));
}
static void overlay(FzeroVersusVideo *v,uint32_t *p,FzeroViewport viewport,unsigned pane,double alpha,unsigned scale) {
  const FzeroVersusSnapshot *s=&v->state;
  const FzeroVersusRacer *r=&s->racers[pane];
  const uint32_t colors[2]={0xff65dfff,0xffffb059};
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
    if(ship->ground_y>=48 && ship->ground_y<224)
      rect(p,viewport,scale,ship->ground_x+viewport.extra-shadow_w/2,ship->ground_y-2,shadow_w,2,0xff202028);
    for(int sy=0;sy<64;++sy)for(int sx=0;sx<64;++sx) {
      uint32_t pixel=ship->pixels[sy*64+sx];if(!pixel)continue;
      int dx=x+sx+viewport.extra,dy=y+sy;
      if(dy<48 || dy>=224 || dx<0 || dx>=viewport.width)continue;
      if(s->racers[i].actor.black_flash && (s->tick&2))pixel=0xffd8d8e0;
      rect(p,viewport,scale,dx,dy,1,1,pixel);
    }
    if(s->racers[i].actor.boost_timer && (s->tick&1))
      rect(p,viewport,scale,ship->ground_x+viewport.extra-2,y+47,4,4,0xff80e8ff);
    char label[4];snprintf(label,sizeof(label),"P%u",i+1);
    text(p,viewport,scale,ship->ground_x+viewport.extra-8,y+10,label,colors[i]);
  }
  rect(p,viewport,scale,0,0,viewport.width,35,0xff111625);
  rect(p,viewport,scale,0,0,3,224,colors[pane]);
  char line[64],timer[32];
  unsigned lap=r->completed_laps+1;if(lap>5)lap=5;
  snprintf(line,sizeof(line),"P%u  LAP %u/5",pane+1,lap);text(p,viewport,scale,8,5,line,colors[pane]);
  uint64_t tick=r->status!=FZERO_VS_ACTIVE?r->finish_tick:s->race_tick;
  time_text(timer,tick);text(p,viewport,scale,8,20,timer,0xfff4f4ff);
  snprintf(line,sizeof(line),"%3u KM/H",FzeroDisplaySpeed(r->actor.speed));
  text(p,viewport,scale,viewport.width-96,20,line,0xfff4f4ff);
  unsigned power=(r->actor.health&0x8000)?0:r->actor.health;
  if(power>0x800)power=0x800;
  int right=viewport.width-104;
  rect(p,viewport,scale,right,5,96,8,0xff3b3e4d);
  rect(p,viewport,scale,right,5,(int)(power*96/0x800),8,power<0x200?0xffff6868:colors[pane]);
  snprintf(line,sizeof(line),"S:%u",r->actor.boosts);text(p,viewport,scale,viewport.width/2-12,20,line,colors[pane]);
  const char *message=NULL;
  if(s->pause)message="PAUSED";
  else if(s->phase==FZERO_VS_READY)message="READY";
  else if(s->phase==FZERO_VS_COUNTDOWN) {
    snprintf(line,sizeof(line),"%u",(s->countdown+59)/60);message=line;
  } else if(r->status==FZERO_VS_FINISHED)message="FINISHED";
  else if(r->status==FZERO_VS_DNF)message="DNF";
  if(message) {
    int w=(int)strlen(message)*8;
    rect(p,viewport,scale,(viewport.width-w)/2-8,66,w+16,16,0xff111625);
    text(p,viewport,scale,(viewport.width-w)/2,70,message,colors[pane]);
  }
  if(s->phase==FZERO_VS_RESULTS) {
    const char *result=s->winner==FZERO_VS_TIE?"TIE":s->winner==FZERO_VS_PLAYER1?"PLAYER 1 WINS":
        s->winner==FZERO_VS_PLAYER2?"PLAYER 2 WINS":"NO FINISHERS";
    text(p,viewport,scale,(viewport.width-(int)strlen(result)*8)/2,90,result,0xfff4f4ff);
    text(p,viewport,scale,(viewport.width-24*8)/2,106,"B REMATCH / Y CHANGE RACE",0xffb9bfd0);
  } else if(s->pause) {
    const char *hint=(s->pause&12)?"START ON REPLACEMENT PAD":"START RESUME / X END RACE";
    text(p,viewport,scale,(viewport.width-(int)strlen(hint)*8)/2,90,hint,0xffb9bfd0);
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
  return true;
}
