#pragma once
#include "fzero_actor.h"

#define FZERO_TRAFFIC_COUNT 4u
enum {FZERO_TRAFFIC_ART=0x8000,FZERO_TRAFFIC_PALETTE=0x7e00};
typedef struct FzeroTrafficPose { FzeroActor actor; uint8_t active; } FzeroTrafficPose;
typedef struct FzeroTraffic {
  /* Native byte-addressed state is already canonical little endian. CPU
   * registers and multiply/divide scratch are private to each bounded call. */
  uint8_t ram[0x2000], seed[0x2000];
  uint8_t enabled, present, contacts[2][FZERO_TRAFFIC_COUNT];
} FzeroTraffic;
bool FzeroTrafficInit(FzeroTraffic *traffic,uint8_t world[0x20000],
    const uint8_t native[0x20000],FzeroActorTables tables);
void FzeroTrafficReset(FzeroTraffic *traffic);
bool FzeroTrafficTick(FzeroTraffic *traffic,uint8_t world[0x20000],
    const FzeroActor humans[2],uint64_t tick,FzeroActorTables tables);
void FzeroTrafficDescribe(const FzeroTraffic *traffic,FzeroTrafficPose out[FZERO_TRAFFIC_COUNT]);
void FzeroTrafficContact(FzeroTraffic *traffic,unsigned player,FzeroActor *human,
    FzeroActorTables tables);
bool FzeroTrafficValid(const FzeroTraffic *traffic,const uint8_t world[0x20000]);
