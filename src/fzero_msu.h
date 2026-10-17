#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compose the user's Conn/Cubear v11 patch after the optional Deluxe image.
 * The original ROM file and built-in Deluxe payload are never modified. */
bool FzeroMsuPrepare(uint8_t **rom, size_t *size, const char *pack, const char *rom_path);
/* Resolve audio before preparing every derived vehicle cartridge. A custom
 * pack with the existing v11 patch uses its standard mapping; otherwise the
 * author-supplied CGP adapter handles the selected audio. */
bool FzeroMsuHasLegacyPatch(const char *pack);
bool FzeroMsuHasInstalledMusic(void);
bool FzeroMsuConfigure(const char *pack, bool cgp, const char *rom_path);
/* VS uses host playback, never a cartridge music patch or guest RAM flags. */
bool FzeroMsuConfigureVersus(const char *pack, bool cgp, const char *rom_path);
/* Once per presented simulation frame, outside rollback resimulation. */
void FzeroMsuUpdateVersus(void);
bool FzeroMsuApplyConfigured(uint8_t **rom, size_t *size);
bool FzeroMsuActive(void);
const char *FzeroMsuError(void);
bool FzeroMsuSelectSaveRoot(void);
void FzeroMsuRestoreAudio(const uint8_t *ram);
/* Course soundtrack namespaces coexist beside the user's selected primary
 * soundtrack. NULL source explicitly requests SNES fallback; empty = primary. */
void FzeroMsuSelectTrackSource(const char *source, bool race);
