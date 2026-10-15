#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Portable actor adapters. Values retain the cartridge's integer units and
 * wrap points. Positions have a 16-bit fractional word; angles wrap at $C000.
 * No guest pointers, camera state, sprite allocation or host clock lives here.
 * ROM tables are supplied by a verified stock-cartridge reader. */
typedef uint8_t (*FzeroActorReadRom)(void *context, uint32_t address);
typedef struct FzeroActorTables {
  FzeroActorReadRom read;
  void *context;
} FzeroActorTables;

typedef struct FzeroActor {
  uint16_t speed, speed_fraction;
  uint16_t x, x_fraction, y, y_fraction;
  uint16_t vx, vx_fraction, vy, vy_fraction;
  uint16_t height, height_velocity;
  uint16_t facing, momentum, turning, spin, no_control;
  uint16_t health;
  uint16_t held, pressed, released, turn_stage, turn_acceleration;
  uint16_t boundary_momentum;
  uint8_t machine, action, state, surface, ground_flags, surface_flags;
  uint8_t checkpoint_angle, slide, instant_grip, jump_timer;
  uint8_t acceleration_index, turning_index;
  uint8_t exhaust, extra_height_index, extra_height, animation;
  uint8_t sound_flags;
  uint8_t steering_history, boosts, boost_timer, boost_award;
  uint8_t surface_id, ground_timer, surface_extra;
  uint8_t race_state, death_animation, lap, death_lap, landing_sound;
  uint8_t damage_cooldown, track_flash, electric_flash, effect_timer;
  uint8_t sound_flags2, sound_port3, screen_flags;
  uint8_t recovery_active, boost_beep_guard, boost_beep;
  uint8_t black_flash;
  uint8_t animation_scale, animation_transition, countdown_acceleration;
  uint16_t recovery_x, recovery_y;
  uint8_t recovery_angle, pit_phase, pit_departure, beam_top, beam_bottom;
} FzeroActor;

/* Scratch is explicitly owned by a single uncoupled movement evaluation.
 * Fields mirror the verified $00..$2F arithmetic dependencies, not a copied
 * guest RAM window. The hub initializes these before the actor walk. */
typedef struct FzeroActorScratch {
  uint16_t top_speed, turn, speed, facing, momentum, grip, gravity;
  uint16_t off_gas, over_speed, spin_decay;
  uint16_t vx, vx_fraction, vy, vy_fraction;
} FzeroActorScratch;

void FzeroActorScratchReset(FzeroActorScratch *scratch);
void FzeroActorSlide(FzeroActor *actor);
void FzeroActorTopSpeed(FzeroActor *actor, FzeroActorScratch *scratch,
                        FzeroActorTables tables);
void FzeroActorGrip(FzeroActor *actor, FzeroActorScratch *scratch,
                    FzeroActorTables tables);
void FzeroActorSpinState(FzeroActor *actor, FzeroActorScratch *scratch);
void FzeroActorSpeed(FzeroActor *actor, FzeroActorScratch *scratch,
                     FzeroActorTables tables, uint8_t race_state);
void FzeroActorApplySpin(FzeroActor *actor, FzeroActorScratch *scratch);
void FzeroActorTurn(FzeroActor *actor, FzeroActorScratch *scratch);
void FzeroActorApplyGrip(FzeroActor *actor, FzeroActorScratch *scratch);
void FzeroActorVelocity(FzeroActorScratch *scratch, FzeroActorTables tables);
void FzeroActorWind(const FzeroActor *actor, FzeroActorScratch *scratch,
                    FzeroActorTables tables, uint8_t race_state);
void FzeroActorStoreVelocity(FzeroActor *actor, FzeroActorScratch *scratch);
void FzeroActorMove(FzeroActor *actor, const FzeroActorScratch *scratch);
void FzeroActorDash(FzeroActor *actor);
/* SNES auto-joypad bit order (B=$8000, right=$0100, A=$0080, R=$0010).
 * Latch both actors before evaluating either one's movement. */
void FzeroActorLatch(FzeroActor *actor, uint16_t held);
void FzeroActorInputAction(FzeroActor *actor, FzeroActorTables tables);
void FzeroActorTurnStage(FzeroActor *actor);
void FzeroActorTurnPerformance(FzeroActor *actor, FzeroActorTables tables);
void FzeroActorAltitude(FzeroActor *actor, FzeroActorScratch *scratch,
                        FzeroActorTables tables, uint8_t tick);
void FzeroActorDamageSurface(FzeroActor *actor, FzeroActorTables tables);
void FzeroActorSurface(FzeroActor *actor);
uint8_t FzeroActorSampleSurface(const uint8_t world[0x20000], uint16_t x, uint16_t y);
typedef struct FzeroActorMineEvent { bool hit; uint16_t x, y; } FzeroActorMineEvent;
void FzeroActorMine(FzeroActor *actor, FzeroActorTables tables, FzeroActorMineEvent *event);
void FzeroActorPreRace(FzeroActor *actor, FzeroActorTables tables);
void FzeroActorLaunch(FzeroActor *actor);
void FzeroActorFailedStart(FzeroActor *actor);
void FzeroActorSpinTrigger(FzeroActor *actor);
void FzeroActorAnimation(FzeroActor *actor, FzeroActorTables tables);
void FzeroActorRecovery(FzeroActor *actor);
void FzeroActorPit(FzeroActor *actor, FzeroActorTables tables);
