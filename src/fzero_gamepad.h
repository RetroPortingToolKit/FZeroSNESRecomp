#pragma once

#include "desktop/sdl_compat.h"

/* Own the gameplay handle independently of the launcher's SDL lifetime. */
void FzeroGamepadConfigure(const char *config, const char *guid, int deadzone);
void FzeroGamepadRefresh(SDL_GameController **pad);
void FzeroGamepadEvent(SDL_GameController **pad, const SDL_Event *event);
uint32_t FzeroGamepadRead(SDL_GameController *pad);
/* Independent local-player profile. Common device bindings are inherited;
 * [Local2P.P1]/[Local2P.P2] Controls and Deadzone override them separately. */
uint32_t FzeroGamepadReadPlayer(SDL_GameController *pad,unsigned player);
/* Read configured host shortcuts using recomp-ui's portable encoding.
 * Active buttons/axes are consumed before producing the guest input word. */
uint32_t FzeroGamepadReadShortcuts(SDL_GameController *pad, const int *bindings,
                                  unsigned count, uint32_t *active);
