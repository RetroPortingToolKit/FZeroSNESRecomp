#include "fzero_online.h"
#include <stdio.h>
#include <string.h>

static bool enabled;
static unsigned view;
static FzeroOnlineState state;
void FzeroOnlineBegin(unsigned aspect,unsigned slot) {
  memset(&state,0,sizeof(state));state.aspect=(uint8_t)aspect;
  state.config=(FzeroVersusConfig){0,{0,1}};state.phase=FZERO_ONLINE_TITLE;view=slot;enabled=true;
}
void FzeroOnlineEnterVs(void) {
  if(!enabled || state.phase!=FZERO_ONLINE_TITLE)return;
  state.phase=FZERO_ONLINE_MACHINES;state.ready=0;state.release=3;
  for(unsigned i=0;i<2;++i)state.selection_tick[i]=state.tick;
  state.previous[0]=state.previous[1]=4095;
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
bool FzeroOnlineTick(uint32_t *packed) {
  if(!enabled)return false;
  ++state.tick;
  uint16_t held[2]={(uint16_t)(*packed&4095),(uint16_t)((*packed>>12)&4095)},pressed[2];
  for(unsigned i=0;i<2;++i) {
    if(state.release&(1u<<i)) {if(!held[i])state.release&=~(1u<<i);held[i]=0;}
  }
  *packed=held[0]|((uint32_t)held[1]<<12)|(3u<<30);
  for(unsigned i=0;i<2;++i) {pressed[i]=held[i]&~state.previous[i];state.previous[i]=held[i];}
  if(state.phase==FZERO_ONLINE_EXIT)return true;
  if(state.phase==FZERO_ONLINE_TITLE)return false;
  if(state.phase==FZERO_ONLINE_LOADING) {
    if(FzeroLocalSession()) {state.phase=FZERO_ONLINE_RACE;state.previous[0]=state.previous[1]=4095;}
    return false;
  }
  if(state.phase==FZERO_ONLINE_RACE) {
    FzeroVersus *race=FzeroLocalSession();const FzeroVersusSnapshot *s=FzeroVersusCompleted(race);
    if(!s)return false;
    /* Either human may pause. Host alone navigates shared pause/results and
     * changes course; its controls are ordinary synchronized input rows. */
    if((s->pause || s->phase==FZERO_VS_RESULTS) && FzeroLocalMenuReady()) {
      bool pause=s->pause!=0;unsigned count=pause?4:3,choice=FzeroLocalMenuSelection();
      if(pressed[0]&0x30)FzeroLocalSetMenuSelection((choice+((pressed[0]&0x10)?count-1:1))%count);
      if(pressed[0]&0x109) {
        choice=FzeroLocalMenuSelection();
        if(pause && choice==0)FzeroVersusResume(race);
        else if(choice==(pause?1u:0u)) {
          if(pause){FzeroVersusEndRace(race);FzeroVersusResume(race);}
          FzeroVersusRematch(race);FzeroVersusReady(race,0,true);FzeroVersusReady(race,1,true);
        } else if(choice==(pause?2u:1u)) {
          state.phase=FZERO_ONLINE_MACHINES;state.ready=0;
        } else leave();
        FzeroLocalSetMenuSelection(0);state.previous[0]=state.previous[1]=4095;state.release=3;*packed=3u<<30;
      }
    }
    return state.phase!=FZERO_ONLINE_RACE;
  }
  if(state.phase==FZERO_ONLINE_MACHINES) {
    static const unsigned order[4]={0,2,1,3};
    for(unsigned i=0;i<2;++i) {
      if(pressed[i]&1) {if(state.ready&(1u<<i))state.ready&=~(1u<<i);else if(!i) {state.phase=FZERO_ONLINE_TITLE;state.release=3;}}
      if(!(state.ready&(1u<<i)) && (pressed[i]&0x30)) {
        unsigned row=0;while(order[row]!=state.config.machine[i])++row;
        state.config.machine[i]=(uint8_t)order[(row+((pressed[i]&0x10)?3:1))%4];
        state.selection_tick[i]=state.tick;
      }
      if(pressed[i]&0x108)state.ready|=1u<<i;
    }
    if(state.ready==3)state.phase=FZERO_ONLINE_CUP;
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
        state.phase=FZERO_ONLINE_LOADING;state.release=3;
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
  for(unsigned i=0;i<2;++i)put(out+44+i*8,state.selection_tick[i],8);
}
bool FzeroOnlineValidate(const uint8_t in[FZERO_ONLINE_STATE_SIZE]) {
  if(memcmp(in,"FZONLINE",8) || get(in+8,4)!=FZERO_ONLINE_PROTOCOL ||
      !FzeroLocalCourseLocation((unsigned)get(in+28,2),NULL,NULL) || in[30]>=4 || in[31]>=4 ||
      get(in+32,2)>4095 || get(in+34,2)>4095 || get(in+36,2)>=FzeroLocalCupCount() ||
      in[38]>FZERO_ONLINE_EXIT || in[39]>3 || in[40]>2 || in[43]>1 || get(in+20,8)>get(in+12,8))return false;
  if(in[41] || in[42] || in[60]>3)return false;
  for(unsigned i=0;i<2;++i)if(get(in+44+i*8,8)>get(in+12,8))return false;
  for(unsigned i=61;i<FZERO_ONLINE_STATE_SIZE;++i)if(in[i])return false;
  return true;
}
bool FzeroOnlineRestore(const uint8_t in[FZERO_ONLINE_STATE_SIZE]) {
  if(!enabled || !FzeroOnlineValidate(in) || in[40]!=state.aspect)return false;
  state.tick=get(in+12,8);state.exit_tick=get(in+20,8);state.config.course=(uint16_t)get(in+28,2);
  state.config.machine[0]=in[30];state.config.machine[1]=in[31];
  state.previous[0]=(uint16_t)get(in+32,2);state.previous[1]=(uint16_t)get(in+34,2);state.cup=(uint16_t)get(in+36,2);
  state.phase=in[38];state.ready=in[39];state.failed=in[43];state.release=in[60];
  for(unsigned i=0;i<2;++i)state.selection_tick[i]=get(in+44+i*8,8);
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
    FzeroUiMachineSelect(ui,c,state.config.machine[p],state.tick-state.selection_tick[p],p,true,(state.ready&(1u<<p))!=0);
    unsigned other=1-p;
    FzeroUiPlayerTag(ui,c,left+8,208,other,other?FZERO_UI_YELLOW:FZERO_UI_CYAN);
    FzeroUiMachine(ui,c,left+32,208,state.config.machine[other],true);
    FzeroUiText(ui,c,left+64,212,(state.ready&(1u<<other))?"READY":FzeroUiMachineName(state.config.machine[other]),
        FZERO_UI_SMALL,FZERO_UI_CYAN);
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
