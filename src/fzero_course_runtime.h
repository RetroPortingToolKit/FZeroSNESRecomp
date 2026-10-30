#pragma once
#include "fzero_course.h"
#include "content_pack.h"
const FzeroCourse *FzeroTracksCurrentCourse(void);
const FzeroCourse *FzeroTracksCourseAt(unsigned cup, unsigned order);
bool FzeroTracksCurrentMusic(char *out, size_t cap);
/* Stable catalog key for music already selected by the resolver. */
bool FzeroTracksCurrentMusicKey(char *pack, size_t cap, unsigned *index);
unsigned FzeroTracksRequiredFeatures(void);
void FzeroTracksRefreshCourse(void);
void FzeroTracksInstallHooks(void);
bool FzeroTracksRuntimeSelect(unsigned index);
unsigned FzeroTracksRuntimeCount(void);
const CpCup *FzeroTracksRuntimeCup(unsigned index, const CpPack **pack);
const CpTrack *FzeroTracksRuntimeTrack(unsigned cup, unsigned order);
bool FzeroTracksRecordKey(unsigned cup, const char *vehicle, uint8_t key[32]);
void FzeroTracksReadRecords(unsigned cup, const char *vehicle, uint8_t records[0x400]);
void FzeroTracksOverlay(uint32_t *pixels, unsigned width, unsigned height, size_t pitch,const uint8_t *ram);
void FzeroTracksFlush(void);
void FzeroTracksMenuReset(void);
unsigned FzeroTracksMenuIndex(void);
bool FzeroTracksMenuVisible(void);
bool FzeroTracksMenuVisibleAt(const uint8_t *ram);
bool FzeroTracksClassSelectedAt(const uint8_t *ram);
struct Ppu;
void FzeroTracksIntroFrame(struct Ppu *ppu,const uint8_t *ram,bool begin);
bool FzeroTracksClassSelected(void);
void FzeroTracksSavesInit(void);
void FzeroTracksSavesFinish(void);
const char *FzeroTracksSaveBaseRoot(void);
const char *FzeroTracksSaveNamespace(void);
bool FzeroTracksRecordsSelect(const uint8_t *key);
size_t FzeroTracksSaveStateSize(void);
struct SaveLoadInfo;
void FzeroTracksSaveState(struct SaveLoadInfo *sli, bool load);
unsigned FzeroTracksCurrentCupSize(void);
unsigned FzeroTracksPracticeInput(void);
