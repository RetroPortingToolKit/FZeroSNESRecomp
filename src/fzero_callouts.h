#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { FZERO_CALLOUT_NONE, FZERO_CALLOUT_BOOST, FZERO_CALLOUT_FINAL };
typedef struct FzeroLapObservation {
  uint64_t generation, race, tick;
  unsigned completed[2], total;
  bool racing, paused;
} FzeroLapObservation;

/* Local presentation only: never saved, hashed, or emitted during rollback. */
bool FzeroCalloutsValid(const char *path);
bool FzeroCalloutsConfigure(bool enabled,const char *boost,const char *final);
void FzeroCalloutsShutdown(void);
unsigned FzeroCalloutsObserve(const FzeroLapObservation *state);
void FzeroCalloutsUpdate(void);
