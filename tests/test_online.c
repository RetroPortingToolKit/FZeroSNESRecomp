#include "fzero_online.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
/* Real shared race and online input reducer. Only native course loading and
 * the asset catalog are fixtures; no transport, SDL or private ROM required. */
static FzeroVersus *race;
static unsigned selection,loads;
static uint8_t table(void *ctx,uint32_t address) {(void)ctx;(void)address;return 0;}
unsigned FzeroLocalCupCount(void) {return 18;}
unsigned FzeroLocalCourseCount(unsigned cup) {return cup<18?20:0;}
unsigned FzeroLocalCourseId(unsigned cup,unsigned order) {return cup*20+order;}
bool FzeroLocalCourseLocation(unsigned id,unsigned *cup,unsigned *order) {
  if(id>=360)return false;
  if(cup)*cup=id/20;
  if(order)*order=id%20;
  return true;
}
const char *FzeroLocalCupName(unsigned cup) {(void)cup;return "LEAGUE";}
const char *FzeroLocalCourseName(unsigned id) {(void)id;return "COURSE";}
FzeroVersus *FzeroLocalSession(void) {return race;}
const FzeroNativeUi *FzeroLocalUi(void) {return NULL;}
unsigned FzeroLocalMenuSelection(void) {return selection;}
void FzeroLocalSetMenuSelection(unsigned n) {selection=n;}
bool FzeroLocalMenuReady(void) {return true;}
bool FzeroOnlineLoadCourse(FzeroVersusConfig config) {
  static const uint8_t world[0x20000];FzeroActor spawn[2]={{0},{0}};spawn[1].x=64;
  FzeroVersusDestroy(race);race=FzeroVersusCreate(config,world,spawn,(FzeroActorTables){table,NULL});
  ++loads;return race!=NULL;
}
static void tick(unsigned p1,unsigned p2) {
  uint32_t packed=p1|(p2<<12);FzeroOnlineTick(&packed);
}
static void press(unsigned p1,unsigned p2) {tick(p1,p2);tick(0,0);}
static void image(uint8_t out[FZERO_ONLINE_STATE_SIZE]) {FzeroOnlineSave(out);CHECK(FzeroOnlineValidate(out));}
static void choose(void) {
  press(0x20,0);CHECK(FzeroOnlineCompleted()->config.machine[0]==2);
  uint64_t p1_spin=FzeroOnlineCompleted()->selection_tick[0];
  press(0,0x20);CHECK(FzeroOnlineCompleted()->config.machine[1]==3);
  CHECK(FzeroOnlineCompleted()->selection_tick[0]==p1_spin);
  CHECK(FzeroOnlineCompleted()->selection_tick[1]>p1_spin);
  press(8,0);CHECK(FzeroOnlineCompleted()->ready==1);
  press(0,8);CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_CUP);
}
int main(void) {
  uint8_t initial[FZERO_ONLINE_STATE_SIZE],same[FZERO_ONLINE_STATE_SIZE],saved[FZERO_ONLINE_STATE_SIZE],bad[FZERO_ONLINE_STATE_SIZE];
  for(unsigned aspect=0;aspect<3;++aspect) {
    FzeroOnlineBegin(aspect,0);image(initial);
    FzeroOnlineBegin(aspect,1);image(same);CHECK(!memcmp(initial,same,sizeof(same)));
    CHECK(FzeroOnlineView()==1);
    CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_TITLE);
    press(8,8);CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_TITLE);
    image(saved);FzeroOnlineEnterVs();tick(0,0);
    /* Back from an unconfirmed selector resumes the native title. Its
     * owner-facing navigation remains outside this pure reducer test. */
    press(1,0);CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_TITLE);
    CHECK(FzeroOnlineRestore(saved));image(same);CHECK(!memcmp(saved,same,sizeof(saved)));
    FzeroOnlineEnterVs();tick(0,0);choose();image(saved);
    /* A guest cannot change the host's league/course decision. */
    press(0,0x20);CHECK(FzeroOnlineCompleted()->cup==0);
    press(0,8);CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_CUP);
    for(unsigned i=0;i<17;++i)press(0x20,0);
    press(8,0);CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_COURSE);
    CHECK(FzeroOnlineCompleted()->config.course==340);
    for(unsigned i=0;i<19;++i)press(0x20,0);
    CHECK(FzeroOnlineCompleted()->config.course==359);image(initial);
    CHECK(FzeroOnlineRestore(saved));CHECK(FzeroOnlineView()==1);
    press(0,0x20);press(0,8);
    for(unsigned i=0;i<17;++i)press(0x20,0);
    press(8,0);for(unsigned i=0;i<19;++i)press(0x20,0);
    /* Restore/replay reproduces menu edges, timers and wide course IDs. */
    image(same);CHECK(!memcmp(initial,same,sizeof(same)));
    memcpy(bad,same,sizeof(bad));bad[40]=(uint8_t)((aspect+1)%3);
    CHECK(!FzeroOnlineRestore(bad));image(saved);CHECK(!memcmp(same,saved,sizeof(same)));
    memcpy(bad,same,sizeof(bad));bad[38]=255;CHECK(!FzeroOnlineValidate(bad));
    memcpy(bad,same,sizeof(bad));bad[28]=0xff;bad[29]=0xff;CHECK(!FzeroOnlineValidate(bad));
    for(unsigned offset=41;offset<96;++offset)if(offset==41 || offset==42 || offset>60) {
      memcpy(bad,same,sizeof(bad));bad[offset]=1;CHECK(!FzeroOnlineValidate(bad));
    }
    memcpy(bad,same,sizeof(bad));memset(bad+44,255,8);CHECK(!FzeroOnlineValidate(bad));
    press(8,0);CHECK(loads==aspect+1);CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_RACE);
    CHECK(FzeroVersusCompleted(race)->config.course==359);
    FzeroVersusPause(race,FZERO_VS_PAUSE_MANUAL);selection=1;
    press(0,8);CHECK(FzeroVersusCompleted(race)->pause);
    press(8,0);CHECK(!FzeroVersusCompleted(race)->pause);
    CHECK(FzeroVersusCompleted(race)->phase==FZERO_VS_COUNTDOWN);
    FzeroVersusPause(race,FZERO_VS_PAUSE_MANUAL);selection=3;press(8,0);
    CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_EXIT);
    CHECK(FzeroOnlineCompleted()->exit_tick>0);image(initial);
    tick(0x20,8);CHECK(FzeroOnlineCompleted()->phase==FZERO_ONLINE_EXIT);
    CHECK(FzeroOnlineRestore(initial));image(same);CHECK(!memcmp(initial,same,sizeof(same)));
    FzeroOnlineEnd();CHECK(!FzeroOnlineEnabled());CHECK(FzeroOnlineView()==-1);
    CHECK(!FzeroOnlineRestore(initial));FzeroVersusDestroy(race);race=NULL;
  }
  puts("online: authoritative menu inputs, late courses, rollback images, rematch and exit PASS");return 0;
}
