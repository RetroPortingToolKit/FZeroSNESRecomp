/* Private-ROM integration. A native WRAM/capture fixture supplies authored
 * CPU lines/art. Position-only fixtures exercise contacts, never race outcomes. */
#include "fzero_versus.h"
#include "fzero_ship.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"traffic:%d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint8_t rom[0x80000],native[0x20000],world[0x20000];
static uint8_t read_rom(void *context,uint32_t at) {
  (void)context;CHECK(!(at>>24) && ((at>>16)&127)<16 && (at&65535)>=0x8000);
  return rom[((at>>16)&127)*0x8000+(at&32767)];
}
static unsigned word(unsigned at) {return native[at]|(unsigned)native[at+1]<<8;}
static FzeroVersus *session(FzeroActorTables tables) {
  FzeroActor spawns[2]={{0},{0}};
  for(unsigned i=0;i<2;++i) {
    unsigned x=i*2;spawns[i].x=(uint16_t)word(0xb70+x);spawns[i].y=(uint16_t)word(0xb90+x);
    spawns[i].facing=(uint16_t)word(0xbd0+x);spawns[i].checkpoint=native[0xd00+x];
  }
  if(spawns[0].x==spawns[1].x && spawns[0].y==spawns[1].y)spawns[1].x=(spawns[0].x+16)&8191;
  FzeroVersus *s=FzeroVersusCreate((FzeroVersusConfig){0,{0,1}},world,spawns,tables);CHECK(s);
  CHECK(FzeroVersusEnableTraffic(s,native));FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);return s;
}
int main(int argc,char **argv) {
  CHECK(argc==3);FILE *f=fopen(argv[1],"rb");CHECK(f && fread(rom,sizeof(rom),1,f)==1);fclose(f);
  f=fopen(argv[2],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);
  CHECK(size==sizeof(native) || size>sizeof(native)+8);
  CHECK(!fseek(f,size==(long)sizeof(native)?0:-(long)(sizeof(native)+8),size==(long)sizeof(native)?SEEK_SET:SEEK_END));
  CHECK(fread(native,sizeof(native),1,f)==1);fclose(f);
  CHECK(FzeroActorCaptureWorld(world,native));FzeroActorTables tables={read_rom,NULL,0};
  FzeroVersus *s=session(tables);uint16_t held[2]={0,0};unsigned moved=0,active=0,maximum=0,moving[4]={0};
  for(unsigned tick=0;tick<900;++tick) {
    FzeroTrafficPose before[FZERO_TRAFFIC_COUNT];memcpy(before,FzeroVersusCompleted(s)->traffic,sizeof(before));
    CHECK(FzeroVersusTick(s,held));const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);
    unsigned field=0;for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i)if(v->traffic[i].active) {
      ++field;
      ++active;if(before[i].active && (before[i].actor.x!=v->traffic[i].actor.x || before[i].actor.y!=v->traffic[i].actor.y)) {++moved;++moving[i];}
    }
    if(field>maximum)maximum=field;
  }
  CHECK(active>100 && moved>100 && maximum==FZERO_TRAFFIC_COUNT);
  printf("moving ticks per bumper: %u %u %u %u\n",moving[0],moving[1],moving[2],moving[3]);
  for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i)CHECK(moving[i]>100);
  const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);
  CHECK(v->phase==FZERO_VS_RACING && !v->winner && !v->racers[0].completed_laps && !v->racers[1].completed_laps);
  size_t length=FzeroVersusSaveSize();uint8_t *saved=malloc(length);CHECK(saved);
  CHECK(FzeroVersusSave(s,saved,length));
  for(unsigned i=0;i<120;++i)CHECK(FzeroVersusTick(s,held));uint64_t expected=FzeroVersusHash(s);
  CHECK(FzeroVersusRestore(s,saved,length,NULL));
  for(unsigned i=0;i<120;++i)CHECK(FzeroVersusTick(s,held));CHECK(FzeroVersusHash(s)==expected);
  saved[length-1]^=1;CHECK(!FzeroVersusRestore(s,saved,length,NULL) && FzeroVersusHash(s)==expected);
  /* Bumpers respond to both humans, with independent contact latches. */
  FzeroTraffic traffic;CHECK(FzeroTrafficInit(&traffic,world,native,tables));
  FzeroActor humans[2]={v->racers[0].actor,v->racers[1].actor};
  CHECK(FzeroTrafficTick(&traffic,world,humans,1,tables));FzeroTrafficPose actors[FZERO_TRAFFIC_COUNT];FzeroTrafficDescribe(&traffic,actors);
  unsigned selected=0;while(selected<FZERO_TRAFFIC_COUNT && !actors[selected].active)++selected;CHECK(selected<FZERO_TRAFFIC_COUNT);
  for(unsigned player=0;player<2;++player) {
    FzeroActor a=actors[selected].actor;a.machine=(uint8_t)player;a.speed=(uint16_t)(0x900+player*0x500);a.health=0x800;
    FzeroActor other=humans[1-player];FzeroTrafficContact(&traffic,player,&a,tables);
    if(a.health==0x800 || !traffic.contacts[player][selected])fprintf(stderr,"contact player=%u health=%u latch=%u pose=%u,%u height=%04x facing=%04x\n",player,a.health,traffic.contacts[player][selected],a.x,a.y,a.height,a.facing);
    CHECK(a.health<0x800 && traffic.contacts[player][selected]);
    CHECK(!memcmp(&other,&humans[1-player],sizeof(other)));
  }
  /* Same world actor projects and paints in two independent physical views. */
  for(unsigned player=0;player<2;++player) {
    FzeroActor camera=actors[selected].actor;camera.x=(camera.x+player*8)&8191;FzeroShip image;
    CHECK(FzeroShipBumper(&image,&camera,&actors[selected].actor,world,tables) && image.visible);
    unsigned pixels=0;for(unsigned i=0;i<4096;++i)pixels+=image.pixels[i]!=0;CHECK(pixels>20);
  }
  /* Native NPC uploads cover every orientation and distance band. */
  unsigned bands=0;FzeroActor camera={0};camera.x=2048;camera.y=2048;
  for(unsigned distance=0;distance<=640;distance+=2)for(unsigned angle=0;angle<192;angle+=4) {
    FzeroActor a=camera;a.y=(uint16_t)(2048-distance);a.facing=(uint16_t)(angle<<8);FzeroShip image;
    CHECK(FzeroShipBumper(&image,&camera,&a,world,tables));
    if(image.visible) {bands|=1u<<image.size;unsigned pixels=0;
      for(unsigned i=0;i<4096;++i)pixels+=image.pixels[i]!=0;CHECK(pixels);
    }
  }
  CHECK(bands==511);
  CHECK(FzeroTrafficValid(&traffic,world));traffic.ram[0xb0a]=128;CHECK(!FzeroTrafficValid(&traffic,world));
  CHECK(FzeroVersusRestart(s));FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
  FzeroVersus *fresh=session(tables);
  for(unsigned i=0;i<600;++i){CHECK(FzeroVersusTick(s,held));CHECK(FzeroVersusTick(fresh,held));}
  CHECK(FzeroVersusHash(s)==FzeroVersusHash(fresh));
  FzeroVersusDestroy(fresh);FzeroVersusDestroy(s);free(saved);
  printf("PASS native traffic: %u bumpers, both contacts/views, all sprite bands/angles, save/replay, atomic invalid rejection, restart, human-only results (%u moving samples)\n",maximum,moved);
  return 0;
}
