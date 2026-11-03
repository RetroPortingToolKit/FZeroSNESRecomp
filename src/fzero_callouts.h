#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FZERO_CALLOUT_NONE, FZERO_CALLOUT_BOOST, FZERO_CALLOUT_FINAL, FZERO_CALLOUT_FINISH };
enum { FZERO_CALLOUT_PLAYERS=4 };
typedef struct FzeroLapObservation {
  uint64_t generation, race, tick;
  unsigned completed[FZERO_CALLOUT_PLAYERS], total;
  bool racing, paused;
  bool results; /* Same race after its terminal crossing; let the fanfare finish. */
} FzeroLapObservation;

/* Local presentation only: never saved, hashed, or emitted during rollback. */
bool FzeroCalloutsValid(const char *path);
bool FzeroCalloutsConfigure(bool enabled,const char *boost,const char *final,bool second_lap_only,const char *finish);
void FzeroCalloutsShutdown(void);
unsigned FzeroCalloutsObserve(const FzeroLapObservation *state);
void FzeroCalloutsUpdate(void);
