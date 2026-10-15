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
  uint16_t next_checkpoint_x, next_checkpoint_y, previous_checkpoint_x, previous_checkpoint_y;
  uint8_t checkpoint, next_checkpoint_angle, previous_checkpoint_angle;
  uint8_t next_checkpoint_flags, previous_checkpoint_flags, status2, wall_angle, finish_side;
  uint16_t branch; /* low-byte flags; shortcut recovery clears the whole word */
  uint8_t jump_watch;
  uint16_t takeoff_x, takeoff_y, landing_x, landing_y;
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
typedef struct FzeroActorMineEvent { bool hit; uint16_t x, y; uint8_t surface_id; } FzeroActorMineEvent;
void FzeroActorMine(FzeroActor *actor, FzeroActorTables tables, FzeroActorMineEvent *event);
void FzeroActorCommitMine(uint8_t world[0x20000], const FzeroActorMineEvent *event);
void FzeroActorPreRace(FzeroActor *actor, FzeroActorTables tables);
void FzeroActorLaunch(FzeroActor *actor);
void FzeroActorFailedStart(FzeroActor *actor);
void FzeroActorSpinTrigger(FzeroActor *actor);
void FzeroActorAnimation(FzeroActor *actor, FzeroActorTables tables);
void FzeroActorRecovery(FzeroActor *actor);
void FzeroActorRecoveryTrigger(FzeroActor *actor, const uint8_t world[0x20000], FzeroActorTables tables);
void FzeroActorPit(FzeroActor *actor, FzeroActorTables tables);
/* Human checkpoint/boundary adapters. World is one immutable completed course
 * image shared by both actors, including the stock checkpoint/branch tables.
 * Returned deltas describe ordered gate changes; laps are owned by the race. */
void FzeroActorCheckpointInitialize(FzeroActor *actor, const uint8_t world[0x20000]);
int FzeroActorCheckpointForward(FzeroActor *actor, const uint8_t world[0x20000], FzeroActorTables tables);
int FzeroActorCheckpointBackward(FzeroActor *actor, const uint8_t world[0x20000], FzeroActorTables tables);
int FzeroActorProgressTick(FzeroActor *actor, const uint8_t world[0x20000], FzeroActorTables tables, uint8_t tick);
/* Finish strip detection ($9968..$99BF) without stock rank/lives/record writes.
 * -1 backwards, +1 forwards, zero otherwise. Laps belong to the session. */
int FzeroActorCrossFinish(FzeroActor *actor, const uint8_t world[0x20000]);
/* One uncoupled racing evaluation. The caller latches BOTH inputs first,
 * resolves shared contact afterwards, and advances shared events once. */
int FzeroActorTick(FzeroActor *actor, const uint8_t world[0x20000], FzeroActorTables tables,
                   uint8_t tick, FzeroActorMineEvent *mine);
typedef struct FzeroActorProjection {
  int32_t right, forward, x, y;
  bool projectable, stock_visible;
} FzeroActorProjection;
/* Stock world-to-body projection in the reference actor's physical frame.
 * No presentation camera/viewport/visibility state is an input. */
FzeroActorProjection FzeroActorProject(const FzeroActor *reference, const FzeroActor *actor, FzeroActorTables tables);
void FzeroActorContactSpeed(FzeroActor *follower, FzeroActor *leader);
void FzeroActorDamageContact(FzeroActor *actor, const FzeroActor *other, FzeroActorTables tables, bool direct);
void FzeroActorContactSpin(FzeroActor *a, FzeroActor *b, FzeroActorTables tables, uint8_t geometry);
void FzeroActorContactLateral(FzeroActor *a, FzeroActor *b, FzeroActorTables tables, uint8_t geometry, unsigned separation);
/* Pair contact belongs to the shared session and survives save/restore.
 * Call once in stable actor-ID order, after uncoupled movement. */
bool FzeroActorResolveContact(FzeroActor *a, FzeroActor *b, FzeroActorTables tables, uint8_t *contact);
