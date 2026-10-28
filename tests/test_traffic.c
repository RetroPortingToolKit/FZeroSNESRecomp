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
static unsigned tw(const FzeroTraffic *t,unsigned at) {return t->ram[at]|(unsigned)t->ram[at+1]<<8;}
static void tp(FzeroTraffic *t,unsigned at,unsigned value) {t->ram[at]=(uint8_t)value;t->ram[at+1]=(uint8_t)(value>>8);}
static void test_terrain(FzeroActorTables tables,bool require_special) {
  unsigned found[4]={0};
  for(unsigned kind=0;kind<4;++kind) {
    FzeroActor at={0};bool located=false;
    for(unsigned y=4;y<4096 && !located;y+=8)for(unsigned x=4;x<8192;x+=8) {
      at.x=(uint16_t)x;at.y=(uint16_t)y;at.surface_id=FzeroActorSampleSurface(world,at.x,at.y);
      FzeroActorCourseSurface(&at,world);
      bool match=kind==0?(at.surface&64)!=0:kind==1?
          ((world[FZERO_WORLD_FEATURES]&FZERO_TERRAIN_UP_MAGNETS) && (at.surface&8) &&
            (at.surface_id==0xb6 || (at.surface_id>=0xcc && at.surface_id<0xd0))):
          (at.ground_flags&128)!=0;
      if(match){located=true;break;}
    }
    if(!located)continue;
    FzeroTraffic traffic;CHECK(FzeroTrafficInit(&traffic,world,native,tables));
    FzeroActor humans[2]={{.x=100,.y=100},{.x=200,.y=200}};
    CHECK(FzeroTrafficTick(&traffic,world,humans,1,tables));
    unsigned slot=2;traffic.present|=1;traffic.ram[0xb00+slot]=0xc1;traffic.ram[0x1140+slot]=0;
    tp(&traffic,0xb70+slot,at.x);tp(&traffic,0xb90+slot,at.y);tp(&traffic,0xb20+slot,0x500);
    traffic.ram[0xd51+slot]=kind==1 || kind==3?0x80:0;
    tp(&traffic,0xbc0+slot,kind==1?0x2000:kind==3?0x210:0x600);
    tp(&traffic,0xbb0+slot,kind==1 || kind==3?0xff00:0);
    /* Prevent population recycling during this one terrain evaluation. */
    humans[0].checkpoint=humans[1].checkpoint=traffic.ram[0xd00+slot];
    CHECK(FzeroTrafficTick(&traffic,world,humans,2,tables));
    CHECK(traffic.ram[0xcd0+slot]==at.surface_id);
    if(kind==0)CHECK((traffic.ram[0xd51+slot]&128) && tw(&traffic,0xbc0+slot)>0x600);
    if(kind==1)CHECK((int16_t)tw(&traffic,0xbb0+slot)>-256);
    if(kind>=2)CHECK(!(traffic.ram[0xb00+slot]&128));
    found[kind]=1;
    printf("terrain %u: tile=%02x position=%u,%u state=%02x height=%04x velocity=%04x PASS\n",kind,at.surface_id,at.x,at.y,
        traffic.ram[0xd51+slot],tw(&traffic,0xbc0+slot),tw(&traffic,0xbb0+slot));
  }
  CHECK(found[2] && found[3]);
  if(require_special)CHECK(found[0] && found[1]);
  printf("actual special tiles: jump=%u up-magnet=%u (absent types are not applicable)\n",found[0],found[1]);
}
static unsigned distance(const FzeroActor *a,const FzeroActor *b) {
  unsigned x=(a->x+8192-b->x)&8191,y=(a->y+4096-b->y)&4095;
  if(x>4096)x=8192-x;if(y>2048)y=4096-y;return x*x+y*y;
}
static void test_population(FzeroActorTables tables) {
  FzeroTraffic traffic;CHECK(FzeroTrafficInit(&traffic,world,native,tables));
  unsigned count=(unsigned)world[0xad]+1,encounters[5]={0},brown=0,red=0;uint64_t tick=0;
  /* Position-only route sweep: tests population on five circuits, not human
   * lap counting or natural driving. No session/result fields are written. */
  for(unsigned circuit=0;circuit<5;++circuit)for(unsigned gate=0;gate<count;++gate) {
    FzeroActor humans[2]={{0},{0}};
    for(unsigned p=0;p<2;++p) {
      humans[p].x=(uint16_t)word(0x1200+gate*2);humans[p].y=(uint16_t)word(0x1400+gate*2);
      humans[p].facing=(uint16_t)native[0x1600+gate]<<8;humans[p].checkpoint=(uint8_t)gate;
      humans[p].x=(humans[p].x+p*16)&8191;humans[p].speed=0xa00;
    }
    for(unsigned frame=0;frame<45;++frame) {
      CHECK(FzeroTrafficTick(&traffic,world,humans,++tick,tables));CHECK(FzeroTrafficValid(&traffic,world));
      FzeroTrafficPose poses[FZERO_TRAFFIC_COUNT];FzeroTrafficDescribe(&traffic,poses);
      for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i)if(poses[i].active) {
        if(distance(&poses[i].actor,&humans[0])<1200*1200)++encounters[circuit];
        if(poses[i].explosive)++red;else ++brown;
      }
    }
  }
  printf("five route circuits nearby traffic: %u %u %u %u %u; brown=%u red=%u\n",
      encounters[0],encounters[1],encounters[2],encounters[3],encounters[4],brown,red);
  for(unsigned i=0;i<5;++i)CHECK(encounters[i]>100);
  CHECK(brown>100 && red>100);
}
static void picture(const char *name,const FzeroShip *image) {
  FILE *f=fopen(name,"wb");CHECK(f);fprintf(f,"P6\n64 64\n255\n");
  for(unsigned i=0;i<4096;++i){unsigned c=image->pixels[i];uint8_t rgb[3]={c>>16,c>>8,c};CHECK(fwrite(rgb,3,1,f)==1);}CHECK(!fclose(f));
}
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
  CHECK(argc>=3 && argc<=5);FILE *f=fopen(argv[1],"rb");CHECK(f && fread(rom,sizeof(rom),1,f)==1);fclose(f);
  f=fopen(argv[2],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);
  CHECK(size==sizeof(native) || size>sizeof(native)+8);
  CHECK(!fseek(f,size==(long)sizeof(native)?0:-(long)(sizeof(native)+8),size==(long)sizeof(native)?SEEK_SET:SEEK_END));
  CHECK(fread(native,sizeof(native),1,f)==1);fclose(f);
  CHECK(FzeroActorCaptureWorld(world,native));
  if(argc>=4){f=fopen(argv[3],"rb");CHECK(f && fread(world,sizeof(world),1,f)==1);fclose(f);}
  FzeroActorTables tables={read_rom,NULL,0};
  test_terrain(tables,argc==5 && !strcmp(argv[4],"require-special"));
  test_population(tables);
  FzeroVersus *s=session(tables);uint16_t held[2]={0,0};unsigned moved=0,active=0,maximum=0,moving[4]={0},regular_moved=0,red_seen=0;
  for(unsigned tick=0;tick<900;++tick) {
    FzeroTrafficPose before[FZERO_TRAFFIC_COUNT];memcpy(before,FzeroVersusCompleted(s)->traffic,sizeof(before));
    CHECK(FzeroVersusTick(s,held));const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);
    unsigned field=0;for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i)if(v->traffic[i].active) {
      ++field;
      ++active;if(before[i].active && (before[i].actor.x!=v->traffic[i].actor.x || before[i].actor.y!=v->traffic[i].actor.y)) {++moved;++moving[i];}
      if(v->traffic[i].explosive)++red_seen;
      else if(before[i].active && (before[i].actor.x!=v->traffic[i].actor.x || before[i].actor.y!=v->traffic[i].actor.y))++regular_moved;
    }
    if(field>maximum)maximum=field;
  }
  CHECK(active>100 && moved>100 && maximum==FZERO_TRAFFIC_COUNT);
  CHECK(regular_moved>300 && red_seen>0);
  printf("moving ticks per bumper: %u %u %u %u\n",moving[0],moving[1],moving[2],moving[3]);
  for(unsigned i=0;i<FZERO_TRAFFIC_COUNT;++i)CHECK(moving[i]>0);
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
  traffic.ram[0xb02+selected*2]|=32;traffic.ram[0x1142+selected*2]=0;
  for(unsigned player=0;player<2;++player) {
    FzeroActor a=actors[selected].actor;a.machine=(uint8_t)player;a.health=0x800;
    a.state=0;a.height=0x600;a.speed=0x777;traffic.contacts[player][selected]=0;
    FzeroTrafficContact(&traffic,player,&a,tables);
    CHECK(a.health==0x800-read_rom(NULL,0xfac1+player));
    CHECK(a.height_velocity==0x100 && a.black_flash==0x85 && (a.state&0x81)==0x81 && (a.no_control&255)==0x20);
    CHECK(a.sound_port3&8);CHECK(a.speed==0x777);
  }
  FzeroTrafficPose consumed[FZERO_TRAFFIC_COUNT];FzeroTrafficDescribe(&traffic,consumed);CHECK(!consumed[selected].active);
  {
    /* A consumed explosive must remain a valid snapshot even when no safe
     * replacement exists. Terrain fixtures do not change the real course. */
    FzeroTraffic blocked=traffic;uint8_t *unsafe=malloc(sizeof(world));CHECK(unsafe);memcpy(unsafe,world,sizeof(world));
    unsafe[FZERO_WORLD_RESOURCE]=1;memset(unsafe+FZERO_WORLD_TERRAIN+0x300,128,256);
    CHECK(FzeroTrafficTick(&blocked,unsafe,humans,3,tables));
    CHECK(FzeroTrafficTick(&blocked,unsafe,humans,4,tables));
    CHECK(!blocked.present && FzeroTrafficValid(&blocked,unsafe));free(unsafe);
  }
  /* Same world actor projects and paints in two independent physical views. */
  for(unsigned player=0;player<2;++player) {
    FzeroActor camera=actors[selected].actor;camera.x=(camera.x+player*8)&8191;FzeroShip image;
    CHECK(FzeroShipBumper(&image,&camera,&actors[selected].actor,world,tables,actors[selected].explosive) && image.visible);
    unsigned pixels=0;for(unsigned i=0;i<4096;++i)pixels+=image.pixels[i]!=0;CHECK(pixels>20);
  }
  /* Native NPC uploads cover every orientation and distance band. */
  unsigned bands=0;FzeroActor camera={0};camera.x=2048;camera.y=2048;
  for(unsigned distance=0;distance<=640;distance+=2)for(unsigned angle=0;angle<192;angle+=4) {
    FzeroActor a=camera;a.y=(uint16_t)(2048-distance);a.facing=(uint16_t)(angle<<8);FzeroShip image;
    CHECK(FzeroShipBumper(&image,&camera,&a,world,tables,false));
    if(image.visible) {bands|=1u<<image.size;unsigned pixels=0;
      for(unsigned i=0;i<4096;++i)pixels+=image.pixels[i]!=0;CHECK(pixels);
    }
  }
  CHECK(bands==511);
  {
    FzeroActor a=camera;FzeroShip regular,explosive;
    CHECK(FzeroShipBumper(&regular,&camera,&a,world,tables,false));
    CHECK(FzeroShipBumper(&explosive,&camera,&a,world,tables,true));
    CHECK(regular.visible && explosive.visible && memcmp(regular.pixels,explosive.pixels,sizeof(regular.pixels)));
    picture("bumper-brown.ppm",&regular);picture("bumper-red.ppm",&explosive);
  }
  CHECK(FzeroTrafficValid(&traffic,world));traffic.ram[0xb0a]=128;CHECK(!FzeroTrafficValid(&traffic,world));
  CHECK(FzeroVersusRestart(s));FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
  FzeroVersus *fresh=session(tables);
  for(unsigned i=0;i<600;++i){CHECK(FzeroVersusTick(s,held));CHECK(FzeroVersusTick(fresh,held));}
  CHECK(FzeroVersusHash(s)==FzeroVersusHash(fresh));
  FzeroVersusDestroy(fresh);FzeroVersusDestroy(s);free(saved);
  printf("PASS native traffic: %u bumpers, both contacts/views, all sprite bands/angles, save/replay, atomic invalid rejection, restart, human-only results (%u moving samples)\n",maximum,moved);
  return 0;
}
