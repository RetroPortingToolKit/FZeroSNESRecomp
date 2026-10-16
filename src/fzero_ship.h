#pragma once
#include "fzero_actor.h"

/* A camera-local raster of one stable actor. Zero pixels are transparent.
 * Followed actors use native human steering/tilt layouts; other actors use
 * the original opponent orientation/distance tables;
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
