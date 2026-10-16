#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool FzeroVehiclesActive(void);
bool FzeroVehiclesLoad(const uint8_t *stock, size_t size, char *error,
                       size_t cap);
bool FzeroVehiclesPrepare(uint8_t **rom, size_t *size, char *error, size_t cap);
void FzeroVehiclesSync(void);
void FzeroVehiclesLoaded(void);
void FzeroVehiclesInstallHooks(void);
const char *FzeroVehicleIdentity(void);
const char *FzeroVehicleRecordIdentity(unsigned index);
const char *FzeroVehicleRecordName(unsigned index);
bool FzeroVehicleRecordIcon(unsigned index, uint32_t pixels[16 * 16]);
unsigned FzeroVehicleCount(void);
unsigned FzeroVehicleSelected(void);
unsigned FzeroVehicleAcceleration(unsigned slot, unsigned speed);
unsigned FzeroVehicleTurn(unsigned speed);
struct Ppu;
void FzeroVehiclesRaster(struct Ppu *ppu, const uint8_t *ram, unsigned line);
void FzeroVehiclesBeginFrame(struct Ppu *ppu, const uint8_t *ram);
void FzeroVehiclesEndFrame(struct Ppu *ppu);

/* Independently verified artwork/card plus the same authored mechanics as1P.
 * Caller owns the returned 4MiB immutable resource image. No live image swap. */
uint8_t *FzeroVehicleVersusImage(const uint8_t *stock,size_t size,unsigned group,char *error,size_t cap);
