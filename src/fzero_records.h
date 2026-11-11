#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fits the formerly unused key while a read-only records view is active.
 * This keeps the existing snapshot/rewind trailer layout compatible. */
typedef struct FzeroRecordsView {
  uint16_t page, previous_cup, input, hold;
  uint8_t vehicle, reserved, selected, display_selected;
  uint16_t label_tick;
  uint8_t display_detail; /* Last loaded page, retained through native fades. */
} FzeroRecordsView;
FzeroRecordsView *FzeroRecordsViewState(void);
bool FzeroRecordsViewBegin(void);
void FzeroRecordsViewEnd(void);
bool FzeroRecordsRead(const uint8_t *key, uint8_t records[0x400]);
/* Read-only source lookup; only a validated native ghost stream is copied. */
bool FzeroRecordsImportGhost(const uint8_t key[32], unsigned course);
void FzeroRecordsGhostWritten(void);
void FzeroRecordsGhostUnavailable(void);
uint8_t *FzeroRecordsRowVehicles(unsigned slot);
void FzeroRecordsMergeCup(uint8_t records[0x400], const uint8_t previous[0x400], unsigned cup);
void FzeroRecordsTick(void);
uint16_t FzeroRecordsInput(uint16_t input);
void FzeroRecordsInstallHooks(void);
void FzeroRecordsOverlay(uint32_t *pixels, unsigned width, unsigned height, size_t pitch,const uint8_t *ram);
bool FzeroRecordsDetail(void);
unsigned FzeroRecordsCup(void);
unsigned FzeroRecordsOrder(void);
