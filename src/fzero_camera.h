#pragma once
#include "fzero_actor.h"

/* Stock race camera display data, derived only from a completed actor pose
 * and immutable ROM tables. No live guest, CPU flags, OAM or simulation writes.
 * Each pane builds its own camera. Normal racing uses the authored perspective
 * tables; intro/overview zoom is a separate presentation mode. */
typedef struct FzeroCamera {
  int16_t coefficients[176][4];
  uint16_t center_x,center_y,scroll_x,scroll_y;
  uint16_t background_h,background_v,skyline_h,skyline_v;
  uint8_t inverse_angle;
} FzeroCamera;
bool FzeroCameraBuild(FzeroCamera *camera,const FzeroActor *actor,FzeroActorTables tables);
/* SNES scanlines are one-based. The race band starts at 48; line 224 retains
 * the final coefficient after HDMA's 47 + 112 + 64-line runs have completed. */
bool FzeroCameraAtLine(const FzeroCamera *camera,unsigned scanline,int16_t matrix[8]);
