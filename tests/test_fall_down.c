#include "fzero_actor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t world[0x20000];
static uint8_t tables(void *context, uint32_t address) {
  (void)context; (void)address;
  return 7;
}
int main(void) {
  FzeroActorTables t = {tables, NULL, 0};
  const unsigned features[] = {FZERO_TERRAIN_FALL_DOWN, FZERO_TERRAIN_RAINBOW};
  for (unsigned k = 0; k < 2; ++k) {
    memset(world, 0, sizeof(world));
    world[FZERO_WORLD_FEATURES] = (uint8_t)features[k];
    for (unsigned flags = 0; flags < 256; ++flags) {
      FzeroActor a = {.ground_flags = (uint8_t)flags, .health = 0x300, .lap = 3};
      FzeroActorCourseDamage(&a, t, world);
      assert(a.race_state == (flags & 128 ? 0x40 : 0));
      assert(a.death_animation == (flags & 128 ? 6 : 0));
      assert(a.death_lap == (flags & 128 ? 3 : 0));
      if (!(flags & 12)) assert(a.health == 0x300);
    }
    for (unsigned backward = 0; backward < 2; ++backward) {
      FzeroActor a = {.x=100,.y=100,.momentum=0x3000,.ground_flags=0x20,
                     .next_checkpoint_x=120,.next_checkpoint_y=200,
                     .previous_checkpoint_x=120,.previous_checkpoint_y=200};
      FzeroActor baseline = a;
      world[FZERO_WORLD_FEATURES] = 0;
      if (backward) FzeroActorCheckpointBackward(&baseline, world, t);
      else FzeroActorCheckpointForward(&baseline, world, t);
      assert(baseline.momentum != 0x3000);
      world[FZERO_WORLD_FEATURES] = (uint8_t)features[k];
      if (backward) FzeroActorCheckpointBackward(&a, world, t);
      else FzeroActorCheckpointForward(&a, world, t);
      assert(a.momentum == 0x3000);
    }
  }
  world[FZERO_WORLD_FEATURES] = 0;
  FzeroActor stock = {.ground_flags=0x20,.health=0x300};
  FzeroActorCourseDamage(&stock,t,world);
  assert(stock.health < 0x300 && !stock.race_state);
  puts("fall-down: native collision flags, forward/backward wall bypass and stock isolation PASS");
  return 0;
}
