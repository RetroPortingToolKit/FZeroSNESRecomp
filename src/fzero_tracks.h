#pragma once
#include "content_pack.h"
#include <stdbool.h>
#include <stdio.h>
#include "fzero_music_sources.h"

/* The catalog is independent of the running cartridge. Registering a pack
 * never patches another pack or replaces a built-in identity. */
bool FzeroTracksInit(const char *root, bool deluxe_available);
bool FzeroTrackLoaderEnabled(void);
void FzeroTrackLoaderEnable(bool enabled);
const CpCatalog *FzeroTracksCatalog(void);
const char *FzeroTracksError(void);
const char *FzeroTracksRoot(void);
/* Read optional soundtrack descriptors belonging to registered track packs,
 * including disabled packs so their namespaces cannot impersonate another. */
bool FzeroTracksReadMusicSources(FzeroMusicSources *sources);
void FzeroTracksReport(const char *message);
void FzeroTracksDiscover(const uint8_t *stock, size_t size);
const char *FzeroTracksPatch(const CpPack *pack);
bool FzeroTracksAvailable(const CpPack *pack);
bool FzeroTracksBundled(const CpPack *pack);
bool FzeroTracksHidden(const CpPack *pack);
bool FzeroTracksEnabled(const CpPack *pack);
bool FzeroTracksEnable(const CpPack *pack, bool enabled);
bool FzeroTracksTitleEnabled(void);
void FzeroTracksEnableTitle(bool enabled);
const char *FzeroTracksTitleStyle(void);
bool FzeroTracksSetTitleStyle(const char *style);
bool FzeroTracksSetPatch(const CpPack *pack, const char *path);
bool FzeroTracksSave(void);
/* Serialize effective choices without altering the installed settings. */
bool FzeroTracksWriteSettings(FILE *file);
/* Diagnostics are catalog-wide: bad files never disappear silently. */
unsigned FzeroTracksDiagnosticCount(void);
const char *FzeroTracksDiagnostic(unsigned index);

/* Game adapter (not part of the generic catalog). */
bool FzeroTracksPrepare(uint8_t **rom, size_t *size, bool deluxe, const char *deluxe_path);
bool FzeroTracksActive(void);
/* Snapshot identity also includes independent title artwork. */
bool FzeroTracksStateActive(void);
const uint8_t *FzeroTracksActiveHash(void);
const char *FzeroTracksActiveId(void);
bool FzeroTracksSelectSaveRoot(void);
void FzeroTracksMenuTick(uint8_t *ram, uint32_t previous_scene);

uint16_t FzeroTracksMenuInput(uint16_t input, const uint8_t *ram);
void FzeroTracksMenuState(uint8_t state[2], bool load);
