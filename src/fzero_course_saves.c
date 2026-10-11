#ifdef _WIN32
#include <windows.h>
#undef FORCEINLINE
#endif
#include "fzero_course_runtime.h"
#include "fzero_tracks.h"
#include "fzero_deluxe.h"
#include "fzero_records.h"
#include "common_rtl.h"
#include "sha256.h"
#include "snes/saveload.h"
#include <stdio.h>
#include <string.h>

/* Guest records still use their native slots. A cup has a stable private
 * namespace, and the original cartridge's records remain the base context.
 * The backup is part of every library snapshot and rewind frame. */
typedef struct CourseSaveState {
  uint8_t external, backup_valid, read_only, view;
  union { uint8_t key[32]; FzeroRecordsView browser; };
  uint8_t base_sram[0x8000], base_records[0x200];
} CourseSaveState;
static CourseSaveState state;
_Static_assert(sizeof(FzeroRecordsView) <= 32, "Records view must fit the snapshot key");
_Static_assert(sizeof(CourseSaveState) == 4 + 32 + 0x8000 + 0x200,
               "Preserve the legacy course-save snapshot layout");
static char base_root[96];
static bool set_root(const uint8_t *key) {
  if (!key) {
    RtlSetSaveRoot(base_root);
    return true;
  }
  char hex[65], root[96];
  cp_hash_format(key, hex);
  /* 128-bit path component; the full digest is checked in records.bin. */
  if (snprintf(root, sizeof(root), "%s/courses/%.32s", base_root, hex) >= (int)sizeof(root))
    return false;
  char parent[96];
  snprintf(parent, sizeof(parent), "%s/courses", base_root);
  /* A first run with only a track pack has no Deluxe/rules directory setup. */
  RtlSetSaveRoot(base_root);
  RtlEnsureSaveDir();
  RtlSetSaveRoot(parent);
  RtlEnsureSaveDir();
  RtlSetSaveRoot(root);
  RtlEnsureSaveDir();
  return true;
}
void FzeroTracksSavesInit(void) {
  memset(&state, 0, sizeof(state));
  snprintf(base_root, sizeof(base_root), "%s", RtlSaveRoot());
  /* The quick slot must remain reachable after returning to the title or
   * changing courses. Course/vehicle battery records still switch roots. */
  RtlSetQuickSaveRoot(base_root);
}
void FzeroTracksFlush(void) {
  if (!state.external || state.read_only)
    return;
  char path[160], temp[164];
  snprintf(path, sizeof(path), "%s/records.bin", RtlSaveRoot());
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  FILE *f = fopen(temp, "wb");
  if (!f) {
    FzeroTracksReport("Cannot save imported cup records");
    return;
  }
  bool ok = fwrite(state.key, 1, 32, f) == 32 &&
            fwrite(g_sram, 1, g_sram_size, f) == (size_t)g_sram_size &&
            fwrite(g_ram + 0x14800, 1, 0x200, f) == 0x200;
  if (fclose(f))
    ok = false;
#ifdef _WIN32
  if (ok)
    ok = MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (ok)
    ok = rename(temp, path) == 0;
#endif
  if (!ok)
    FzeroTracksReport("Could not finish saving imported cup records");
}
/* Canonical record reset: 55 BCD times (9:59.99) and their 16-bit
 * byte-sum checksum per five-course league. Verified against the stock
 * $02c541 and Deluxe $1eb7bc initialization routines. Keep unlock flags. */
static void clear_league(unsigned start, unsigned times) {
  unsigned sum = 0;
  static const uint8_t empty_time[] = {0x09, 0x59, 0x99};
  for (unsigned i = 0; i < times * 3; ++i) {
    uint8_t value = empty_time[i % 3];
    g_sram[start + i] = value;
    sum += value;
  }
  g_sram[start + times * 3] = (uint8_t)sum;
  g_sram[start + times * 3 + 1] = (uint8_t)(sum >> 8);
}
static void fresh_records(void) {
  clear_league(0x005, 55);
  clear_league(0x0ac, 55);
  clear_league(0x153, 55);
  if (FzeroDeluxeActive() && g_sram_size >= 0x400) {
    clear_league(0x205, 55);
    clear_league(0x2ac, 55);
    clear_league(0x353, 22);
  }
  memcpy(g_ram + 0x14800, g_sram, 0x200);
}
bool FzeroTracksRecordsSelect(const uint8_t *key) {
  FzeroRecordsViewEnd();
  if ((!key && !state.external) || (key && state.external && !memcmp(key, state.key, 32)))
    return true;
  if (g_sram_size < 0x200 || g_sram_size > 0x8000)
    return false;
  FzeroTracksFlush();
  if (state.external && state.backup_valid) {
    memcpy(g_sram, state.base_sram, g_sram_size);
    memcpy(g_ram + 0x14800, state.base_records, 0x200);
  }
  state.external = 0;
  state.read_only = 0;
  if (!key) {
    set_root(NULL);
    return true;
  }
  memcpy(state.base_sram, g_sram, g_sram_size);
  memcpy(state.base_records, g_ram + 0x14800, 0x200);
  state.backup_valid = 1;
  if (!set_root(key)) {
    set_root(NULL);
    FzeroTracksReport("Save root too long for imported cup records");
    return false;
  }
  memcpy(state.key, key, 32);
  state.external = 1;
  fresh_records();
  char path[160];
  snprintf(path, sizeof(path), "%s/records.bin", RtlSaveRoot());
  FILE *f = fopen(path, "rb");
  if (f) {
    uint8_t hash[32], sram[0x8000], records[0x200];
    bool ok = fread(hash, 1, 32, f) == 32 && !memcmp(hash, key, 32) &&
              fread(sram, 1, g_sram_size, f) == (size_t)g_sram_size &&
              fread(records, 1, 0x200, f) == 0x200 && fgetc(f) == EOF;
    fclose(f);
    if (ok) {
      memcpy(g_sram, sram, g_sram_size);
      memcpy(g_ram + 0x14800, records, 0x200);
    } else {
      state.read_only = 1;
      FzeroTracksReport(
          "Imported cup records failed validation; this session will preserve that file");
    }
  }
  return true;
}
FzeroRecordsView *FzeroRecordsViewState(void) {
  return state.view ? &state.browser : NULL;
}
bool FzeroRecordsViewBegin(void) {
  if (state.view) return true;
  if (!FzeroTracksRecordsSelect(NULL)) return false;
  memcpy(state.base_sram, g_sram, g_sram_size);
  memcpy(state.base_records, g_ram + 0x14800, 0x200);
  state.backup_valid = state.view = 1;
  memset(state.key, 0, sizeof(state.key));
  return true;
}
void FzeroRecordsViewEnd(void) {
  if (!state.view) return;
  memcpy(g_sram, state.base_sram, g_sram_size);
  memcpy(g_ram + 0x14800, state.base_records, 0x200);
  state.view = 0;
}
/* Reading a context never changes the save root, creates directories, or
 * installs it as writable SRAM. Missing/bad files yield an empty display. */
bool FzeroRecordsRead(const uint8_t *key, uint8_t records[0x400]) {
  memset(records, 0, 0x400);
  const unsigned starts[] = {5, 0xac, 0x153, 0x205, 0x2ac, 0x353};
  for (unsigned league = 0; league < 6; ++league)
    for (unsigned i = 0; i < (league == 5 ? 22 : 55); ++i)
      memcpy(records + starts[league] + i * 3, "\x09\x59\x99", 3);
  if (!key) {
    memcpy(records, (state.view || state.external) ? state.base_sram : g_sram, 0x400);
    return true;
  }
  char hex[65], path[160];
  cp_hash_format(key, hex);
  snprintf(path, sizeof(path), "%s/courses/%.32s/records.bin", base_root, hex);
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint8_t hash[32], sram[0x8000], mirror[0x200];
  bool ok = fread(hash, 1, 32, f) == 32 && !memcmp(hash, key, 32) &&
      fread(sram, 1, g_sram_size, f) == (size_t)g_sram_size &&
      fread(mirror, 1, sizeof(mirror), f) == sizeof(mirror) && fgetc(f) == EOF;
  fclose(f);
  if (ok) memcpy(records, sram, 0x400);
  else FzeroTracksReport("Records file failed validation; displaying empty records without modifying it");
  return ok;
}
static unsigned time_value(const uint8_t *time) {
  return ((unsigned)(time[0] & 15) << 16) | ((unsigned)time[1] << 8) | time[2];
}
void FzeroRecordsMergeCup(uint8_t records[0x400], const uint8_t previous[0x400], unsigned cup) {
  static const unsigned starts[] = {5, 0xac, 0x153, 0x205, 0x2ac};
  if (cup >= 5) return;
  unsigned start = starts[cup];
  for (unsigned course = 0; course < 5; ++course) {
    uint8_t *dest = records + start + course * 33;
    const uint8_t *old = previous + start + course * 33;
    for (unsigned row = 0; row < 10; ++row) {
      const uint8_t *candidate = old + row * 3;
      if ((candidate[0] & 15) >= 9) continue;
      bool duplicate = false;
      for (unsigned i = 0; i < 10; ++i)
        if (!memcmp(dest + i * 3, candidate, 3)) duplicate = true;
      if (duplicate) continue;
      for (unsigned i = 0; i < 10; ++i) {
        if (time_value(candidate) < time_value(dest + i * 3)) {
          memmove(dest + (i + 1) * 3, dest + i * 3, (9 - i) * 3);
          memcpy(dest + i * 3, candidate, 3);
          break;
        }
      }
    }
    if (time_value(old + 30) < time_value(dest + 30)) memcpy(dest + 30, old + 30, 3);
  }
  unsigned sum = 0;
  for (unsigned i = 0; i < 165; ++i) sum += records[start + i];
  records[start + 165] = (uint8_t)sum;
  records[start + 166] = (uint8_t)(sum >> 8);
}
size_t FzeroTracksSaveStateSize(void) {
  return sizeof(state);
}
void FzeroTracksSaveState(SaveLoadInfo *sli, bool load) {
  sli->func(sli, &state, sizeof(state));
  if (load)
    set_root(state.external ? state.key : NULL);
}
void FzeroTracksSavesFinish(void) {
  FzeroTracksRecordsSelect(NULL);
}
