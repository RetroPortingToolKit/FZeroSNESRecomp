#pragma once
#include "fzero_actor.h"

/* A camera-local raster of one stable actor. Zero pixels are transparent.
 * Geometry and art are derived from the original opponent display tables;
 * neither visibility nor animation selection feeds back into simulation. */
typedef struct FzeroShip {
  uint32_t pixels[64 * 64];
  int x, y, ground_x, ground_y;
  unsigned size, layout, graphic;
  bool visible;
} FzeroShip;
bool FzeroShipBuild(FzeroShip *ship, const FzeroActor *camera,
                    const FzeroActor *actor, FzeroActorTables tables);
unsigned FzeroDisplaySpeed(uint16_t speed);
