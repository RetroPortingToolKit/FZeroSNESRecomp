#pragma once
#include "fzero_actor.h"
#include <stddef.h>

/* A single game-owned authoritative race. Presentation and host device state
 * are deliberately absent. All time is measured in FZERO_SIMULATION_HZ ticks.
 * Rule 1: two stock humans, five laps, movement then one stable-ID contact.
 * This is a new simulation profile, not a claim of stock whole-frame parity. */
#define FZERO_VERSUS_RULES 1u
#define FZERO_VERSUS_LAPS 5u
#define FZERO_VERSUS_COUNTDOWN 180u
typedef struct FzeroVersus FzeroVersus;
typedef struct FzeroVersusConfig { uint8_t course, machine[2]; } FzeroVersusConfig;
enum FzeroVersusPhase { FZERO_VS_READY, FZERO_VS_COUNTDOWN, FZERO_VS_RACING, FZERO_VS_RESULTS };
enum FzeroVersusStatus { FZERO_VS_ACTIVE, FZERO_VS_FINISHED, FZERO_VS_DNF };
enum FzeroVersusPause { FZERO_VS_PAUSE_MANUAL=1, FZERO_VS_PAUSE_FOCUS=2,
                        FZERO_VS_PAUSE_DEVICE1=4, FZERO_VS_PAUSE_DEVICE2=8 };
enum FzeroVersusWinner { FZERO_VS_NO_WINNER, FZERO_VS_PLAYER1, FZERO_VS_PLAYER2, FZERO_VS_TIE };
typedef struct FzeroVersusRacer {
  FzeroActor actor;
  FzeroActor crossing_pose;
  /* finish_tick freezes elapsed time for either terminal status (DNF too). */
  uint64_t finish_tick, lap_start, lap_ticks[FZERO_VERSUS_LAPS], crossing_tick;
  int32_t gates;
  uint8_t status, completed_laps, started;
} FzeroVersusRacer;
typedef struct FzeroVersusSnapshot {
  FzeroVersusConfig config;
  FzeroVersusRacer racers[2];
  FzeroActorMineEvent mines[2];
  uint64_t tick, race_tick;
  uint32_t countdown;
  uint8_t phase, pause, ready, contact, winner;
} FzeroVersusSnapshot;

/* world is the stock course prepared by the guest; spawns are distinct stock
 * grid slots. It is copied once, and never read from a live guest during ticks.
 * Machine-specific human fields are initialized from each selected machine. */
FzeroVersus *FzeroVersusCreate(FzeroVersusConfig config,const uint8_t world[0x20000],
                              const FzeroActor spawns[2],FzeroActorTables tables);
void FzeroVersusDestroy(FzeroVersus *session);
void FzeroVersusReady(FzeroVersus *session,unsigned player,bool ready);
void FzeroVersusPause(FzeroVersus *session,unsigned reason);
void FzeroVersusResume(FzeroVersus *session);
void FzeroVersusEndRace(FzeroVersus *session);
bool FzeroVersusRematch(FzeroVersus *session);
/* Returns true only when simulation advanced. Both words are latched before
 * either actor runs. Start from either participant pauses the entire session. */
bool FzeroVersusTick(FzeroVersus *session,const uint16_t held[2]);
const FzeroVersusSnapshot *FzeroVersusCompleted(const FzeroVersus *session);
const uint8_t *FzeroVersusWorld(const FzeroVersus *session);
/* Canonical little-endian, padding-free whole-session image with rules/version
 * and integrity check. Restore validates a temporary session before mutation.
 * expected may be NULL when the host permits loading a different stock race. */
size_t FzeroVersusSaveSize(void);
bool FzeroVersusSave(const FzeroVersus *session,void *output,size_t size);
bool FzeroVersusRestore(FzeroVersus *session,const void *input,size_t size,
                        const FzeroVersusConfig *expected);
uint64_t FzeroVersusHash(const FzeroVersus *session);
