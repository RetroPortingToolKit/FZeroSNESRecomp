#pragma once
#include "fzero_actor.h"
#include "fzero_traffic.h"
#include <stddef.h>

/* A single game-owned authoritative race. Presentation and host device state
 * are deliberately absent. All time is measured in FZERO_SIMULATION_HZ ticks.
 * Rule 9: all twelve authored CGP profiles and shared native bumper traffic.
 * Two humans, five laps, movement then stable-ID contacts; never pauses.
 * This is a new simulation profile, not a claim of stock whole-frame parity. */
#define FZERO_VERSUS_RULES 10u
#define FZERO_VERSUS_LAPS 5u
#define FZERO_VERSUS_ARRIVAL 68u
#define FZERO_VERSUS_REV_COUNTDOWN 180u
#define FZERO_VERSUS_COUNTDOWN (FZERO_VERSUS_ARRIVAL+FZERO_VERSUS_REV_COUNTDOWN)
typedef struct FzeroVersus FzeroVersus;
typedef struct FzeroVersusConfig { uint16_t course; uint8_t machine[2]; } FzeroVersusConfig;
enum FzeroVersusPhase { FZERO_VS_READY, FZERO_VS_COUNTDOWN, FZERO_VS_RACING, FZERO_VS_RESULTS };
enum FzeroVersusStatus { FZERO_VS_ACTIVE, FZERO_VS_FINISHED, FZERO_VS_DNF, FZERO_VS_SURVIVED };
enum FzeroVersusPause { FZERO_VS_PAUSE_MANUAL=1, FZERO_VS_PAUSE_FOCUS=2,
                        FZERO_VS_PAUSE_DEVICE1=4, FZERO_VS_PAUSE_DEVICE2=8 };
enum FzeroVersusWinner { FZERO_VS_NO_WINNER, FZERO_VS_PLAYER1, FZERO_VS_PLAYER2, FZERO_VS_TIE };
typedef struct FzeroVersusRacer {
  FzeroActor actor;
  FzeroActor crossing_pose;
  /* finish_tick records the terminal tick. Only FINISHED has a finish time;
   * DNF and SURVIVED must never be displayed/recorded as a completed race. */
  uint64_t finish_tick, lap_start, lap_ticks[FZERO_VERSUS_LAPS], crossing_tick;
  int32_t gates;
  uint8_t status, completed_laps, started;
} FzeroVersusRacer;

/* Race completion counts the remaining field, rather than reacting to the
 * first DNF. Kept independent of the current two-seat host for future fields. */
bool FzeroVersusRaceComplete(const FzeroVersusRacer *racers,unsigned count);
typedef struct FzeroVersusSnapshot {
  FzeroVersusConfig config;
  FzeroVersusRacer racers[2];
  FzeroActorMineEvent mines[2];
  FzeroTrafficPose traffic[FZERO_TRAFFIC_COUNT];
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
/* Capture immutable native CPU-car resources before the first Ready. */
bool FzeroVersusEnableTraffic(FzeroVersus *session,const uint8_t native[0x20000]);
void FzeroVersusReady(FzeroVersus *session,unsigned player,bool ready);
void FzeroVersusPause(FzeroVersus *session,unsigned reason);
void FzeroVersusResume(FzeroVersus *session);
void FzeroVersusEndRace(FzeroVersus *session);
bool FzeroVersusRematch(FzeroVersus *session);
/* Explicit live-menu restart; restores initial racers and course world. */
bool FzeroVersusRestart(FzeroVersus *session);
/* Returns true only when simulation advanced. Both words are latched before
 * either actor runs. Multiplayer never pauses; the host handles Start's live menu. */
bool FzeroVersusTick(FzeroVersus *session,const uint16_t held[2]);
const FzeroVersusSnapshot *FzeroVersusCompleted(const FzeroVersus *session);
const uint8_t *FzeroVersusWorld(const FzeroVersus *session);
/* Canonical little-endian, padding-free whole-session image with rules/version
 * and integrity check. Restore validates a temporary session before mutation.
 * expected may be NULL when the host permits loading a different stock race. */
size_t FzeroVersusSaveSize(void);
bool FzeroVersusSave(const FzeroVersus *session,void *output,size_t size);
/* Allocate a fully validated session without needing a pre-existing race. */
FzeroVersus *FzeroVersusDecode(const void *input,size_t size,FzeroActorTables tables,
                              const FzeroVersusConfig *expected);
bool FzeroVersusRestore(FzeroVersus *session,const void *input,size_t size,
                        const FzeroVersusConfig *expected);
uint64_t FzeroVersusHash(const FzeroVersus *session);
