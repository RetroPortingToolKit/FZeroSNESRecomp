#pragma once
#include "fzero_versus.h"
#include <stdio.h>

/* Owner-authorized lifecycle fixture, never a driving/physics qualification.
 * Teleport one racer at a time through authored gates and the finish strip.
 * Neutralize motion and replenish health so terrain/contacts cannot derail
 * this flow check. Do not write gates, laps, times, statuses or winner: the
 * real session must award all five laps and resolve both actual crossings.
 * Called only by explicitly enabled headless/online QA fixtures. */
static uint32_t FzeroLocalTeleportFixture(FzeroVersus *session) {
  if(!session)return 0;
  const FzeroVersusSnapshot *completed=FzeroVersusCompleted(session);
  if(completed->phase!=FZERO_VS_RACING || completed->pause)return 0;
  FzeroVersusSnapshot *race=(FzeroVersusSnapshot *)(void *)completed;
  const uint8_t *world=FzeroVersusWorld(session);
  unsigned player=race->racers[0].status==FZERO_VS_FINISHED?1:0;
  if(!(race->race_tick%1000)) {
    const FzeroVersusRacer *r=&race->racers[player];
    fprintf(stderr,"qa-teleport: course=%u player=%u lap=%u started=%u gates=%d cp=%u side=%02x next=%u,%u position=%u,%u count=%u crossing=%llu tick=%llu\n",
        race->config.course,player+1,r->completed_laps,r->started,r->gates,r->actor.checkpoint,r->actor.finish_side,
        r->actor.next_checkpoint_x,r->actor.next_checkpoint_y,r->actor.x,r->actor.y,world[0xad]+1,
        (unsigned long long)r->crossing_tick,(unsigned long long)race->race_tick);
  }
  for(unsigned i=0;i<2;++i)if(race->racers[i].status==FZERO_VS_ACTIVE) {
    FzeroActor *a=&race->racers[i].actor;
    a->speed=a->speed_fraction=a->vx=a->vx_fraction=a->vy=a->vy_fraction=0;
    a->health=0x800;a->height=0x400;a->height_velocity=a->spin=a->no_control=0;
    a->state=0;a->recovery_active=0;
    if(i!=player)continue;
    /* Presentation qualification holds each naturally awarded lap long
     * enough to observe the native notice's complete display/flash period. */
    if(getenv("FZERO_TEST_LAP_CUES") && race->racers[i].completed_laps &&
        race->race_tick-race->racers[i].lap_start<180)continue;
    a->facing=a->momentum=(uint16_t)a->next_checkpoint_angle<<8;
    /* Stay on the strip for both halves even if native backwards boundary
     * sampling repays one surplus gate during the teleport approach. */
    bool on_finish= a->finish_side && race->racers[i].gates>=(int)world[0xad]+1;
    if(!race->racers[i].started || race->racers[i].gates>=(int)world[0xad]+3 || on_finish) {
      a->x=(uint16_t)(world[0xae]*256+(race->race_tick&1?136:120));
      a->y=(uint16_t)(world[0xaf]*256+128);
    } else if(!(race->tick&1)) {
      a->x=a->next_checkpoint_x;a->y=a->next_checkpoint_y;
    } else {
      /* Exact previous-gate equality keeps the native backwards sample
       * stable even where wind moved the neutral fixture on the prior tick. */
      a->x=a->previous_checkpoint_x;a->y=a->previous_checkpoint_y;
    }
  }
  return 0;
}

/* Force a real void landing, leaving crash status/explosion/results to core. */
static void FzeroLocalCrashFixture(FzeroVersus *session) {
  const FzeroVersusSnapshot *race=FzeroVersusCompleted(session);
  if(race && race->phase==FZERO_VS_RACING && race->race_tick==30) {
    const uint8_t *world=FzeroVersusWorld(session);bool found=false;
    for(unsigned y=0;y<4096 && !found;y+=16)for(unsigned x=0;x<8192 && !found;x+=16) {
      unsigned surface=FzeroActorSampleSurface(world,(uint16_t)x,(uint16_t)y);
      bool lethal=world[FZERO_WORLD_RESOURCE]?
          (world[FZERO_WORLD_TERRAIN+0x300+surface]&0x80)!=0:surface>=0xd0;
      if(lethal) {
        FzeroActor *actor=&((FzeroVersusSnapshot *)(void *)race)->racers[0].actor;
        actor->x=(uint16_t)x;actor->y=(uint16_t)y;actor->health=0x800;
        actor->speed=actor->speed_fraction=actor->vx=actor->vx_fraction=0;
        actor->vy=actor->vy_fraction=actor->spin=actor->no_control=0;
        actor->recovery_active=0;
        actor->state=0x80;actor->height=0x210;actor->height_velocity=0xff00;
        fprintf(stderr,"online-crash-test: healthy landing on %s void surface %02x at %u,%u\n",
            world[FZERO_WORLD_RESOURCE]?"imported":"native",surface,x,y);
        found=true;
      }
    }
  }
}
