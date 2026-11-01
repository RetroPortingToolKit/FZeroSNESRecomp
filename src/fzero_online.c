#include "fzero_online.h"
#include <stdio.h>
#include <string.h>

static bool enabled;
static unsigned view;
static FzeroOnlineState state;
void FzeroOnlineBeginPlayers(unsigned aspect,unsigned slot,unsigned count) {
  if(aspect>2 || count<2 || count>4 || slot>=count) {FzeroOnlineEnd();return;}
  memset(&state,0,sizeof(state));state.aspect=(uint8_t)aspect;
  state.config=(FzeroVersusConfig){0,{0,1,2,3},(uint8_t)count};state.phase=FZERO_ONLINE_TITLE;view=slot;enabled=true;
}
void FzeroOnlineEnterVs(void) {
  if(!enabled || state.phase!=FZERO_ONLINE_TITLE)return;
  state.phase=FZERO_ONLINE_MACHINES;state.ready=0;state.release=(uint8_t)FzeroVersusReadyMask(state.config);
  for(unsigned i=0;i<state.config.player_count;++i)state.selection_tick[i]=state.tick;
  for(unsigned i=0;i<state.config.player_count;++i) {state.previous[i]=4095;}
}
void FzeroOnlineEnd(void) {enabled=false;memset(&state,0,sizeof(state));}
bool FzeroOnlineEnabled(void) {return enabled;}
int FzeroOnlineView(void) {return enabled?(int)view:-1;}
const FzeroOnlineState *FzeroOnlineCompleted(void) {return &state;}
static void leave(void) {state.phase=FZERO_ONLINE_EXIT;state.exit_tick=state.tick;}
static void back(void) {
  if(state.phase==FZERO_ONLINE_COURSE)state.phase=FZERO_ONLINE_CUP;
  else if(state.phase==FZERO_ONLINE_CUP) {state.phase=FZERO_ONLINE_MACHINES;state.ready=0;}
  else leave();
}
bool FzeroOnlineTickInputs(uint16_t inputs[]) {
  if(!enabled)return false;
  ++state.tick;
  uint16_t held[FZERO_MAX_PLAYERS]={0},pressed[FZERO_MAX_PLAYERS]={0};
  memcpy(held,inputs,state.config.player_count*sizeof(*held));
  for(unsigned i=0;i<state.config.player_count;++i) {
    if(state.release&(1u<<i)) {if(!held[i])state.release&=~(1u<<i);held[i]=0;}
  }
  memcpy(inputs,held,state.config.player_count*sizeof(*held));
  for(unsigned i=0;i<state.config.player_count;++i) {pressed[i]=held[i]&~state.previous[i];state.previous[i]=held[i];}
  if(state.phase==FZERO_ONLINE_EXIT)return true;
  if(state.phase==FZERO_ONLINE_TITLE)return false;
  if(state.phase==FZERO_ONLINE_LOADING) {
    if(FzeroLocalSession()) {state.phase=FZERO_ONLINE_RACE;for(unsigned i=0;i<state.config.player_count;++i) {state.previous[i]=4095;}}
    return false;
  }
  if(state.phase==FZERO_ONLINE_RACE) {
    FzeroVersus *race=FzeroLocalSession();const FzeroVersusSnapshot *s=FzeroVersusCompleted(race);
    if(!s)return false;
    /* Menu state is in the rollback image. Opening it never consumes a
     * simulation tick; only its operator's driving input is neutralized. */
    bool results=s->phase==FZERO_VS_RESULTS;
    unsigned owner=FzeroLocalMenuOwner();
    if(results && owner) {FzeroLocalSetMenuOwner(0);owner=0;}
    if(!results && !owner)for(unsigned p=0;p<state.config.player_count;++p)if(pressed[p]&8) {
      FzeroLocalSetMenuOwner(p+1);owner=FzeroLocalMenuOwner();
      pressed[p]=0;break;
    }
    if(!results && owner)inputs[owner-1]=0;
    if((results || owner) && FzeroLocalMenuReady()) {
      unsigned operator=results?0:owner-1;
      unsigned count=results?2:4,choice=FzeroLocalMenuSelection()%count;
      if(choice!=FzeroLocalMenuSelection())FzeroLocalSetMenuSelection(choice);
      if(pressed[operator]&0x30)FzeroLocalSetMenuSelection((choice+((pressed[operator]&0x10)?count-1:1))%count);
      if(!results && (pressed[operator]&1)) {FzeroLocalSetMenuOwner(0);return false;}
      if(pressed[operator]&(results?0x109:0x108)) {
        choice=FzeroLocalMenuSelection();
        FzeroLocalSetMenuOwner(0);
        if(!results && choice==0)return false;
        if(!results && choice==3) {
          state.phase=FZERO_ONLINE_TITLE;state.ready=0;state.release=(uint8_t)FzeroVersusReadyMask(state.config);
          for(unsigned i=0;i<state.config.player_count;++i) {state.previous[i]=4095;}memset(inputs,0,state.config.player_count*sizeof(*inputs));return true;
        }
        if(!results)--choice;
        if(choice==0) {
          if(results)FzeroVersusRematch(race);else FzeroVersusRestart(race);
          for(unsigned i=0;i<state.config.player_count;++i)FzeroVersusReady(race,i,true);
        } else if(choice==1) {
          state.phase=FZERO_ONLINE_MACHINES;state.ready=0;
        }
        FzeroLocalSetMenuSelection(0);for(unsigned i=0;i<state.config.player_count;++i) {state.previous[i]=4095;}state.release=(uint8_t)FzeroVersusReadyMask(state.config);memset(inputs,0,state.config.player_count*sizeof(*inputs));
      }
    }
    return state.phase!=FZERO_ONLINE_RACE;
  }
  if(state.phase==FZERO_ONLINE_MACHINES) {
    for(unsigned i=0;i<state.config.player_count;++i) {
      if(pressed[i]&1) {if(state.ready&(1u<<i))state.ready&=~(1u<<i);else if(!i) {state.phase=FZERO_ONLINE_TITLE;state.release=(uint8_t)FzeroVersusReadyMask(state.config);}}
      bool sliding=state.slide[i] && state.tick-state.selection_tick[i]<12;
      if(!(state.ready&(1u<<i)) && !sliding && (pressed[i]&0xf4)) {
        int slide;state.config.machine[i]=(uint8_t)FzeroMachineMove(state.config.machine[i],pressed[i],&slide);
        state.slide[i]=(uint8_t)(slide<0?1:slide>0?2:0);state.selection_tick[i]=state.tick;
      }
      if(!sliding && (pressed[i]&0x108))state.ready|=1u<<i;
    }
    if(state.ready==FzeroVersusReadyMask(state.config))state.phase=FZERO_ONLINE_CUP;
  } else {
    if(pressed[0]&1) {back();return true;}
    if(pressed[0]&0x30) {
      unsigned count,selected;int delta=(pressed[0]&0x10)?-1:1;
      if(state.phase==FZERO_ONLINE_CUP) {
        count=FzeroLocalCupCount();state.cup=(uint16_t)((state.cup+count+delta)%count);
      } else {
        count=FzeroLocalCourseCount(state.cup);FzeroLocalCourseLocation(state.config.course,NULL,&selected);
        state.config.course=(uint16_t)FzeroLocalCourseId(state.cup,(selected+count+delta)%count);
      }
    }
    if(pressed[0]&0x108) {
      if(state.phase==FZERO_ONLINE_CUP) {
        unsigned cup;FzeroLocalCourseLocation(state.config.course,&cup,NULL);
        if(cup!=state.cup)state.config.course=(uint16_t)FzeroLocalCourseId(state.cup,0);
        state.phase=FZERO_ONLINE_COURSE;
      } else {
        state.phase=FZERO_ONLINE_LOADING;state.release=(uint8_t)FzeroVersusReadyMask(state.config);
        if(!FzeroOnlineLoadCourse(state.config)) {state.failed=1;leave();}
      }
    }
  }
  return true;
}
static void put(uint8_t *p,uint64_t v,unsigned n) {for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint64_t get(const uint8_t *p,unsigned n) {uint64_t v=0;for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(i*8);return v;}
void FzeroOnlineSave(uint8_t out[FZERO_ONLINE_STATE_SIZE]) {
  memset(out,0,FZERO_ONLINE_STATE_SIZE);memcpy(out,"FZONLINE",8);
  put(out+8,FZERO_ONLINE_PROTOCOL,4);put(out+12,state.tick,8);put(out+20,state.exit_tick,8);
  put(out+28,state.config.course,2);out[30]=state.config.machine[0];out[31]=state.config.machine[1];
  put(out+32,state.previous[0],2);put(out+34,state.previous[1],2);put(out+36,state.cup,2);
  out[38]=state.phase;out[39]=state.ready;out[40]=state.aspect;
  out[43]=state.failed;out[60]=state.release;
  for(unsigned i=0;i<2;++i) {put(out+44+i*8,state.selection_tick[i],8);out[61+i]=state.slide[i];}
  out[63]=state.config.player_count;
  for(unsigned i=2;i<4;++i) {unsigned at=64+(i-2)*12;out[at]=state.config.machine[i];put(out+at+1,state.previous[i],2);put(out+at+3,state.selection_tick[i],8);out[at+11]=state.slide[i];}
}
bool FzeroOnlineValidate(const uint8_t in[FZERO_ONLINE_STATE_SIZE]) {
  if(memcmp(in,"FZONLINE",8) || get(in+8,4)!=FZERO_ONLINE_PROTOCOL ||
      !FzeroLocalCourseLocation((unsigned)get(in+28,2),NULL,NULL) || in[30]>=FZERO_MACHINE_COUNT || in[31]>=FZERO_MACHINE_COUNT ||
      get(in+32,2)>4095 || get(in+34,2)>4095 || get(in+36,2)>=FzeroLocalCupCount() ||
      in[38]>FZERO_ONLINE_EXIT || (in[63]<2 || in[63]>4 || in[39]>((1u<<in[63])-1)) || in[40]>2 || in[43]>1 || get(in+20,8)>get(in+12,8))return false;
  if(in[41] || in[42] || in[60]>((1u<<in[63])-1))return false;
  for(unsigned i=0;i<2;++i)if(get(in+44+i*8,8)>get(in+12,8))return false;
  if(in[61]>2 || in[62]>2)return false;
  for(unsigned i=2;i<4;++i) {unsigned at=64+(i-2)*12;
    if(in[at]>=FZERO_MACHINE_COUNT || get(in+at+1,2)>4095 || get(in+at+3,8)>get(in+12,8) || in[at+11]>2)return false;
  }
  for(unsigned i=88;i<FZERO_ONLINE_STATE_SIZE;++i)if(in[i])return false;
  return true;
}
bool FzeroOnlineRestore(const uint8_t in[FZERO_ONLINE_STATE_SIZE]) {
  if(!enabled || !FzeroOnlineValidate(in) || in[40]!=state.aspect || in[63]!=state.config.player_count)return false;
  state.tick=get(in+12,8);state.exit_tick=get(in+20,8);state.config.course=(uint16_t)get(in+28,2);
  state.config.machine[0]=in[30];state.config.machine[1]=in[31];
  state.previous[0]=(uint16_t)get(in+32,2);state.previous[1]=(uint16_t)get(in+34,2);state.cup=(uint16_t)get(in+36,2);
  state.phase=in[38];state.ready=in[39];state.failed=in[43];state.release=in[60];
  for(unsigned i=0;i<2;++i) {state.selection_tick[i]=get(in+44+i*8,8);state.slide[i]=in[61+i];}
  for(unsigned i=2;i<4;++i) {unsigned at=64+(i-2)*12;state.config.machine[i]=in[at];state.previous[i]=(uint16_t)get(in+at+1,2);state.selection_tick[i]=get(in+at+3,8);state.slide[i]=in[at+11];}
  return true;
}
void FzeroOnlineDraw(uint32_t *pixels,FzeroViewport viewport,unsigned scale) {
  if(!pixels || !enabled)return;
  FzeroUiCanvas c={pixels,viewport.width*(int)scale,224*(int)scale,scale};
  const FzeroNativeUi *ui=FzeroLocalUi();int center=viewport.width/2,left=center-128;
  FzeroUiRect(c,0,0,viewport.width,224,0xff000000);
  if(state.phase==FZERO_ONLINE_LOADING || state.phase==FZERO_ONLINE_EXIT) {
    FzeroUiCenter(ui,c,80,"VS MODE",FZERO_UI_MENU,FZERO_UI_YELLOW);
    FzeroUiCenter(ui,c,112,state.failed?"CONNECTION FAILED":state.phase==FZERO_ONLINE_LOADING?"SELECTED COURSE":
        state.phase==FZERO_ONLINE_EXIT?"RETURN TO LOBBY":"CONNECTING",FZERO_UI_MENU,FZERO_UI_CYAN);
    return;
  }
  if(state.phase==FZERO_ONLINE_MACHINES) {
    unsigned p=view;
    FzeroUiMachineSelectScroll(ui,c,state.config.machine[p],state.tick-state.selection_tick[p],p,true,(state.ready&(1u<<p))!=0,
        state.slide[p]==1?-1:state.slide[p]==2?1:0);
    for(unsigned other=0,row=0;other<state.config.player_count;++other) {
    if(other==p)continue;
    int badge_y=184+(int)row++*12;
    FzeroUiPlayerTag(ui,c,left+8,badge_y,other,other?FZERO_UI_YELLOW:FZERO_UI_CYAN);
    FzeroUiMachine(ui,c,left+32,badge_y,state.config.machine[other],true);
    FzeroUiText(ui,c,left+64,badge_y+4,(state.ready&(1u<<other))?"READY":FzeroUiMachineName(state.config.machine[other]),
        FZERO_UI_SMALL,FZERO_UI_CYAN);
    }
    return;
  }
  bool cup=state.phase==FZERO_ONLINE_CUP;unsigned selected=state.cup;
  FzeroUiCenter(ui,c,8,"VS MODE",FZERO_UI_MENU,FZERO_UI_YELLOW);
  FzeroUiFrame(ui,c,left+8,48,240,168);
  FzeroUiCenter(ui,c,32,cup?"SELECT LEAGUE":"SELECT COURSE",FZERO_UI_MENU,FZERO_UI_YELLOW);
  if(!cup) {
    FzeroLocalCourseLocation(state.config.course,NULL,&selected);
    char label[25];snprintf(label,sizeof(label),"%.24s",FzeroLocalCupName(state.cup));
    FzeroUiCenter(ui,c,56,label,FZERO_UI_SMALL,FZERO_UI_YELLOW);
  }
  unsigned count=cup?FzeroLocalCupCount():FzeroLocalCourseCount(state.cup),first=selected<5?0:selected-4;
  for(unsigned row=0;row<5 && first+row<count;++row) {
    unsigned item=first+row;bool chosen=item==selected;int y=80+(int)row*24;
    if(chosen)FzeroUiTile(ui,c,left+24,y+4,0x2f,FZERO_UI_YELLOW,true,false);
    const char *name=cup?FzeroLocalCupName(item):FzeroLocalCourseName(FzeroLocalCourseId(state.cup,item));
    unsigned len=(unsigned)strlen(name),offset=0;
    if(chosen && len>24) {unsigned step=(unsigned)(state.tick/20)%(len-24+12);offset=step<6?0:step-6;if(offset>len-24)offset=len-24;}
    char label[25];snprintf(label,sizeof(label),"%.24s",name+offset);
    FzeroUiText(ui,c,left+40,y,label,FZERO_UI_MENU,chosen?FZERO_UI_CYAN:FZERO_UI_GREY);
  }
}

void FzeroOnlineBegin(unsigned aspect,unsigned slot) {FzeroOnlineBeginPlayers(aspect,slot,2);}
bool FzeroOnlineTick(uint32_t *packed) {
  uint16_t inputs[4]={(uint16_t)(*packed&4095),(uint16_t)((*packed>>12)&4095),0,0};
  bool consumed=FzeroOnlineTickInputs(inputs);*packed=inputs[0]|((uint32_t)inputs[1]<<12)|(3u<<30);return consumed;
}
