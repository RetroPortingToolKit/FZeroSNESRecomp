#include "fzero_actor.h"
#include "fzero_machine.h"
#include "fzero_course.h"
#include <string.h>

static uint8_t table8(FzeroActorTables t, uint32_t address) {
  return t.read(t.context, address);
}
static uint16_t table16(FzeroActorTables t, uint32_t address) {
  return (uint16_t)(table8(t, address) | (uint16_t)table8(t, address + 1) << 8);
}
static uint16_t angle_add(uint16_t angle, uint16_t amount) {
  unsigned result = (unsigned)angle + amount;
  if (result >= 0xc000) result -= 0xc000;
  return (uint16_t)result;
}
static uint16_t angle_sub(uint16_t angle, uint16_t amount) {
  return (uint16_t)(angle >= amount ? angle - amount : angle - amount + 0xc000);
}
static uint16_t subtract_floor(uint16_t value, uint16_t amount) {
  return value >= amount ? (uint16_t)(value - amount) : 0;
}
void FzeroActorSetCourseResources(uint8_t world[0x20000],const FzeroCourse *course) {
  if(!world || !course)return;
  world[FZERO_WORLD_RESOURCE]=1;
  memcpy(world+FZERO_WORLD_TERRAIN,course->terrain,sizeof(course->terrain));
  world[FZERO_WORLD_FEATURES]=course->required;
  world[FZERO_WORLD_CYCLES]=course->has_palette_cycles?course->palette_cycle_count:0xff;
  world[FZERO_WORLD_PALETTE]=course->has_palette_cycles;
  memcpy(world+FZERO_WORLD_CYCLE_OFFSETS,course->palette_cycles,sizeof(course->palette_cycles));
  memcpy(world+FZERO_WORLD_COURSE_HASH,course->hash,sizeof(course->hash));
  memcpy(world+FZERO_WORLD_SHORTCUTS,course->shortcuts,sizeof(course->shortcuts));
}
void FzeroActorSetNativePalette(uint8_t world[0x20000],unsigned type) {
  /* Deluxe C1CF reads course metadata $7F:4C03 rather than the venue.
   * Store its equivalent immutable cycle program without enabling imported
   * terrain mechanics for a native course. Offsets are relative to CGRAM16. */
  static const uint8_t offsets[]={0x40,0x60,0x80,0x70};
  world[FZERO_WORLD_PALETTE]=1;
  world[FZERO_WORLD_CYCLES]=(uint8_t)(type==0?0:type==2?1:type==3?4:3);
  memcpy(world+FZERO_WORLD_CYCLE_OFFSETS,offsets,sizeof(offsets));
}

void FzeroActorScratchReset(FzeroActorScratch *s) {
  memset(s, 0, sizeof(*s));
  /* $9147..$9159. Gravity is overwritten for the human actor by $91D9. */
  s->off_gas = 4; s->over_speed = 24; s->gravity = 24;
}

/* $9377..$938C, M=8/X=8. The subtract/reflect uses byte arithmetic. */
void FzeroActorSlide(FzeroActor *a) {
  unsigned facing = a->facing >> 8, momentum = a->momentum >> 8;
  unsigned difference = facing >= momentum ? facing - momentum : facing - momentum + 192;
  a->slide = (uint8_t)(difference < 96 ? difference : 192 - difference);
}

/* $91BC..$925F, human branch. DB=0/D=0; table indices are bytes. */
void FzeroActorTopSpeed(FzeroActor *a, FzeroActorScratch *s, FzeroActorTables t) {
  bool cgp=FzeroMachineGroup(a->machine)!=0;
  if ((a->no_control & 255) != 0) a->action &= 0x5c;
  s->off_gas = (s->off_gas & 0xff00) | table8(t, 0xfaa9 + FzeroMachineSlot(a->machine));
  s->over_speed = (s->over_speed & 0xff00) | table8(t, 0xfaad + FzeroMachineSlot(a->machine));
  s->gravity = (s->gravity & 0xff00) | table8(t, 0x9260 + ((a->action & 12) >> 2));
  if (a->action & 0x40) s->top_speed = 0x0e00;
  else if (a->state & 0x80) s->top_speed = cgp?0x0a00:0x0900;
  else if (a->surface & 0x80) s->top_speed = (a->state & 8) ? 0x0600 : cgp?0x0400:0x0300;
  else if (a->state & 0x40) s->top_speed = cgp?0x0980:0x0780;
  else if (a->state & 8) s->top_speed = cgp?0x0a00:0x0900;
  else {
    unsigned index = FzeroMachineSlot(a->machine) * 2;
    s->top_speed = table16(t, 0xfa91 + index);
    if (a->health < 0x200) {
      unsigned loss = ((0x200 - a->health) >> 3) * (table16(t, 0xfad5 + index) & 255);
      s->top_speed = (uint16_t)(s->top_speed - loss);
    }
  }
}

/* $9264..$930C. Sound flags are actor output events, never APU writes. */
void FzeroActorGrip(FzeroActor *a, FzeroActorScratch *s, FzeroActorTables t) {
  unsigned tolerance = table8(t, 0xfa81 + FzeroMachineSlot(a->machine));
  if (a->slide) {
    if ((a->speed >> 8) >= 5) a->sound_flags |= a->slide < tolerance ? 0x20 : 0x40;
    if (a->slide >= 0x28) {
      s->grip = (uint16_t)table8(t, 0xfa85 + FzeroMachineSlot(a->machine)) << 8;
      return;
    }
  } else a->instant_grip = 0;
  if (a->slide < tolerance && !(a->action & 0x80)) s->grip = 0x180;
  else if (a->instant_grip & 1) s->grip = (uint16_t)table8(t, 0xfa85 + FzeroMachineSlot(a->machine)) << 8;
  else if (a->state & 0x80) s->grip = 0xa0;
  else if ((a->surface_flags & 0x80) && a->slide >= 8) s->grip = 0x20;
  else if (a->speed < table16(t, 0xfa99 + FzeroMachineSlot(a->machine) * 2)) s->grip = 0x280;
  else s->grip = (uint16_t)table8(t, (a->slide < tolerance ? 0xfa89 : 0xfa8d) + FzeroMachineSlot(a->machine)) * 8;
}

/* $930D..$9376. The stock high-byte spin test is deliberately preserved:
 * a negative spin uses sign/magnitude ($8000 | magnitude), not int16. */
void FzeroActorSpinState(FzeroActor *a, FzeroActorScratch *s) {
  if (a->state & 0x40) {
    if (!(a->speed >> 8) || !a->spin) a->state &= 0xbf;
    else if (!(a->spin >> 8) && !(a->spin & 0x80)) {
      s->spin_decay &= 255; s->grip = 0; s->spin_decay = 2; return;
    } else {
      s->spin_decay &= 255; s->grip = 0;
      a->sound_flags |= 0x80;
      s->spin_decay = 2;
      if ((a->spin & 0x8000) ? (a->action & 1) : (a->action & 2)) s->spin_decay = 8;
      a->action &= 0x7c;
      return;
    }
  }
  s->spin_decay = 0x20;
}

/* $9470..$9502, human branch. Return a signed 16.8 increment in 24 bits. The stock
 * acceleration table is byte indexed after ASL/XBA and an ADC retaining
 * ASL's carry. Supported stock speeds keep that carry clear. */
static uint32_t acceleration(FzeroActor *a, FzeroActorTables t) {
  bool jump_initial = false;
  if (a->state & 0x80) {
    if (a->jump_timer < 4) { ++a->jump_timer; return 0; }
    if (a->jump_timer < 20) { ++a->jump_timer; jump_initial = true; }
  } else {
    /* M16 LDA $15 spans gravity's high byte and the action copy at $16. */
    if ((a->action & 0x40) || (a->state & 8)) return FzeroMachineGroup(a->machine)?0x9c40:0x10000;
    if ((a->action & 3) && a->speed >= 0x600 && a->slide >= 5) return 0xffff00;
  }
  unsigned index = jump_initial ? 0 : ((a->speed >> 7) & 255);
  unsigned carry = !jump_initial && (a->speed & 0x8000) ? 1 : 0;
  unsigned value = FzeroMachineGroup(a->machine)?
      table8(t,0x02ca42+FzeroMachineSlot(a->machine)*29+(index<29?index:28)):
      table8(t, 0x02ca37 + ((index + a->acceleration_index + carry) & 255));
  bool backward = table8(t, 0x08ed20 + a->slide) != 0;
  if (backward) value = 0x10;
  unsigned result = value * table8(t, 0x08ec30 + a->slide) / 2;
  /* $94EE decrements Y even when negating zero; preserve that stock edge. */
  return backward ? (0xff0000u | ((0u - result) & 65535)) : result;
}

/* $938D..$946F. The fractional buffer is a byte at $D60; $D61 is
 * untouched except when the stock STZ.W clears it on negative speed. */
void FzeroActorSpeed(FzeroActor *a, FzeroActorScratch *s, FzeroActorTables t, uint8_t race_state) {
  bool force_no_turn = false;
  bool checks_top = (a->ground_flags & 0xe0) || (a->action & 0xc0) || !(a->action & 0x30);
  if (checks_top && s->top_speed < a->speed) a->speed = subtract_floor(a->speed, s->over_speed);
  else if (a->ground_flags & 0xe0) { /* Wall/background: no acceleration. */ }
  else if (a->action & 0xc0) {
    uint32_t delta = acceleration(a, t);
    unsigned fraction = (a->speed_fraction & 255) + (delta & 255);
    a->speed_fraction = (a->speed_fraction & 0xff00) | (fraction & 255);
    unsigned speed = (unsigned)a->speed + (delta >> 8) + (fraction >> 8);
    a->speed = (uint16_t)speed;
    if (a->speed & 0x8000) { a->momentum = a->facing; a->speed_fraction = 0; a->speed = 0; }
  } else if (a->action & 0x20) {
    /* M16 ASL/XBA, X8 TAX: the table receives just the quotient byte. */
    a->speed = subtract_floor(a->speed, table8(t, (FzeroMachineGroup(a->machine)?0x02cab6:0x02ca96) + ((a->speed >> 7) & 255)));
  } else if (a->action & 0x10) {
    a->speed = subtract_floor(a->speed, 0x20);
    force_no_turn = race_state != 0;
  } else a->speed = subtract_floor(a->speed, s->off_gas);
  s->speed = a->speed;
  s->turn = 0;
  if (a->speed && !force_no_turn) {
    s->turn = a->turning;
    if (!(a->state & 0xc0) && (a->surface & 0x80)) s->turn >>= 1;
  }
}

void FzeroActorApplySpin(FzeroActor *a, FzeroActorScratch *s) {
  if (!a->spin) return;
  unsigned magnitude = a->spin & 0x7fff;
  a->facing = a->spin & 0x8000 ? angle_sub(a->facing, (uint16_t)magnitude) : angle_add(a->facing, (uint16_t)magnitude);
  bool negative = (a->spin & 0x8000) != 0;
  magnitude = subtract_floor((uint16_t)magnitude, s->spin_decay);
  /* Equal subtraction preserves the negative sign bit, as SBC carries. */
  if (negative && (a->spin & 0x7fff) >= s->spin_decay) magnitude |= 0x8000;
  a->spin = (uint16_t)magnitude;
}
void FzeroActorTurn(FzeroActor *a, FzeroActorScratch *s) {
  if (a->action & 1) a->facing = angle_add(a->facing, s->turn);
  else if (a->action & 2) a->facing = angle_sub(a->facing, s->turn);
  s->facing = a->facing;
}
static bool facing_left(const FzeroActorScratch *s) {
  return angle_sub(s->facing, s->momentum) >= 0x6000;
}
void FzeroActorApplyGrip(FzeroActor *a, FzeroActorScratch *s) {
  s->momentum = a->momentum;
  if (!s->speed) s->momentum = s->facing;
  else if (!(a->action & 0xc3) || a->no_control) return;
  else {
    bool left = facing_left(s);
    s->momentum = left ? angle_sub(s->momentum, s->grip) : angle_add(s->momentum, s->grip);
    if (facing_left(s) != left) s->momentum = s->facing;
  }
  a->momentum = s->momentum;
}

/* $95E7..$9671, stock hardware multiplies. Each result is a signed 16.16
 * word pair. The cartridge uses only the momentum angle's high byte. */
void FzeroActorVelocity(FzeroActorScratch *s, FzeroActorTables t) {
  unsigned index = s->momentum >> 8;
  uint32_t x = (uint32_t)table8(t, 0x08ec00 + index) * s->speed * 2;
  uint32_t y = (uint32_t)table8(t, 0x08ec30 + index) * s->speed * 2;
  if (table8(t, 0x08ecf0 + index)) x = 0u - x;
  if (!table8(t, 0x08ed20 + index)) y = 0u - y;
  s->vx = (uint16_t)(x >> 16); s->vx_fraction = (uint16_t)x;
  s->vy = (uint16_t)(y >> 16); s->vy_fraction = (uint16_t)y;
}
void FzeroActorWind(const FzeroActor *a, FzeroActorScratch *s, FzeroActorTables t, uint8_t race_state) {
  if (race_state || a->ground_flags || (a->state & 0x20) || !(a->surface_flags & 0x60)) return;
  unsigned angle = a->checkpoint_angle;
  if (a->surface_flags & 0x40) angle = (angle + 0x34) % 192;
  else angle = (angle + 192 - 0x2c) % 192;
  unsigned index = (angle & 0xf8) >> 2;
  uint16_t raw_x = table16(t, 0x09ef92 + index), raw_y = table16(t, 0x09efc2 + index);
  /* Sign extension via unsigned arithmetic avoids negative left-shift UB. */
  uint32_t x_push = ((raw_x & 0x8000) ? 0xffff0000u : 0u) | raw_x;
  uint32_t y_push = ((raw_y & 0x8000) ? 0xffff0000u : 0u) | raw_y;
  uint32_t x = ((uint32_t)s->vx << 16) | s->vx_fraction;
  uint32_t y = ((uint32_t)s->vy << 16) | s->vy_fraction;
  /* $96D5's first ADC inherits the final Y ROL carry. For both signs,
   * that carry is the sign bit of the original Y word. */
  x += x_push * 8u + ((raw_y & 0x8000) != 0);
  y += y_push * 8u;
  s->vx = (uint16_t)(x >> 16); s->vx_fraction = (uint16_t)x;
  s->vy = (uint16_t)(y >> 16); s->vy_fraction = (uint16_t)y;
}
void FzeroActorStoreVelocity(FzeroActor *a, FzeroActorScratch *s) {
  if (!FzeroMachineGroup(a->machine) && !(a->state & 0x28)) {
    unsigned limit=9;
    if (!(s->vx & 0x8000) && s->vx >= limit) { s->vx = (uint16_t)limit; s->vx_fraction = 0; }
    else if ((s->vx & 0x8000) && s->vx < 65536-limit) { s->vx = (uint16_t)(65536-limit); s->vx_fraction = 0; }
    if (!(s->vy & 0x8000) && s->vy >= limit) { s->vy = (uint16_t)limit; s->vy_fraction = 0; }
    else if ((s->vy & 0x8000) && s->vy < 65536-limit) { s->vy = (uint16_t)(65536-limit); s->vy_fraction = 0; }
  }
  a->vx = s->vx; a->vx_fraction = s->vx_fraction;
  a->vy = s->vy; a->vy_fraction = s->vy_fraction;
}
void FzeroActorMove(FzeroActor *a, const FzeroActorScratch *s) {
  unsigned x = (unsigned)a->x_fraction + s->vx_fraction;
  unsigned y = (unsigned)a->y_fraction + s->vy_fraction;
  a->x_fraction = (uint16_t)x; a->y_fraction = (uint16_t)y;
  a->x = (uint16_t)((a->x + s->vx + (x >> 16)) & 0x1fff);
  a->y = (uint16_t)((a->y + s->vy + (y >> 16)) & 0x0fff);
}
void FzeroActorDash(FzeroActor *a) {
  if (!(a->state & 0x10)) {
    if (a->surface & 0x10) {
      unsigned facing = a->facing >> 8;
      unsigned difference = (facing + 192 - a->checkpoint_angle) % 192;
      if (difference < 0x18 || difference >= 0xa9) {
        a->momentum = (uint16_t)a->checkpoint_angle << 8;
        a->state |= 0x30;
      }
    }
    if (!(a->state & 0x10)) {
      if (!(a->state & 0x20)) return;
      a->sound_flags |= 0x80;
      if ((a->speed >> 8) < 9) a->state &= 0xdf;
      else a->action &= 0x3f;
      return;
    }
  }
  a->sound_flags |= 0x80;
  if ((a->speed >> 8) < 13) a->action = (a->action & 15) | 0xc0;
  else {
    a->state &= 0xef; a->speed = 0x0e00; a->speed_fraction &= 0xff00;
    a->action &= 0x3f;
  }
}

void FzeroActorLatch(FzeroActor *a, uint16_t held) {
  a->pressed = held & (uint16_t)~a->held;
  a->released = a->held & (uint16_t)~held;
  a->held = held;
}

/* CGP/{1,2,3}/CGP_Boost.asm, independent energy boost per human.
 * Native autogas's temporary action flag is overwritten by B7CB before
 * physics; retain its actual refill/drain behavior, not an invented assist. */
void FzeroActorEnergyBoost(FzeroActor *a) {
  unsigned group=FzeroMachineGroup(a->machine);if(!group)return;
  if(a->lap>1 && (group==2 || !(a->state&8))) {
    if(!a->health || (a->health&0x8000))a->boosts=0;
    else if(a->boosts!=3 && !a->boost_award)a->boost_award=1;
  }
  if((a->state&0x38)!=8 || a->lap>5 || a->race_state || (a->recovery_active&128) || (a->boost_timer&7))return;
  static const uint8_t drain[3][4]={{23,21,19,23},{22,21,23,20},{22,17,22,21}};
  unsigned amount=drain[group-1][FzeroMachineSlot(a->machine)];
  unsigned old=a->health;
  if(old<amount || (old&0x8000)) {a->health=0;a->boost_timer=1;}
  else a->health=(uint16_t)(old-amount);
}

/* $B7CB..$B880. OAM icon writes are presentation outputs, deliberately not
 * part of actor state; boost count/timer/award contain their source state. */
void FzeroActorInputAction(FzeroActor *a, FzeroActorTables t) {
  a->action = 0;
  if (a->state & 8) {
    --a->boost_timer;
    if (!a->boost_timer) a->state &= 0xf7;
    else a->sound_flags |= 0x40;
  } else {
    if (a->boost_award) {
      --a->boost_award;
      if (a->boosts != 3) ++a->boosts;
    }
    if ((a->pressed & 0x80) && a->boosts) {
      a->state |= 8;
      a->boost_timer = FzeroMachineGroup(a->machine)?150:0;
      --a->boosts;
    } else if ((a->pressed & 0x80) && !a->boost_beep_guard) a->boost_beep = 0x10;
  }
  if (a->state & 0x80) a->action |= (a->held >> 8) & 12;
  if (((a->held & 255) | (a->held >> 8)) & 0x40) {
    a->action |= 0x20; a->exhaust = 0;
  } else if (a->held & 0x8000) {
    a->action |= 0x80;
    if (!a->exhaust) {
      a->exhaust = 0x80;
      if (!(a->state & 0x80) && a->speed <= table16(t, 0xfaa1 + FzeroMachineSlot(a->machine) * 2)) a->instant_grip = 1;
    }
  } else a->exhaust = 0;
}

/* $B94E..$BA12, branch entries decoded separately at M8/X8. Stage and
 * acceleration are fractional word pairs ($D5/$D6 and $D7/$D8). */
void FzeroActorTurnStage(FzeroActor *a) {
  unsigned direction = (a->held >> 8) & 3;
  if ((a->pressed >> 8) & 3) {
    a->turn_acceleration = 0x100;
    if (a->steering_history & 0x80) {
      unsigned high = a->turn_stage >> 8;
      if (!(a->steering_history & 1) && (a->pressed & 0x200) && (!high || high >= 0xf3)) {
        a->turn_stage = 0xf200; direction = 2;
      } else if ((a->steering_history & 1) && (a->pressed & 0x100) && high < 0x0e) {
        a->turn_stage = 0x0e00; direction = 1;
      }
    }
  }
  if (!direction) {
    a->turn_acceleration = 0x100;
    if (a->turn_stage & 0x8000) {
      a->turn_stage = (uint16_t)(a->turn_stage + 0x100);
      if (!(a->turn_stage & 0x8000)) a->turn_stage = 0;
    } else {
      a->turn_stage = subtract_floor(a->turn_stage, 0x100);
    }
    return;
  }
  if (direction & 1) {
    unsigned limit = a->held & 0x10 ? 0x1000 : 0x0e00;
    if (!(a->turn_stage & 0x8000)) {
      if (a->turn_stage == limit) return;
      if (a->turn_stage > limit) {
        a->turn_stage = subtract_floor(a->turn_stage, 0x100); return;
      }
    }
    a->turn_stage = (uint16_t)(a->turn_stage + a->turn_acceleration);
    if (!(a->turn_stage & 0x8000) && a->turn_stage >= limit) a->turn_stage = (uint16_t)limit;
  } else {
    unsigned limit = a->held & 0x20 ? 0xf000 : 0xf200;
    if (a->turn_stage & 0x8000) {
      if (a->turn_stage == limit) return;
      if (a->turn_stage < limit) {
        a->turn_stage = (uint16_t)(a->turn_stage + 0x100);
        if (!(a->turn_stage & 0x8000)) a->turn_stage = 0;
        return;
      }
    }
    a->turn_stage = (uint16_t)(a->turn_stage - a->turn_acceleration);
    if ((a->turn_stage & 0x8000) && a->turn_stage < limit) a->turn_stage = (uint16_t)limit;
  }
  a->turn_acceleration = (uint16_t)(a->turn_acceleration + 0x1e);
}

static void turn_performance(FzeroActor *a,FzeroActorTables t,bool magnet) {
  unsigned high = a->turn_stage >> 8;
  unsigned index = (a->speed >> 7) & 255;
  if (high) {
    bool left = (high & 0x80) != 0;
    unsigned magnitude = left ? ((0u - high) & 255) : high;
    unsigned factor = magnet?0x38:FzeroMachineGroup(a->machine)?
        table8(t,0x02c9ab+FzeroMachineSlot(a->machine)*30+(index<30?index:29)):index < 0x12 ? table8(t, 0x02c9ab + ((index + a->turning_index) & 255))
                                  : table8(t, 0x02c9f7 + index);
    a->turning = (uint16_t)(magnitude * factor / 2);
    a->action |= left ? 2 : 1;
    return;
  }
  a->turning = 0;
  if (a->action & 0x10) return;
  unsigned shoulder = a->held & 0x30;
  if (!shoulder || shoulder == 0x30) return;
  unsigned difference = ((a->momentum >> 8) + 192 - (a->facing >> 8)) % 192;
  unsigned amount = (magnet?0x50:table8(t, (FzeroMachineGroup(a->machine)?0x02ca23:0x02ca18) + index)) * 16;
  if (shoulder & 0x10) {
    if (difference >= 96 || difference < 6) {
      a->momentum = angle_add(a->momentum, (uint16_t)amount); a->instant_grip = 2;
    }
    a->animation = 0x21;
  } else {
    if (difference < 96 || difference >= 0xba) {
      a->momentum = angle_sub(a->momentum, (uint16_t)amount); a->instant_grip = 2;
    }
    a->animation = 0x20;
  }
}
void FzeroActorTurnPerformance(FzeroActor *a,FzeroActorTables t) {turn_performance(a,t,false);}
void FzeroActorCourseTurnPerformance(FzeroActor *a,FzeroActorTables t,const uint8_t world[0x20000]) {
  bool magnet=(world[FZERO_WORLD_FEATURES]&FZERO_TERRAIN_GRIP_MAGNETS) &&
      !(a->state&128) && (a->surface&8);
  turn_performance(a,t,magnet);
}

static void actor_damage(FzeroActor *a, unsigned damage) {
  if (a->health & 0x8000) {
    a->race_state |= 0x40; a->death_animation = 6; a->death_lap = a->lap;
  } else {
    unsigned old = a->health;
    a->health = (uint16_t)(a->health - damage);
    if (old >= 0x200 && a->health < 0x200) a->screen_flags |= 0x40;
  }
}

void FzeroActorDamageContact(FzeroActor *a,const FzeroActor *b,FzeroActorTables t,bool direct) {
  unsigned damage;
  a->sound_flags2|=0x80;
  if (direct) {
    /* $87BC deliberately reads the LOW momentum bytes at M8, with carry
     * set from SEP #$21. Preserve those verified cartridge semantics. */
    unsigned own=(a->speed>>5)&255,other=(b->speed>>4)&255;
    int difference=(int)(b->momentum&255)-(int)(a->momentum&255);
    unsigned index=(unsigned)(difference<0?difference+192:difference)&255;
    unsigned projected=(other*table8(t,0x08ec30+index))>>8;
    unsigned relative=table8(t,0x08ed20+index)?((own+projected)&255)
        :(own>projected?own-projected:projected-own);
    if (relative>=127) relative=127;
    damage=(relative*table8(t,0xfab1+FzeroMachineSlot(a->machine)))>>8;
    a->sound_port3|=3;
  } else { damage=table8(t,0xfab5+FzeroMachineSlot(a->machine));a->sound_port3|=1; }
  actor_damage(a,damage);
}

void FzeroActorDamageSurface(FzeroActor *a, FzeroActorTables t) {
  unsigned flags = a->ground_flags;
  if (!(a->state & 0x80)) flags |= a->surface & 12;
  if (a->race_state || !(flags & 0xfc)) return;
  if (!(flags & 0x1c)) {
    if (!a->track_flash) a->track_flash = 9;
    a->no_control = table8(t, 0xfaed + FzeroMachineSlot(a->machine));
    a->sound_flags2 |= 0x40;
    if (!a->damage_cooldown) {
      a->damage_cooldown = 9;
      actor_damage(a, table8(t, 0xfabd + FzeroMachineSlot(a->machine)));
    }
  } else {
    if ((flags & 0x10) && !a->electric_flash) a->electric_flash = 8;
    if (!a->damage_cooldown) {
      a->damage_cooldown = 2;
      actor_damage(a, table8(t, 0xfab9 + FzeroMachineSlot(a->machine)));
    }
  }
  if (a->speed >= table16(t, 0xfacd + FzeroMachineSlot(a->machine) * 2)) a->speed -= 16;
  if (!(a->animation & 0xe0)) a->animation = 0x89;
  if (!(a->health & 0x8000) && !(a->effect_timer & 0x80)) a->effect_timer = 0x8b;
}
void FzeroActorCourseDamage(FzeroActor *a,FzeroActorTables t,const uint8_t world[0x20000]) {
  unsigned features=world[FZERO_WORLD_FEATURES];uint8_t surface=a->surface;
  if(features&FZERO_TERRAIN_GRIP_MAGNETS)a->surface&=~8u;
  if(features&FZERO_TERRAIN_RAINBOW) {
    unsigned flags=a->ground_flags|((a->state&0x80)?0:a->surface&12);
    flags&=0x8c;
    if(a->race_state || !flags) {a->surface=surface;return;}
    if(flags&128) {
      a->race_state|=0x40;a->death_animation=6;a->death_lap=a->lap;a->surface=surface;return;
    }
  }
  FzeroActorDamageSurface(a,t);a->surface=surface;
}

static void start_jump(FzeroActor *a, unsigned base_height) {
  a->state = (a->state | 0x80) & 0xfe;
  unsigned velocity = a->speed >> 1;
  a->height_velocity = velocity < 0x100 ? 0x100 : velocity >= 0x481 ? 0x480 : (uint16_t)velocity;
  if (a->speed >= 0x200) a->speed -= 0x80;
  a->jump_timer = 0;
  a->height = (uint16_t)(base_height << 8);
}

/* $9B4F..$9D41. Versus has no rank-out; the jump rank guard always admits
 * each human. Return presentation events as fields rather than touching OAM,
 * the camera, music, lives or the shared guest scene. */
static void altitude(FzeroActor *a, FzeroActorScratch *s, FzeroActorTables t,uint8_t tick,bool custom,unsigned features) {
  if (a->recovery_active) return;
  bool magnetic=(features&FZERO_TERRAIN_GRIP_MAGNETS) && (a->surface&8);
  if (a->exhaust && !magnetic) {
    if (a->exhaust == 0x80) a->extra_height_index = 0;
    unsigned target = table8(t, 0x9b7e + a->extra_height_index);
    if (target < a->extra_height) --a->extra_height;
    else a->extra_height = (uint8_t)target;
    if (a->extra_height_index != 6) ++a->extra_height_index;
  } else if (a->extra_height) --a->extra_height;
  unsigned base_height = (a->extra_height + 4) & 255;
  uint16_t ground_height = (uint16_t)(((base_height - 2) & 255) << 8);
  if (!(a->state & 0x80)) {
    if (a->surface & 0x40) start_jump(a,base_height);
    else {
      a->height = (a->height & 255) | (uint16_t)base_height << 8;
      if ((a->surface & 0x80) && (a->speed >> 8)) a->height = (uint16_t)(a->height + ((tick & 2) ? 0x100 : 0));
      else if (a->exhaust && !(a->exhaust & 0x80) && !(tick & 1)) a->height -= 0x100;
      return;
    }
  }
  a->height = (uint16_t)(a->height + a->height_velocity);
  bool landed=features&FZERO_TERRAIN_UP_MAGNETS?(a->height<ground_height || a->height>=0xc000):
      (a->height & 0x8000) || a->height<ground_height;
  if (landed && (a->height_velocity & 0x8000)) {
    a->height = ground_height;
    bool off_track = custom?((a->ground_flags&0x80) || (!(a->state&1) && (a->ground_flags&0x20))):a->surface_id >= 0xd0 ||
        (!(a->state & 1) && (a->surface_id == 0x69 || a->surface_id == 0xa5));
    if (off_track) {
      a->race_state |= 0x40; a->death_animation = 6; a->death_lap = a->lap;
      a->state &= 0x7f;
      if (!(a->state & 1)) a->landing_sound = 12;
    } else {
      unsigned impact = (0u - a->height_velocity) & 65535;
      if (impact >= 0x280) {
        a->height_velocity = (uint16_t)(impact >> 2);
        if (!(a->action & 4)) {
          if (a->surface & 0x40) { start_jump(a,base_height); return; }
          if (!(a->effect_timer & 0x80)) a->effect_timer = 0x45;
          if (a->speed >= 0x200) { a->speed -= 0x100; a->sound_port3 |= 8; }
          a->state |= 1;
          return;
        }
      }
      a->instant_grip = 1;
      a->state &= 0x7f;
      if (!(a->state & 1)) a->landing_sound = 12;
    }
    if (a->surface & 0x40) start_jump(a,base_height);
  } else {
    if((features&FZERO_TERRAIN_UP_MAGNETS) && (a->surface&8)) {
      bool up=a->surface_id==0xb6 || (a->surface_id>=0xcc && a->surface_id<0xd0);
      if(!up)a->height_velocity=(uint16_t)(a->height_velocity-(a->surface_id>=0xd0?0x60:0x61));
      else {
        static const unsigned gain[]={7,8,5},limit[]={0x190,0x200,0x120};
        unsigned tilt=(a->action&12)>>2;if(tilt>2)tilt=0;
        unsigned strength=a->height<=0x7000?(0x7000-a->height)>>10:0;
        unsigned old=a->height_velocity,fall=(old-s->gravity)&65535;
        unsigned result=(fall+strength*gain[tilt]+(old>=s->gravity))&65535;
        a->height_velocity=(uint16_t)((((result-limit[tilt])&0x8000)!=0)?result:limit[tilt]);
      }
    } else a->height_velocity = (uint16_t)(a->height_velocity - ((a->surface & 8) ? 0x60 : s->gravity) - 1);
    a->sound_flags |= (a->action & 4) ? 0x80 : 0x40;
  }
}
void FzeroActorAltitude(FzeroActor *a,FzeroActorScratch *s,FzeroActorTables t,uint8_t tick) {
  altitude(a,s,t,tick,false,0);
}
void FzeroActorCourseAltitude(FzeroActor *a,FzeroActorScratch *s,FzeroActorTables t,uint8_t tick,const uint8_t world[0x20000]) {
  altitude(a,s,t,tick,world[FZERO_WORLD_RESOURCE]!=0,world[FZERO_WORLD_FEATURES]);
}

/* $8E36..$8ECD and $03:9187..$920A, player branch. The boundary animation
 * timer is gameplay: it controls wall/void responses even without a sprite. */
void FzeroActorSurface(FzeroActor *a) {
  unsigned tile = a->surface_id;
  a->surface = a->surface_extra = a->surface_flags = 0;
  if (tile < 0x69) { a->ground_flags = 0x10; a->ground_timer = 0; }
  else if ((tile < 0x82) || tile == 0xa5) {
    if (tile == 0x69) a->surface_extra = 0x80;
    if (tile == 0xa5) a->surface_extra = 0x40;
    if (a->ground_timer & 0x80) { if (a->speed < 0x100) a->speed = 0x100; }
    else if (a->ground_timer == 0 || a->ground_timer == 2 || a->ground_timer == 3) {
      if (!a->ground_timer) { a->boundary_momentum = a->momentum; a->ground_flags = 0x20; a->ground_timer = 1; }
      else if (a->ground_timer == 2) { a->ground_flags = 0x40; a->ground_timer = 3; }
      if (a->speed < 0x80) a->speed = 0x80;
    } else ++a->ground_timer;
  } else if (tile >= 0xd0) {
    a->ground_flags = 0x80; a->ground_timer = 0xff;
    if (a->speed < 0x100) a->speed = 0x100;
  } else {
    a->ground_flags = a->ground_timer = 0;
    if (tile >= 0xc3) a->surface = tile < 0xc8 ? 0x10 : tile < 0xcc ? 0x20 : 0x80;
    else if (tile >= 0xc0) a->surface_extra = tile == 0xc0 ? 0x80 : tile == 0xc1 ? 0x40 : 0;
    else if (tile >= 0xba) a->surface = 0x40;
    else if (tile == 0xb9) a->surface_flags = 0x10;
    else if (tile >= 0xb0) a->surface_flags = 0x80;
    else if (tile >= 0xa7) a->surface = 0x80;
    else if (tile == 0xa6) a->surface = 8;
    else if (tile == 0x9e || tile == 0xa1 || tile == 0xa2) a->surface = 4;
    else if (tile == 0xa0) a->surface_flags = 0x40;
    else if (tile == 0x9c) a->surface_flags = 0x20;
  }
}
void FzeroActorCourseSurface(FzeroActor *a,const uint8_t world[0x20000]) {
  if(!world[FZERO_WORLD_RESOURCE]) {FzeroActorSurface(a);return;}
  const uint8_t *terrain=world+FZERO_WORLD_TERRAIN;unsigned id=a->surface_id;
  a->surface_extra=terrain[id];a->surface=terrain[0x100+id];a->surface_flags=terrain[0x200+id];
  unsigned flags=terrain[0x300+id];a->ground_flags=(uint8_t)flags;
  if(flags&128)a->ground_timer=255;
  else if(flags&64) {a->ground_timer=3;if(a->speed<0x80)a->speed=0x80;}
  else if(flags&32) {
    if(!(a->ground_timer&128)) {
      if(!a->ground_timer)a->ground_timer=1;
      else if(a->ground_timer==2) {a->ground_flags=64;a->ground_timer=3;}
      else if(a->ground_timer!=3) {++a->ground_timer;return;}
    }
    if(a->speed<0x80)a->speed=0x80;
  } else a->ground_timer=0;
  if((flags&128) || ((flags&32) && (a->ground_timer&128)))if(a->speed<0x100)a->speed=0x100;
}

/* $A1E5..$A23C. Full-course lookup; neither the streamed Mode 7 square nor
 * either camera participates. All three indirections wrap within bank $7F. */
static unsigned world_word(const uint8_t *bank, unsigned address) {
  return bank[address & 65535] | (unsigned)bank[(address + 1) & 65535] << 8;
}
bool FzeroActorCaptureWorld(uint8_t output[0x20000],const uint8_t native[0x20000]) {
  if(!output || !native || output==native)return false;
  memset(output,0,0x20000);
  memcpy(output+0xad,native+0xad,5); /* gate count, finish strip, grid pointer */
  output[0x1075]=native[0x1075]; /* authored shortcut-recovery selector */
  output[0x10d6]=native[0x10d6];output[0x10d8]=native[0x10d8];output[0x10da]=native[0x10da];
  memcpy(output+0x1200,native+0x1200,0x600); /* both checkpoint/branch tables */
  const uint8_t *source=native+0x10000;uint8_t *bank=output+0x10000;
  bool visited[256]={false};unsigned grid=world_word(native,0xb0);
  for(unsigned cell=0;cell<32*16;++cell) {
    unsigned address=(grid+cell)&65535,block=source[address];bank[address]=source[address];
    if(visited[block])continue;
    visited[block]=true;
    for(unsigned y=0;y<16;++y) {
      unsigned at=(0x5000+block*32+y*2)&65535,row=world_word(source,at);
      bank[at]=source[at];bank[(at+1)&65535]=source[(at+1)&65535];
      for(unsigned x=0;x<16;++x) {
        at=(row+x*2)&65535;unsigned group=world_word(source,at);
        bank[at]=source[at];bank[(at+1)&65535]=source[(at+1)&65535];
        for(unsigned tile=0;tile<4;++tile)bank[(group+tile)&65535]=source[(group+tile)&65535];
      }
    }
  }
  memcpy(bank+0x3ff0,source+0x3ff0,16);
  return true;
}
uint8_t FzeroActorSampleSurface(const uint8_t world[0x20000], uint16_t x, uint16_t y) {
  const uint8_t *bank = world + 0x10000;
  unsigned grid = world[0xb0] | (unsigned)world[0xb1] << 8;
  unsigned block = bank[(grid + ((y >> 8) & 15) * 32 + ((x >> 8) & 31)) & 65535];
  unsigned row = world_word(bank, 0x5000 + block * 32 + ((y >> 4) & 15) * 2);
  unsigned group = world_word(bank, row + ((x >> 4) & 15) * 2);
  return bank[(group + ((x >> 3) & 1) * 2 + ((y >> 3) & 1)) & 65535];
}

/* $9D9E..$9E62's human response. A racing human owns an active actor, and
 * mine eligibility is determined by its sampled world surface/altitude.
 * Retail's opponent sprite-Y eligibility is intentionally absent: a second
 * human must not lose mine interaction when outside the other camera. The
 * event is committed to the shared course only after both actors evaluate. */
void FzeroActorMine(FzeroActor *a, FzeroActorTables t, FzeroActorMineEvent *event) {
  *event = (FzeroActorMineEvent){0};
  if (!(a->surface & 0x20) || (a->state & 0x80)) return;
  *event = (FzeroActorMineEvent){true,a->x,a->y,a->surface_id};
  a->no_control = (a->no_control & 0xff00) | 0x20;
  a->black_flash = 0x85;
  actor_damage(a, table8(t, 0xfac1 + FzeroMachineSlot(a->machine)));
  a->state |= 0x81; a->height_velocity = 0x100;
  unsigned angle = ((a->momentum >> 8) + 192 - 24) % 192;
  unsigned index = ((angle / 12) & 0xfc) | ((a->surface_id - 0xc8) & 255);
  unsigned spin = table8(t, 0x9e63 + index);
  bool negative = (spin & 0x80) != 0;
  if (a->state & 0x40) --spin;
  a->spin = (uint16_t)((spin & 255) << 8);
  a->speed = 0x800;
  unsigned high = a->momentum >> 8;
  if (negative || !(a->state & 0x40)) {
    high = (high + 192 - 20) % 192; a->animation = 0x43;
  } else {
    high = (high + 20) % 192; a->animation = 0x47;
  }
  a->momentum = (a->momentum & 255) | (uint16_t)high << 8;
}

/* $A3F5..$A443's shared course mutation via $A23D. Each 32x32 mine
 * replaces four 16x16 cell pointers with the prepared crater tile groups.
 * Read both actors first; commit their events once in actor-ID order. */
void FzeroActorCommitMine(uint8_t world[0x20000],const FzeroActorMineEvent *event) {
  if (!event->hit) return;
  uint8_t *bank=world+0x10000;
  unsigned x=(event->x-((event->surface_id&1)?16:0))&8191;
  unsigned y=(event->y-((event->surface_id&2)?16:0))&4095;
  unsigned grid=world[0xb0]|(unsigned)world[0xb1]<<8;
  for (unsigned row=0;row<2;++row) {
    unsigned yy=(y+row*16)&4095;
    unsigned block=bank[(grid+(yy>>8)*32+(x>>8))&65535];
    unsigned pointer=world_word(bank,0x5000+block*32+((yy>>4)&15)*2);
    unsigned address=(pointer+((x>>4)&15)*2)&65535;
    for (unsigned column=0;column<2;++column) {
      unsigned value=0x3ff0+row*8+column*4;
      bank[(address+column*2)&65535]=(uint8_t)value;
      bank[(address+column*2+1)&65535]=(uint8_t)(value>>8);
    }
  }
}

/* $FA1C..$FA65 is called twice per native countdown tick. */
void FzeroActorPreRace(FzeroActor *a, FzeroActorTables t) {
  FzeroMachineReader reader;t=FzeroMachineTables(&reader,t,a->machine);
  if ((a->held & 0xc000) == 0x8000) {
    unsigned index = ((a->speed >> 7) & 255) + ((a->speed & 0x8000) ? 1 : 0);
    index = (index + table8(t,0xfa79+FzeroMachineSlot(a->machine))) & 255;
    unsigned speed = (a->speed + table8(t,(FzeroMachineGroup(a->machine)?0x02ca42:0x02ca37)+index)) & 65535;
    a->speed = speed < 0x7ff ? (uint16_t)speed : 0x7ff;
  } else {
    a->speed = (uint16_t)(a->speed - 40);
    if (a->speed & 0x8000) a->speed = 0;
  }
  unsigned rate = (a->speed >> 5) & 255;
  a->countdown_acceleration = rate ? (uint8_t)rate : 1;
}
void FzeroActorLaunch(FzeroActor *a) {
  unsigned high = a->speed >> 8;
  if (high < 2) a->speed = 0;
  else if (high == 7 && (a->speed & 255) == 255 && (a->held & 0x8000)) {
    a->state |= 2; a->speed = 0x700;
  } else a->speed = 0x200;
}
void FzeroActorFailedStart(FzeroActor *a) {
  if (!(a->state & 2)) return;
  if ((a->speed >> 8) >= 2) a->action = (a->action & 3) | 0x18;
  else a->state &= 0xfd;
}
void FzeroActorSpinTrigger(FzeroActor *a) {
  if ((a->state & 0x40) || (a->speed >> 8) < 8) return;
  unsigned flags = a->ground_flags & 0xe0;
  bool negative;
  if (flags) {
    if (!(flags & 0xc0)) return;
    /* CLC; SBC, deliberately without a circle wrap. */
    unsigned difference = ((unsigned)(a->facing >> 8) - a->checkpoint_angle - 1) & 255;
    negative = (difference >= 0x30 && difference < 0x60) || difference >= 0x90;
  } else {
    if (!(a->surface_flags & 0x80)) return;
    unsigned difference = ((a->facing >> 8) + 192 - (a->momentum >> 8)) % 192;
    if (difference < 0x60) {
      if (difference < 0x12) return;
      negative = false;
    } else {
      if (difference >= 0xae) return;
      negative = true;
    }
  }
  a->spin = negative ? 0x8140 : 0x0140;
  a->state |= 0x40;
}
/* $BA70..$BB11. DD affects snap steering on the next tick, so animation
 * identity is actor state even for a headless simulation. */
void FzeroActorAnimation(FzeroActor *a, FzeroActorTables t) {
  if (a->race_state) { a->animation_transition = 0; return; }
  if ((a->state & 0x81) == 0x80 && ((a->action & 4) || a->jump_timer < 16)) {
    a->animation_scale = 0x80; a->steering_history = 5; return;
  }
  if (a->animation & 0x80) {
    a->steering_history = table8(t,0xbb54+(a->animation&15));
    --a->animation;
    if (!(a->animation & 0x80)) a->animation = 0;
    a->animation_transition = 0;
  } else if (a->animation & 0x40) {
    if (!(a->no_control&255)) { a->animation = 0; return; }
    a->steering_history = table8(t,0xbb5e + (a->animation&15));
    if (a->animation&3) --a->animation;
    else a->animation |= 3;
  } else if (a->animation & 0x20) {
    a->steering_history = (a->animation&1)|0x80;
    a->animation = a->animation_transition = 0;
  } else {
    unsigned index = ((a->turn_stage >> 8) + 16) & 255;
    a->steering_history = table8(t,0xbb12+index);
    a->animation_scale = table8(t,0xbb33+index);
    if (a->steering_history == 0 || a->steering_history == 10) {
      if (!a->animation_transition) a->animation_transition = a->steering_history ? 0x81 : 1;
    } else a->animation_transition = 0;
  }
}

/* $C507..$C61B: exhaust startup/decay. E1 also controls hover and airborne
 * descent in $9B4F; omitting this apparently visual update changes handling.
 * Native OAM, palette and DMA writes are presentation outputs, not inputs. */
void FzeroActorExhaust(FzeroActor *a, FzeroActorTables t) {
  if (!a->exhaust) return;
  unsigned index;
  if (a->exhaust&0x80) {
    unsigned phase=a->exhaust&0x7f;
    if (!phase) { ++a->exhaust;index=2; }
    else if (phase==1) { ++a->exhaust;a->exhaust_timer=2;index=1; }
    else if (phase==2) {
      if (--a->exhaust_timer) return;
      ++a->exhaust;a->boost_beep_guard=1;a->exhaust_timer=2;index=0;
    } else {
      if (!--a->exhaust_timer) {
        a->exhaust=4;a->exhaust_timer=1;a->exhaust_frame=a->exhaust_color=0;
      }
      return;
    }
  } else {
    if (--a->exhaust_timer) return;
    --a->exhaust_frame;
    if (a->exhaust_frame&0x80) {
      a->exhaust_frame=3;
      if (a->exhaust!=1) --a->exhaust;
      a->exhaust_timer=a->exhaust;return;
    }
    a->exhaust_timer=a->exhaust;index=a->exhaust_frame;
    unsigned target=((a->speed>=0x7ff?255:a->speed>>3)*0x5e)>>8;
    a->exhaust_color=a->exhaust_color<target?a->exhaust_color+1u:target;
  }
  a->exhaust_offset=table8(t,0xc77c+index);
}
/* $BED7..$BF5E advances DD's extreme-turn spark phase in DF. The low-speed
 * and airborne resets also affect the following animation transition. */
void FzeroActorSpark(FzeroActor *a) {
  if (a->race_state) {
    if (!(a->race_state&0x10)) a->animation_transition=0;
    return;
  }
  if ((a->state&0x80) || a->speed<0x400) {a->animation_transition=0;return;}
  if (a->animation_transition) {
    unsigned phase=(a->animation_transition-1)&0x7f;
    unsigned next=(a->animation_transition+1)&255;
    unsigned doubled=(next<<1)&255;
    if (doubled==0x16) doubled=2;
    a->animation_transition=(uint8_t)((doubled>>1)|(next&0x80));
    if (phase&8) a->sound_flags2|=0x10;
  }
}

/* $BF5F/$BF83/$C0AC state writes for the active human. Palette and OAM
 * output is discarded; retaining countdowns prevents permanent hit flashes. */
void FzeroActorTransientEffects(FzeroActor *a) {
  if(a->electric_flash)--a->electric_flash;
  if(a->track_flash)--a->track_flash;
  if(!a->death_animation && a->effect_timer) {
    --a->effect_timer;
    unsigned phase=a->effect_timer&15;
    if(!phase)a->effect_timer=0;
    else if(a->effect_timer&0x80) {
      static const uint8_t palette_event[10]={2,1,0,2,0,1,0,2,0,1};
      if(a->effect_timer==0x8a)a->sound_flags2|=0x20;
      if(phase<=10 && palette_event[phase-1])a->body_flash=palette_event[phase-1]==1;
    } else if(a->effect_timer&0x40) {
      if(phase==4)a->body_flash=1;
      else if(phase==2)a->body_flash=0;
    }
  }
  if(a->black_flash) {
    bool negative=(a->black_flash&0x80)!=0;
    --a->black_flash;
    unsigned phase=a->black_flash&0x7f;
    if((negative && phase==4) || (!negative && phase==8))a->body_flash=1;
    else if((negative && !phase) || (!negative && phase==6))a->body_flash=0;
    if(negative && !phase)a->black_flash=0;
  }
}

/* $B711..$B7AA retains CMP's carry on the positive approach. The native
 * positive step is seven units, while the negative step is six. */
static uint16_t recovery_coordinate(uint16_t position, uint16_t target, unsigned half) {
  unsigned difference = ((unsigned)position - target) & 65535;
  if (position >= target) {
    if (difference >= half) return (uint16_t)(position + 6);
    uint16_t next = (uint16_t)(position - 6);
    return next >= target ? next : target;
  }
  if (difference < 65536 - half) return (uint16_t)(position - 6);
  uint16_t next = (uint16_t)(position + 7);
  return next < target ? next : target;
}
void FzeroActorRecovery(FzeroActor *a) {
  a->countdown_acceleration |= 0x40;
  bool aligned = false;
  if (!a->speed) {
    unsigned high = a->facing >> 8;
    unsigned difference = (high + 192 - a->recovery_angle) % 192;
    bool angle_aligned = difference == 0;
    if (difference) {
      high = difference >= 96 ? (high + 1) % 192 : (high + 191) % 192;
      a->facing = (a->facing & 255) | (uint16_t)high << 8;
    }
    bool x_aligned = a->x == a->recovery_x, y_aligned = a->y == a->recovery_y;
    if (!x_aligned) a->x = recovery_coordinate(a->x,a->recovery_x,0x1000);
    if (!y_aligned) a->y = recovery_coordinate(a->y,a->recovery_y,0x800);
    aligned = angle_aligned && x_aligned && y_aligned;
  }
  unsigned height = a->height >> 8;
  if (aligned) {
    if (height < 5) a->recovery_active = 0;
    else a->height -= 0x100;
  } else if (height < 0x20) a->height += 0x100;
}
/* $B61B..$B6CB. Sprite uploads are removed; their phase/beam inputs remain
 * explicit so presentation and subsequent health/control transitions agree. */
void FzeroActorPit(FzeroActor *a, FzeroActorTables t) {
  if (!a->race_state && (a->recovery_active || (a->surface_flags & 0x10))) {
    if (!a->pit_phase) { a->screen_flags |= 1; ++a->pit_phase; return; }
    a->pit_departure = 16;
    if (a->pit_phase < 0x22) {
      a->pit_pose=a->pit_phase;
      ++a->pit_phase;
      if (a->recovery_active && a->pit_phase != 0x22) a->pit_phase = 0x21;
    } else if (a->pit_phase == 0x22) {
      a->pit_recovery=a->recovery_active!=0;
      a->beam_top = 0; a->beam_bottom = 0x50; ++a->pit_phase;
    } else {
      a->beam_top = (uint8_t)(a->beam_top + 16);
      if (a->beam_top == 0xb0) a->beam_top = 0;
      a->beam_bottom = (uint8_t)(a->beam_bottom + 16);
      if (a->beam_bottom == 0xb0) a->beam_bottom = 0;
      if (a->recovery_active) FzeroActorRecovery(a);
      else {
        a->sound_port3 |= 0x40;
        if (a->health & 0x8000) a->health = 0;
        else {
          unsigned health = (a->health + table16(t,0xfac5+FzeroMachineSlot(a->machine)*2)) & 65535;
          a->health = health < 0x800 ? (uint16_t)health : 0x800;
        }
      }
    }
  } else if (a->pit_phase) {
    if (a->pit_phase == 0x23) {
      --a->pit_phase;
      if ((a->health >> 8) >= 2) a->screen_flags &= 0xbf;
    } else if (a->pit_departure) --a->pit_departure;
    else if (!--a->pit_phase) {a->screen_flags &= 0xfe;a->pit_pose=0;}
    else a->pit_pose=a->pit_phase;
  }
}
