#include "fzero_versus_video.h"
#include "fzero_ship.h"
#include "fzero_native_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FzeroVersusVideo {
  FzeroRenderView *views[FZERO_MAX_PLAYERS];
  FzeroVersusSnapshot state, previous;
  FzeroShip ships[FZERO_MAX_PLAYERS][FZERO_MAX_PLAYERS+FZERO_TRAFFIC_COUNT], old_ships[FZERO_MAX_PLAYERS][FZERO_MAX_PLAYERS+FZERO_TRAFFIC_COUNT];
  FzeroActor display[FZERO_MAX_PLAYERS];
  unsigned opacity[FZERO_MAX_PLAYERS+FZERO_TRAFFIC_COUNT];
  FzeroNativeUi ui;
  bool valid, interpolate, local_grid;
  unsigned menu_selection,result_ticks,menu_owner;
  uint32_t minimap[32*64];
  uint8_t map_marker[64];
  int map_x,map_y;
  bool map_valid;
};
void FzeroVersusVideoCourseHud(FzeroVersusVideo *v,const Ppu *ppu,const uint8_t *ram) {
  if(!v || v->map_valid || !ppu || !ram)return;
  /* Native $8892 uploads these 32 2bpp tiles to OBJ $1E0..$1FF.
   * Read the actual expanded upload, including custom/Deluxe course art.
   * OAM slots24..31 place its eight16x16 pieces at16,152. */
  if((ram[0x262]|(unsigned)ram[0x263]<<8)%512!=0x1e0)return;
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<32;++x) {
    unsigned tile=y/16*4+(y/8%2)*16+x/8,color=0;
    for(unsigned bit=0;bit<4;++bit)
      color|=(ppu->vram[0x5e00+tile*16+y%8+bit/2*8]>>(7-x%8+bit%2*8)&1)<<bit;
    unsigned w=ppu->cgram[128+color],r=w&31,g=w>>5&31,b=w>>10&31;
    v->minimap[y*32+x]=color?0xff000000u|((r<<3|r>>2)<<16)|((g<<3|g>>2)<<8)|(b<<3|b>>2):0;
  }
  v->map_x=(int)(ram[0xad9]|(unsigned)ram[0xada]<<8)-16;
  v->map_y=(int)(ram[0xadb]|(unsigned)ram[0xadc]<<8)-152;
  unsigned attr=ram[0x252]|(unsigned)ram[0x253]<<8;
  for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
    unsigned color=0;
    for(unsigned bit=0;bit<4;++bit)
      color|=(ppu->vram[0x4000+(attr&511)*16+y+bit/2*8]>>(7-x+bit%2*8)&1)<<bit;
    v->map_marker[y*8+x]=color?(ppu->cgram[128+(attr>>9&7)*16+color]?2:1):0;
  }
  v->map_valid=true;
}
void FzeroVersusVideoLiveMenu(FzeroVersusVideo *v,unsigned owner) {if(v)v->menu_owner=owner;}
FzeroVersusVideo *FzeroVersusVideoCreate(void) {
  FzeroVersusVideo *v=calloc(1,sizeof(*v));
  if(!v)return NULL;
  for(unsigned i=0;i<FZERO_MAX_PLAYERS;++i) {v->views[i]=FzeroRenderViewCreate();
    if(!v->views[i]) {FzeroVersusVideoDestroy(v);return NULL;}}
  return v;
}
void FzeroVersusVideoDestroy(FzeroVersusVideo *v) {
  if(!v)return;
  for(unsigned i=0;i<FZERO_MAX_PLAYERS;++i)FzeroRenderViewDestroy(v->views[i]);free(v);
}
void FzeroVersusVideoReset(FzeroVersusVideo *v) {
  if(!v)return;
  v->valid=v->interpolate=false;
  for(unsigned i=0;i<FZERO_MAX_PLAYERS;++i)FzeroRenderViewReset(v->views[i]);
}
bool FzeroVersusVideoCapture(FzeroVersusVideo *v,const FzeroRenderView *assets,
                             const FzeroVersus *session,FzeroActorTables tables) {
  if(!v || !session || !tables.read)return false;
  if(!v->ui.valid && !FzeroNativeUiInit(&v->ui,tables))return false;
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(session);
  if((tables.cgp_originals || s->config.machine[0]>=4 || s->config.machine[1]>=4) && v->ui.machines_available!=4095 &&
      !FzeroNativeUiLoadMachines(&v->ui,tables))return false;
  v->interpolate=v->valid && s->tick==v->state.tick+1 &&
      !memcmp(&s->config,&v->state.config,sizeof(s->config)) && s->phase==v->state.phase;
  v->previous=v->state;memcpy(v->old_ships,v->ships,sizeof(v->ships));v->state=*s;
  for(unsigned i=0;i<s->config.player_count;++i) {
    const FzeroVersusRacer *r=&s->racers[i];v->opacity[i]=255;v->display[i]=r->actor;
    if(r->status==FZERO_VS_FINISHED)v->opacity[i]=FzeroCameraFinishPose(&v->display[i],&r->actor,tables,
        s->race_tick-r->finish_tick+(s->phase==FZERO_VS_RESULTS?v->result_ticks:0));
  }
  for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i)v->opacity[FZERO_MAX_PLAYERS+i]=s->traffic[i].active?255:0;
  for(unsigned pane=0;pane<s->config.player_count;++pane) {
    const FzeroActor *a=&s->racers[pane].actor;
    FzeroCamera c;
    if(!FzeroCameraBuild(&c,a,tables) ||
       !FzeroRenderViewCaptureRace(v->views[pane],assets,FzeroVersusWorld(session),a,&c,(unsigned)s->tick)) {
      FzeroVersusVideoReset(v);return false;
    }
    for(unsigned i=0;i<s->config.player_count;++i) {
      FzeroActor grid_pose=*a;
      if(i==pane && s->phase<=FZERO_VS_COUNTDOWN) {
        grid_pose.steering_history=5;grid_pose.animation_scale=3;
      }
      const FzeroActor *camera=i==pane && s->phase<=FZERO_VS_COUNTDOWN?&grid_pose:a;
      const FzeroActor *actor=i==pane?camera:&s->racers[i].actor;
      if(s->racers[i].status==FZERO_VS_FINISHED)actor=&v->display[i];
      if(!FzeroShipBuild(&v->ships[pane][i],camera,actor,tables)) {
        FzeroVersusVideoReset(v);return false;
      }
      if(s->phase<=FZERO_VS_COUNTDOWN && s->countdown>=FZERO_VERSUS_REV_COUNTDOWN) {
        unsigned elapsed=FZERO_VERSUS_COUNTDOWN-s->countdown;
        int entry=(int)FzeroCameraArrivalOffset(elapsed);
        FzeroShip *ship=&v->ships[pane][i];
        ship->y+=entry*(16-(int)ship->size)/16;
        ship->ground_y+=entry*(16-(int)ship->size)/16;
      }
    }
    for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i) {
      FzeroShip *ship=&v->ships[pane][FZERO_MAX_PLAYERS+i];memset(ship,0,sizeof(*ship));
      if(s->traffic[i].active && !FzeroShipBumper(ship,a,&s->traffic[i].actor,FzeroVersusWorld(session),tables,s->traffic[i].explosive)) {
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
bool FzeroVersusVideoResultsReady(const FzeroVersusSnapshot *s,unsigned result_ticks) {
  if(!s)return false;
  for(unsigned i=0;i<s->config.player_count;++i) {
    const FzeroVersusRacer *r=&s->racers[i];uint64_t age=s->race_tick-r->finish_tick+result_ticks;
    if((r->status==FZERO_VS_DNF && age<48) ||
        (r->status==FZERO_VS_FINISHED && age<FZERO_FINISH_PRESENTATION))return false;
  }
  return true;
}
static void rect(uint32_t *p,FzeroViewport v,unsigned scale,int x,int y,int w,int h,uint32_t color) {
  FzeroUiRect((FzeroUiCanvas){p,v.width*(int)scale,224*(int)scale,scale},x,y,w,h,color);
}
static void fade_rect(uint32_t *p,FzeroViewport v,unsigned scale,int x,int y,int w,int h,uint32_t color,unsigned opacity,bool premultiplied) {
  if(!opacity)return;
  if(opacity==255) {rect(p,v,scale,x,y,w,h,color);return;}
  int x0=x<0?0:x,y0=y<0?0:y,x1=x+w>v.width?v.width:x+w,y1=y+h>224?224:y+h;
  for(int dy=y0*(int)scale;dy<y1*(int)scale;++dy)for(int dx=x0*(int)scale;dx<x1*(int)scale;++dx) {
    uint32_t *pixel=&p[(size_t)dy*v.width*scale+dx];
    unsigned a=premultiplied?opacity+(((*pixel>>24)*(255-opacity)+127)/255):255;
    uint32_t mixed=a<<24;
    for(unsigned shift=0;shift<24;shift+=8)
      mixed|=((((color>>shift)&255)*opacity+((*pixel>>shift)&255)*(255-opacity)+127)/255)<<shift;
    *pixel=mixed;
  }
}
static void time_text(char out[32],uint64_t tick) {
  uint64_t centiseconds=(uint64_t)((double)tick*100/FZERO_SIMULATION_HZ);
  snprintf(out,32,"%02u'%02u\"%02u",(unsigned)(centiseconds/6000)%100,
      (unsigned)(centiseconds/100)%60,(unsigned)(centiseconds%100));
}
static void overlay(FzeroVersusVideo *v,uint32_t *p,FzeroViewport viewport,unsigned pane,double alpha,unsigned scale,bool premultiplied) {
  static bool trace_ready,trace_traffic;static unsigned traced_panes;
  if(!trace_ready) {trace_ready=true;trace_traffic=getenv("FZERO_TEST_TRAFFIC_VISIBLE")!=NULL;}
  const FzeroVersusSnapshot *s=&v->state;
  const FzeroVersusRacer *r=&s->racers[pane];
  FzeroUiCanvas canvas={p,viewport.width*(int)scale,224*(int)scale,scale};
  const FzeroNativeUi *ui=&v->ui;
  const FzeroUiInk ink[FZERO_MAX_PLAYERS]={FZERO_UI_CYAN,FZERO_UI_YELLOW,FZERO_UI_BLUE,FZERO_UI_GREY};
  /* Stable actor IDs are drawn in depth order in each independent camera. */
  unsigned order[FZERO_MAX_PLAYERS+FZERO_TRAFFIC_COUNT];
  for(unsigned i=0;i<FZERO_MAX_PLAYERS+FZERO_TRAFFIC_COUNT;++i) {
    unsigned at=i;while(at && v->ships[pane][order[at-1]].ground_y>v->ships[pane][i].ground_y) {
      order[at]=order[at-1];--at;
    }
    order[at]=i;
  }
  /* Native human OAM slots0..7 win over later CPU slots at equal priority.
   * Preserve that ownership when a close following vehicle projects lower. */
  for(unsigned i=0;i<FZERO_MAX_PLAYERS+FZERO_TRAFFIC_COUNT;++i)if(order[i]==pane) {
    memmove(order+i,order+i+1,(FZERO_MAX_PLAYERS-1+FZERO_TRAFFIC_COUNT-i)*sizeof(*order));
    order[FZERO_MAX_PLAYERS-1+FZERO_TRAFFIC_COUNT]=pane;break;
  }
  for(unsigned n=0;n<FZERO_MAX_PLAYERS+FZERO_TRAFFIC_COUNT;++n) {
    unsigned i=order[n];bool npc=i>=FZERO_MAX_PLAYERS;
    const FzeroShip *ship=&v->ships[pane][i],*old=&v->old_ships[pane][i];
    unsigned opacity=v->opacity[i];
    if(!ship->visible || !opacity)continue;
    if(!npc && i>=s->config.player_count)continue;
    if(!npc && s->racers[i].status==FZERO_VS_DNF) {
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
    /* Native human body/exhaust in its own view; the other racer uses the
     * native opponent projection. Presentation never advances actor state. */
    int rx=16-(int)ship->size*2,ry=4-(int)ship->size/2;
    if(rx<2)rx=2;if(ry<1)ry=1;
    if(ship->ground_y>=0 && ship->ground_y<224)
      for(int dy=-ry;dy<=ry;++dy)for(int dx=-rx;dx<=rx;++dx)
        if(dx*dx*ry*ry+dy*dy*rx*rx<=rx*rx*ry*ry)
          fade_rect(p,viewport,scale,ship->ground_x+viewport.extra+dx,ship->ground_y-2+dy,1,1,0xff101018,opacity*160/255,premultiplied);
    for(int sy=0;sy<64;++sy)for(int sx=0;sx<64;++sx) {
      uint32_t pixel=ship->pixels[sy*64+sx];if(!pixel)continue;
      int dx=x+sx+viewport.extra,dy=y+sy;
      if(dy<0 || dy>=224 || dx<0 || dx>=viewport.width)continue;
      fade_rect(p,viewport,scale,dx,dy,1,1,pixel,opacity,premultiplied);
      if(npc && trace_traffic && !(traced_panes&(1u<<pane))) {
        traced_panes|=1u<<pane;
        fprintf(stderr,"[traffic-video] pane=%u npc=%u world=%u,%u visible\n",pane,i-FZERO_MAX_PLAYERS,s->traffic[i-FZERO_MAX_PLAYERS].actor.x,s->traffic[i-FZERO_MAX_PLAYERS].actor.y);
      }
    }
    if(npc)continue;
    if(i==pane && s->racers[i].status==FZERO_VS_ACTIVE)
      FzeroUiExhaust(ui,canvas,ship->ground_x+viewport.extra,y+48,&s->racers[i].actor);
    if(s->racers[i].status!=FZERO_VS_FINISHED && s->racers[i].actor.speed>=0x400 && !(s->racers[i].actor.state&0x80))
      FzeroUiSpark(ui,canvas,ship->ground_x+viewport.extra,y+48,ship->size,s->racers[i].actor.animation_transition);
    if(s->racers[i].status!=FZERO_VS_FINISHED)FzeroUiContact(ui,canvas,ship->ground_x+viewport.extra,y+48,ship->size,s->racers[i].actor.effect_timer);
    /* Far-car labels must not paint across the native timer/power HUD. */
    if(opacity==255 && y+2>=64) {
      FzeroUiPlayerTag(ui,canvas,ship->ground_x+viewport.extra-8,y+2,i,ink[i]);
    }
    else if(opacity<255 && y+2>=64) {
      uint32_t label[16*16]={0};FzeroUiPlayerTag(ui,(FzeroUiCanvas){label,16,16,1},0,0,i,ink[i]);
      for(unsigned ly=0;ly<16;++ly)for(unsigned lx=0;lx<16;++lx)if(label[ly*16+lx])
        fade_rect(p,viewport,scale,ship->ground_x+viewport.extra-8+(int)lx,y+2+(int)ly,1,1,label[ly*16+lx],opacity,premultiplied);
    }
  }
  if(r->status==FZERO_VS_ACTIVE)FzeroUiPit(ui,canvas,viewport.extra,&r->actor);
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
  FzeroUiRaceInstruments(ui,canvas,viewport.width-(ui->cgp_hud?136:112),0,r->actor.health,FzeroDisplaySpeed(r->actor.speed),s->tick);
  FzeroUiBoosts(ui,canvas,viewport.width-(ui->cgp_hud?16:48),ui->cgp_hud?8:208,r->actor.boosts,(r->actor.state&8)!=0);
  FzeroUiMachine(ui,canvas,viewport.width-80,204,r->actor.machine,true);
  FzeroUiText(ui,canvas,8,45,"RANK",FZERO_UI_SMALL,ink[pane]);
  snprintf(line,sizeof(line),"%u",FzeroVersusRank(s,pane));
  FzeroUiText(ui,canvas,48,40,line,FZERO_UI_MENU,ink[pane]);
  if(v->map_valid) {
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<32;++x)
      if(v->minimap[y*32+x])FzeroUiRect(canvas,16+(int)x,152+(int)y,1,1,v->minimap[y*32+x]);
    /* Native $AA6C rotates world Y into map X and negates world X into Y.
     * Draw the other marker first, preserving the native player's priority. */
    for(unsigned n=0;n<s->config.player_count;++n) {
      unsigned i=(pane+1+n)%s->config.player_count;if(s->racers[i].status==FZERO_VS_DNF)continue;
      const FzeroActor *a=&s->racers[i].actor;
      int x=(((a->y/128&63)+v->map_x+16)&255),y=((v->map_y+152-(a->x/128&63))&255);
      for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<8;++xx) {
        unsigned color=v->map_marker[yy*8+xx];
        if(color)FzeroUiRect(canvas,x+(int)xx,y+(int)yy,1,1,color==1?0xff000000:ui->palette[ink[i]][3]);
      }
    }
  }
  const char *message=NULL;
  if(s->phase==FZERO_VS_RACING && r->status==FZERO_VS_ACTIVE)
    FzeroUiLapNotice(ui,canvas,r->completed_laps,s->race_tick-r->lap_start,r->lap_boost!=0);
  if(s->phase==FZERO_VS_READY)message="READY";
  else if(s->phase==FZERO_VS_COUNTDOWN) {
    if(s->countdown<=FZERO_VERSUS_REV_COUNTDOWN) {
      snprintf(line,sizeof(line),"%u",(s->countdown+59)/60);message=line;
    }
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
static void menu(FzeroVersusVideo *v,uint32_t *out,FzeroViewport viewport,unsigned scale,int player) {
  bool single=player>=0;
  const FzeroVersusSnapshot *s=&v->state;
  if(s->phase!=FZERO_VS_RESULTS) {
    if(!v->menu_owner)return;
    FzeroUiCanvas c={out,viewport.width*(int)scale,(single?224:448)*(int)scale,scale};
    const FzeroNativeUi *ui=&v->ui;
    /* Keep the live menu compact, inside its operator's own pane.
     * Reuse the native small alphabet and original frame artwork. */
    unsigned owner=v->menu_owner-1;
    if(single && owner!=(unsigned)player)return;
    int left=viewport.width-184,top=single?4:4+(int)owner*224;
    static const char *labels[]={"CONTINUE","RESTART RACE","CHANGE RACE","MAIN MENU"};
    FzeroUiFrame(ui,c,left,top,176,72);
    FzeroUiText(ui,c,left+8,top+8,"RACE CONTINUES",FZERO_UI_SMALL,FZERO_UI_YELLOW);
    for(unsigned i=0;i<4;++i) {
      int y=top+24+(int)i*10;bool selected=i==v->menu_selection;
      if(selected)FzeroUiTile(ui,c,left+8,y,0x2f,FZERO_UI_YELLOW,true,false);
      FzeroUiText(ui,c,left+24,y,labels[i],FZERO_UI_SMALL,selected?FZERO_UI_YELLOW:FZERO_UI_CYAN);
    }
    return;
  }
  if(!s->pause && !FzeroVersusVideoResultsReady(s,v->result_ticks))return;
  FzeroUiCanvas c={out,viewport.width*(int)scale,(single?224:448)*(int)scale,scale};
  const FzeroNativeUi *ui=&v->ui;int center=viewport.width/2;
  unsigned count=single && !v->local_grid?2:3;
  const char *heading=s->winner==FZERO_VS_TIE?"TIE":
      s->winner==FZERO_VS_PLAYER1?"PLAYER 1 WINS":s->winner==FZERO_VS_PLAYER2?"PLAYER 2 WINS":s->winner==FZERO_VS_PLAYER3?"PLAYER 3 WINS":s->winner==FZERO_VS_PLAYER4?"PLAYER 4 WINS":"NO FINISHERS";
  static const char *results[]={"RACE AGAIN","CHANGE RACE","MAIN MENU"};
  FzeroUiFrame(ui,c,center-112,single?8:88,224,single?184:264);
  FzeroUiCenter(ui,c,single?20:104,heading,FZERO_UI_MENU,FZERO_UI_YELLOW);
  for(unsigned i=0;i<s->config.player_count;++i) {
    const FzeroVersusRacer *r=&s->racers[i];char line[64],timer[32];int y=(single?(s->config.player_count>2?42:52):140)+(int)i*(single?(s->config.player_count>2?26:40):48);
    unsigned rank=FzeroVersusRank(s,i);
    snprintf(line,sizeof(line),"P%u  %s",i+1,FzeroUiMachineName(r->actor.machine));
    FzeroUiText(ui,c,center-96,y,line,FZERO_UI_SMALL,i?FZERO_UI_YELLOW:FZERO_UI_CYAN);
    const char *outcome=r->status==FZERO_VS_DNF?"CRASHED OUT":
        r->status==FZERO_VS_SURVIVED?"SURVIVED":(const char *[]){"", "FINISHED 1ST","FINISHED 2ND","FINISHED 3RD","FINISHED 4TH"}[rank];
    if(s->winner==FZERO_VS_TIE && rank==1 && r->status==FZERO_VS_FINISHED)outcome="TIED";
    if(r->status==FZERO_VS_FINISHED)time_text(timer,r->finish_tick);
    else snprintf(timer,sizeof(timer),"--'--\"--");
    FzeroUiText(ui,c,center-96,y+16,outcome,FZERO_UI_SMALL,FZERO_UI_CYAN);
    FzeroUiRaceClock(ui,c,center+24,y+16,timer);
  }
  for(unsigned i=0;i<count;++i) {
    int y=(single?(s->config.player_count>2?152:144):264)+(int)i*(single && s->config.player_count>2?18:24);bool selected=i==v->menu_selection;
    if(selected)FzeroUiTile(ui,c,center-96,y+4,0x2f,FZERO_UI_YELLOW,true,false);
    const char *label=results[i];
    FzeroUiText(ui,c,center-80,y,label,FZERO_UI_MENU,selected?FZERO_UI_YELLOW:FZERO_UI_CYAN);
  }
}
static bool grid(FzeroVersusVideo *v,uint32_t *out,size_t capacity,FzeroViewport whole,double alpha,unsigned scale) {
  if(!v || !v->valid || !out || whole.width<512 || whole.width>2*FZERO_MAX_WIDTH ||
      (scale!=1 && !FzeroValidHdScale(scale)) || capacity<(size_t)whole.width*448*scale*scale)return false;
  FzeroViewport pane=whole;pane.width/=2;pane.extra=(pane.width-256)/2;
  size_t count=(size_t)pane.width*224*scale*scale;
  uint32_t *scratch=malloc(count*4);if(!scratch)return false;
  memset(out,0,(size_t)whole.width*448*scale*scale*4);
  bool ok=true;v->local_grid=true;
  for(unsigned p=0;p<v->state.config.player_count && ok;++p) {
    ok=FzeroVersusVideoDrawPlayer(v,scratch,count,pane,alpha,scale,p);
    for(unsigned y=0;y<224*scale && ok;++y)
      memcpy(out+((p/2*224*scale+y)*whole.width+p%2*pane.width)*scale,
          scratch+(size_t)y*pane.width*scale,(size_t)pane.width*scale*4);
  }
  free(scratch);v->local_grid=false;return ok;
}
bool FzeroVersusVideoDraw(FzeroVersusVideo *v,uint32_t *out,size_t capacity,FzeroViewport viewport,double alpha,unsigned scale) {
  if(v && v->state.config.player_count>2)return grid(v,out,capacity,viewport,alpha,scale);
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
    overlay(v,p,viewport,i,alpha,scale,false);
  }
  FzeroUiRect((FzeroUiCanvas){out,viewport.width*(int)scale,448*(int)scale,scale},
      0,223,viewport.width,1,0xff000000);
  menu(v,out,viewport,scale,-1);
  return true;
}
bool FzeroVersusVideoDrawPlayer(FzeroVersusVideo *v,uint32_t *out,size_t capacity,
                               FzeroViewport viewport,double alpha,unsigned scale,unsigned player) {
  if(!v || !v->valid || !out || player>=v->state.config.player_count || viewport.width<256 || viewport.width>FZERO_MAX_WIDTH ||
     (scale!=1 && !FzeroValidHdScale(scale)) || capacity<(size_t)viewport.width*224*scale*scale)return false;
  if(!isfinite(alpha) || alpha>1)alpha=1;if(alpha<0)alpha=0;if(!v->interpolate)alpha=1;
  size_t count=(size_t)viewport.width*224*scale*scale;
  bool ok=scale==1?FzeroRenderViewDraw(v->views[player],out,viewport,alpha):
      FzeroRenderViewDrawPresentation(v->views[player],NULL,out,count,viewport,alpha,scale);
  if(!ok)return false;
  overlay(v,out,viewport,player,alpha,scale,false);menu(v,out,viewport,scale,(int)player);return true;
}

bool FzeroVersusVideoDrawCombined(FzeroVersusVideo *v,uint32_t *native,uint32_t *hd,size_t capacity,
    FzeroViewport viewport,double alpha,unsigned scale,int player) {
  if(v && player<0 && v->state.config.player_count>2) {
    return (!native || grid(v,native,(size_t)viewport.width*448,viewport,alpha,1)) && grid(v,hd,capacity,viewport,alpha,scale);
  }
  bool single=player>=0;unsigned panes=single?1:2;
  if(!v || !v->valid || !hd || player>=(int)v->state.config.player_count || player< -1 || !FzeroValidHdScale(scale) ||
      viewport.width<256 || viewport.width>FZERO_MAX_WIDTH)return false;
  size_t count=(size_t)viewport.width*224,hd_count=count*scale*scale;
  if(capacity<hd_count*panes)return false;
  if(!isfinite(alpha) || alpha>1)alpha=1;if(alpha<0)alpha=0;if(!v->interpolate)alpha=1;
  for(unsigned pane=0;pane<panes;++pane) {
    unsigned camera=single?(unsigned)player:pane;
    uint32_t *small=native?native+pane*count:NULL,*large=hd+pane*hd_count;
    if(!FzeroRenderViewDrawPresentation(v->views[camera],small,large,hd_count,viewport,alpha,scale))return false;
    overlay(v,large,viewport,camera,alpha,scale,false);
    if(small)overlay(v,small,viewport,camera,alpha,1,false);
  }
  if(!single) {
    FzeroUiRect((FzeroUiCanvas){hd,viewport.width*(int)scale,448*(int)scale,scale},0,223,viewport.width,1,0xff000000);
    if(native)FzeroUiRect((FzeroUiCanvas){native,viewport.width,448,1},0,223,viewport.width,1,0xff000000);
  }
  menu(v,hd,viewport,scale,player);if(native)menu(v,native,viewport,1,player);return true;
}

SnesHdFrame *FzeroVersusVideoPrepareHd(FzeroVersusVideo *v,uint32_t *native,
    FzeroViewport viewport,double alpha,int player) {
  if(v && player<0 && v->state.config.player_count>2)return NULL;
  if (!v || !v->valid || !native || player < -1 || player >= (int)v->state.config.player_count) return NULL;
  if (!isfinite(alpha) || alpha > 1 || !v->interpolate) alpha = 1;
  if (alpha < 0) alpha = 0;
  unsigned panes = player < 0 ? 2 : 1;
  size_t count = (size_t)viewport.width * 224;
  SnesHdFrame *jobs = SnesHdFrameCreate(viewport.width, 224 * panes);
  uint32_t *foreground = calloc(count * panes, sizeof(*foreground));
  if (!jobs || !foreground) goto fail;
  for (unsigned pane = 0; pane < panes; ++pane) {
    unsigned camera = player < 0 ? pane : (unsigned)player;
    uint32_t *small = native + count * pane;
    SnesHdFrame *row_jobs = FzeroRenderViewPrepareHd(v->views[camera],small,viewport,alpha);
    bool ok = row_jobs && SnesHdFrameSeal(row_jobs) && SnesHdFrameCopyRows(jobs,pane*224,row_jobs);
    SnesHdFrameDestroy(row_jobs);
    if (!ok) goto fail;
    overlay(v,small,viewport,camera,alpha,1,false);
    overlay(v,foreground + pane * count,viewport,camera,alpha,1,true);
  }
  if (panes == 2) {
    FzeroUiCanvas c = {native,viewport.width,448,1};
    FzeroUiRect(c,0,223,viewport.width,1,0xff000000);
    c.pixels = foreground; FzeroUiRect(c,0,223,viewport.width,1,0xff000000);
  }
  menu(v,native,viewport,1,player); menu(v,foreground,viewport,1,player);
  if (!SnesHdFrameSetNative(jobs,native,viewport.width*4,NULL,0) ||
      !SnesHdFrameSetOverlay(jobs,foreground,viewport.width*4)) goto fail;
  free(foreground); return jobs;
fail:
  free(foreground); SnesHdFrameDestroy(jobs); return NULL;
}
