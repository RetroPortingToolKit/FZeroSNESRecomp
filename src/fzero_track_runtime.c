#include "fzero_tracks.h"
#include "fzero_packs.h"
#include "fzero_course_runtime.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay.h"
#include "fzero_vehicles.h"
#include "fzero_title.h"
#include "fzero_records.h"
#include "cpu_state.h"
#include "common_rtl.h"
#include "snes/interp_bridge.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ImportedCourses {
  const CpPack *pack;
  FzeroCourse *courses;
} ImportedCourses;
static ImportedCourses imported[CP_PACKS];
static unsigned imported_count;
static const CpPack *active_pack;
static const CpCup *active_cup;
static unsigned menu_index;
static uint8_t identity[32];
static uint16_t menu_last_input;
static char save_namespace[96];
const char *FzeroTracksSaveNamespace(void) { return save_namespace; }
static unsigned menu_hold;
static const FzeroCourse *course;
bool FzeroTracksActive(void) {
  return imported_count != 0 || FzeroGameplayActive();
}
bool FzeroTracksStateActive(void) {
  /* Empty-save browsing needs snapshot state, without enabling cup overlays. */
  return FzeroTracksActive() || FzeroTitleHash() || FzeroGameplaySettingsCurrent()->always_records;
}
const uint8_t *FzeroTracksActiveHash(void) {
  return identity;
}
const char *FzeroTracksActiveId(void) {
  return "f_zero_library";
}
const FzeroCourse *FzeroTracksCurrentCourse(void) {
  return course;
}
bool FzeroTracksCurrentMusic(char *out, size_t cap) {
  if (course)
    for (unsigned i = 0; i < imported_count; ++i)
      for (unsigned t = 0; t < imported[i].pack->track_count; ++t)
        if (course == &imported[i].courses[t])
          return FzeroPacksCourseMusic(imported[i].pack->id, t, out, cap);
  return false;
}
unsigned FzeroTracksRequiredFeatures(void) {
  unsigned features = 0;
  for (unsigned i = 0; i < imported_count; ++i)
    for (unsigned j = 0; j < imported[i].pack->track_count; ++j)
      features |= imported[i].courses[j].required;
  return features;
}
static bool imported_pack(const CpPack *p) {
  for (unsigned i = 0; i < imported_count; ++i)
    if (imported[i].pack == p)
      return true;
  return false;
}
const CpCup *FzeroTracksRuntimeCup(unsigned n, const CpPack **pack) {
  const CpCatalog *cat = FzeroTracksCatalog();
  const CpPack *base = cp_catalog_find(cat, FzeroDeluxeActive() ? "bs-deluxe" : "retail");
  unsigned base_count=FzeroBsTracks() ? 5 : 3;
  if (base && n < base_count) {
    if (pack)
      *pack = base;
    return &base->cups[n];
  }
  if (base)
    n -= base_count;
  for (unsigned i = 0; i < imported_count; ++i) {
    const CpPack *p = imported[i].pack;
    if (n < p->cup_count) {
      if (pack)
        *pack = p;
      return &p->cups[n];
    }
    n -= p->cup_count;
  }
  return NULL;
}
unsigned FzeroTracksRuntimeCount(void) {
  unsigned n = FzeroBsTracks() ? 5 : 3;
  for (unsigned i = 0; i < imported_count; ++i)
    n += imported[i].pack->cup_count;
  return n;
}
const CpTrack *FzeroTracksRuntimeTrack(unsigned n, unsigned order) {
  const CpPack *p;
  const CpCup *cup = FzeroTracksRuntimeCup(n, &p);
  if (cup)
    for (unsigned i = 0; i < p->track_count; ++i)
      if (!strcmp(p->tracks[i].cup, cup->id) && !order--)
        return &p->tracks[i];
  return NULL;
}
bool FzeroTracksRecordKey(unsigned n, const char *vehicle, uint8_t result[32]) {
  const CpPack *p = NULL;
  const CpCup *cup = FzeroTracksRuntimeCup(n, &p);
  if (!cup)
    return false;
  memset(result, 0, 32);
  if (imported_pack(p)) {
    uint8_t key[CP_ID * 2 + CP_TRACKS * (CP_ID + 32)] = {0};
    size_t pos = CP_ID * 2;
    memcpy(key, p->id, strlen(p->id));
    memcpy(key + CP_ID, cup->id, strlen(cup->id));
    for (unsigned i = 0; i < imported_count; ++i)
      if (imported[i].pack == p)
        for (unsigned t = 0; t < p->track_count; ++t)
          if (!strcmp(p->tracks[t].cup, cup->id)) {
            memcpy(key + pos, p->tracks[t].id, strlen(p->tracks[t].id));
            pos += CP_ID;
            memcpy(key + pos, imported[i].courses[t].hash, 32);
            pos += 32;
          }
    sha256_compute(key, pos, result);
  }
  if (vehicle) {
    uint8_t data[32 + 3 * CP_ID] = {0};
    memcpy(data, result, 32);
    memcpy(data + 32, p->id, strlen(p->id));
    memcpy(data + 32 + CP_ID, cup->id, strlen(cup->id));
    memcpy(data + 32 + 2 * CP_ID, vehicle, strlen(vehicle));
    sha256_compute(data, sizeof(data), result);
  }
  return imported_pack(p) || vehicle;
}
/* Old test builds split native Practice records across a second file. Read
 * those times into the unified view, but never create or write that namespace.
 * Existing GP and imported-cup keys remain unchanged. */
static bool previous_practice(unsigned n, const char *vehicle, uint8_t records[0x400]) {
  const CpPack *p = NULL;
  if (!vehicle || !FzeroTracksRuntimeCup(n, &p) || imported_pack(p)) return false;
  uint8_t data[32 + 3 * CP_ID] = {0}, key[32];
  const char *pack = FzeroDeluxeActive() ? "bs-deluxe" : "retail";
  memcpy(data + 32, pack, strlen(pack));
  memcpy(data + 32 + CP_ID, "practice", 8);
  memcpy(data + 32 + 2 * CP_ID, vehicle, strlen(vehicle));
  sha256_compute(data, sizeof(data), key);
  return FzeroRecordsRead(key, records);
}
void FzeroTracksReadRecords(unsigned n, const char *vehicle, uint8_t records[0x400]) {
  uint8_t key[32], previous[0x400];
  FzeroRecordsRead(FzeroTracksRecordKey(n, vehicle, key) ? key : NULL, records);
  const CpCup *cup = FzeroTracksRuntimeCup(n, NULL);
  if (cup && previous_practice(n, vehicle, previous))
    FzeroRecordsMergeCup(records, previous, cup->slot);
}
bool FzeroTracksRuntimeSelect(unsigned n) {
  const CpPack *p;
  const CpCup *cup = FzeroTracksRuntimeCup(n, &p);
  if (!cup) return false;
  menu_index = n;
  active_pack = p;
  active_cup = cup;
  course = NULL;
  return true;
}
unsigned FzeroTracksCurrentCupSize(void) {
  if (!active_cup || !imported_pack(active_pack) || g_ram[0x58])
    return 0;
  unsigned count = 0;
  for (unsigned i = 0; i < active_pack->track_count; ++i)
    count += !strcmp(active_pack->tracks[i].cup, active_cup->id);
  return count;
}
static const FzeroCourse *imported_course(const CpPack *p, const CpCup *cup, unsigned order) {
  if (!cup || !imported_pack(p))
    return NULL;
  for (unsigned i = 0; i < imported_count; ++i)
    if (imported[i].pack == p)
      for (unsigned t = 0; t < p->track_count; ++t)
        if (!strcmp(p->tracks[t].cup, cup->id)) {
          if (!order--) return &imported[i].courses[t];
        }
  return NULL;
}
const FzeroCourse *FzeroTracksCourseAt(unsigned index, unsigned order) {
  const CpPack *pack = NULL;
  const CpCup *cup = FzeroTracksRuntimeCup(index, &pack);
  return imported_course(pack, cup, order);
}
static void current_course(void) {
  course = NULL;
  if (FzeroRecordsDetail()) {
    course = FzeroTracksCourseAt(FzeroRecordsCup(), FzeroRecordsOrder());
  } else if (g_ram[0x54]) {
    const char *test = getenv("FZERO_TEST_COURSE");
    unsigned order = test ? (unsigned)strtoul(test, NULL, 10) : g_ram[0x53];
    course = imported_course(active_pack, active_cup, order);
  }
}
bool FzeroTracksPrepare(uint8_t **rom, size_t *size, bool deluxe, const char *deluxe_path) {
  for (unsigned i = 0; i < imported_count; ++i)
    free(imported[i].courses);
  imported_count = 0;
  active_pack = NULL;
  active_cup = NULL;
  course = NULL;
  menu_index = 0;
  FzeroTitleReset();
  FzeroTracksDiscover(*rom, *size);
  const CpCatalog *cat = FzeroTracksCatalog();
  char error[256];
  const FzeroTitleScreen *title = FzeroTitleFind(FzeroTracksTitleStyle());
  if (FzeroTracksTitleEnabled() && title && *title->patch &&
      !FzeroTitlePrepare(*rom, *size, title->patch, error, sizeof(error))) {
    char message[256];
    snprintf(message, sizeof(message), "Title screen: %.160s; using Original", error);
    FzeroTracksReport(message);
  }
  for (unsigned i = 0; i < cat->count; ++i) {
    const CpPack *p = cat->packs[i];
    if (strcmp(p->adapter, "fzero-course-v1") || !FzeroTracksAvailable(p))
      continue;
    bool ok = true;
    FzeroCourse *courses = calloc(p->track_count, sizeof(*courses));
    if (ok && !courses) {
      snprintf(error, sizeof(error), "Out of memory");
      ok = false;
    }
    for (unsigned j = 0; ok && j < p->cup_count; ++j) {
      unsigned count = 0;
      for (unsigned t = 0; t < p->track_count; ++t)
        count += !strcmp(p->tracks[t].cup, p->cups[j].id);
      if (count > 5) {
        snprintf(error, sizeof(error), "This GP adapter supports 1 to 5 courses per cup");
        ok = false;
      }
    }
    for (unsigned t = 0; ok && t < p->track_count; ++t)
      ok = FzeroPacksLoadCourse(p->id, t, &courses[t], error, sizeof(error));
    if (!ok) {
      free(courses);
      char message[256];
      snprintf(message, sizeof(message), "%.80s: %.160s", p->name, error);
      FzeroTracksReport(message);
      continue;
    }
    imported[imported_count++] = (ImportedCourses){p, courses};
    fprintf(stderr, "[track-library] loaded %s: %u standalone courses\n", p->id,
            p->track_count);
  }
  (void)deluxe;
  if (!FzeroVehiclesLoad(*rom,*size,error,sizeof(error))) { FzeroTracksReport(error); return false; }
  if (!FzeroDeluxePrepare(rom, size, FzeroBsCars() || FzeroBsTracks() || FzeroVehiclesActive(), deluxe_path))
    return false;
  if (!FzeroVehiclesPrepare(rom,size,error,sizeof(error))) { FzeroTracksReport(error); return false; }
  if (FzeroTracksStateActive()) {
    /* The canonical native interrupt module remains selected. Resource
     * callbacks run at the same loader sites for every imported pack. */
    interp_bridge_set_scheduler_aot_policy(0);
    /* Snapshot indices depend on both resource bytes and stable IDs/order.
     * Hash only initialized manifest records, never a pointer or file path. */
    uint8_t hashes[CP_PACKS * 32 + 73] = {0};
    hashes[0] = (uint8_t)FzeroDeluxeActive();
    /* GP roster membership no longer depends on retail tuning. Old snapshots
     * may contain actors/art from the former cohort; reject those without
     * changing the gameplay signature used by existing course records. */
    if (FzeroVehiclesActive()) hashes[0] |= 2;
    const FzeroGameplaySettings *rules=FzeroGameplaySettingsCurrent();
    /* Author-group columns change saved menu/rival indices, not record keys. */
    if (rules->vehicle_packs) hashes[0] |= 4;
    /* Older info-panel snapshots retain retail card tiles/palettes in VRAM. */
    if (FzeroVehiclesActive()) hashes[0] |= 8;
    for (unsigned j=0;j<4;++j) hashes[1+j]=(uint8_t)(rules->enabled>>(j*8));
    hashes[5]=FzeroRuleEnabled(FZERO_RULE_TUNING)?(uint8_t)rules->tuning:0;
    hashes[6]=FzeroRuleEnabled(FZERO_RULE_BOOST)?(uint8_t)rules->boost:0;
    hashes[7]=FzeroRuleEnabled(FZERO_RULE_EXHAUST)?(uint8_t)rules->exhaust:0;
    hashes[8]=(uint8_t)(FzeroBsCars() | (FzeroBsTracks()<<1));
    memcpy(hashes+9,FzeroGameplaySignature(),32);
    for (unsigned i = 0; i < imported_count; ++i) {
      const CpPack *p = imported[i].pack;
      size_t length =
          CP_ID + p->cup_count * sizeof(CpCup) + p->track_count * (sizeof(CpTrack) + 32);
      uint8_t *data = calloc(1, length);
      if (!data)
        return false;
      memcpy(data, p->id, CP_ID);
      size_t pos = CP_ID;
      memcpy(data + pos, p->cups, p->cup_count * sizeof(CpCup));
      pos += p->cup_count * sizeof(CpCup);
      for (unsigned t = 0; t < p->track_count; ++t) {
        memcpy(data + pos, &p->tracks[t], sizeof(CpTrack));
        pos += sizeof(CpTrack);
        memcpy(data + pos, imported[i].courses[t].hash, 32);
        pos += 32;
      }
      sha256_compute(data, length, hashes + 41 + i * 32);
      free(data);
    }
    size_t length = 41 + imported_count * 32;
    /* Title assets affect snapshot compatibility, never course records or the
     * gameplay signature. Preserve existing snapshot identity when off. */
    if (FzeroTitleHash()) {
      memcpy(hashes + length, FzeroTitleHash(), 32);
      length += 32;
      fprintf(stderr, "[track-library] %s title artwork enabled\n",
          title->name);
    }
    sha256_compute(hashes, length, identity);
  }
  FzeroTracksRuntimeSelect(0);
  const char *key = getenv("FZERO_CUP");
  if (key && *key) {
    bool found = false;
    for (unsigned i = 0; i < FzeroTracksRuntimeCount(); ++i) {
      const CpPack *p;
      const CpCup *c = FzeroTracksRuntimeCup(i, &p);
      char k[CP_ID * 2];
      snprintf(k, sizeof(k), "%s/%s", p->id, c->id);
      if (!strcmp(k, key)) {
        FzeroTracksRuntimeSelect(i);
        found = true;
        break;
      }
    }
    if (!found) {
      FzeroTracksReport("Requested FZERO_CUP is unavailable in this library");
      return false;
    }
  }
  return true;
}
bool FzeroTracksSelectSaveRoot(void) {
  size_t original_root_length=strlen(RtlSaveRoot());
  if (!FzeroDeluxeSelectSaveRoot())
    return false;
  if (FzeroGameplaySettingsCurrent()->enabled || FzeroVehiclesActive()) {
    char hex[65],root[96]; cp_hash_format(FzeroGameplaySignature(),hex);
    if(snprintf(root,sizeof(root),"%s/rules-%.16s",RtlSaveRoot(),hex)>=(int)sizeof(root))return false;
    RtlEnsureSaveDir();RtlSetSaveRoot(root);RtlEnsureSaveDir();
  }
  if (FzeroTracksActive() && strlen(RtlSaveRoot()) + 41 >= 96) {
    FzeroTracksReport(
        "Save root is too long for course records; use a shorter SNESRECOMP_SAVE_ROOT");
    return false;
  }
  FzeroTracksSavesInit();
  const char *suffix=RtlSaveRoot()+original_root_length;
  if (*suffix=='/' || *suffix=='\\') ++suffix;
  snprintf(save_namespace,sizeof(save_namespace),"%s",suffix);
  return true;
}
static bool cup_menu(const uint8_t *ram) {
  return FzeroDeluxeActive() ? ram[0x54] == 1 && ram[0x55] == 1 &&
                                 (ram[0x58] ? ram[0x56] == 4
                                            : ram[0x56] == 2 && !ram[0x14c98])
                             : ram[0x54] == 1 && ram[0x55] == 5 && !ram[0x58];
}
static unsigned cup_courses(void) {
  unsigned n = 0;
  if (active_pack && active_cup)
    for (unsigned i = 0; i < active_pack->track_count; ++i)
      n += !strcmp(active_pack->tracks[i].cup, active_cup->id);
  return n ? n : 5;
}
unsigned FzeroTracksPracticeInput(void) {
  unsigned buttons = g_ram[0x67] | (unsigned)g_ram[0x68] << 8;
  if (buttons & 0x9080)
    return 2;
  bool back = (buttons & 0xc00) == 0x800;
  bool forward = (buttons & 0xc00) == 0x400 || (buttons & 0x2f30) == 0x2000;
  if (!back && !forward)
    return 0;
  unsigned order = g_ram[0x53], cups = FzeroTracksRuntimeCount();
  if (back && !order) {
    FzeroTracksRuntimeSelect((menu_index + cups - 1) % cups);
    order = cup_courses() - 1;
  } else if (forward && order + 1 >= cup_courses()) {
    FzeroTracksRuntimeSelect((menu_index + 1) % cups);
    order = 0;
  } else
    order += back ? -1 : 1;
  g_ram[0x53] = (uint8_t)order;
  g_ram[0x90] = imported_pack(active_pack) ? 0 : (uint8_t)active_cup->slot;
  g_ram[0x14cfa] = g_ram[0x14cfb] = 0;
  current_course();
  fprintf(stderr, "[track-library] practice %u/%u course=%u\n", menu_index + 1, cups, order + 1);
  return 1;
}
void FzeroTracksMenuState(uint8_t state[2], bool load) {
  if (load) {
    FzeroTracksRuntimeSelect(state[0] | (unsigned)state[1] << 8);
    menu_last_input = 0;
    menu_hold = 0;
    current_course();
  } else {
    state[0] = (uint8_t)menu_index;
    state[1] = (uint8_t)(menu_index >> 8);
  }
}
void FzeroTracksMenuReset(void) {
  menu_last_input = 0;
  menu_hold = 0;
  course = NULL;
}
void FzeroTracksMenuTick(uint8_t *ram, uint32_t previous_scene) {
  /* The native Practice league screen starts at Knight independently of the
   * GP selection. Subsequent preview navigation uses this same live catalog. */
  if (FzeroTracksActive() && ram[0x58] && ram[0x54] == 1 && ram[0x55] == 1 &&
      ram[0x56] == 3 && (previous_scene >> 16) != 3)
    FzeroTracksRuntimeSelect(0);
  FzeroRecordsTick();
  if (g_ram[0x54] == 0 && !FzeroRecordsViewState())
    FzeroTracksRecordsSelect(NULL);
  current_course();
}
uint16_t FzeroTracksMenuInput(uint16_t input, const uint8_t *ram) {
  if (FzeroRecordsViewState()) return FzeroRecordsInput(input);
  if (getenv("FZERO_TRACE_LIBRARY") && input)
    fprintf(stderr, "[library-input] %u scene=%u,%u,%u active=%u deluxe=%u index=%u\n", input,
            ram[0x54], ram[0x55], ram[0x56], imported_count, FzeroDeluxeActive(), menu_index);
  if (!FzeroTracksActive() || !active_cup)
    return input;
  if (!cup_menu(ram)) {
    menu_last_input = input;
    menu_hold = 0;
    return input;
  }
  /* A single vertical list, including every enabled pack. Horizontal input
   * belongs to the native menu and must not page through the cup catalog. */
  uint16_t direction = input & 0x34;
  bool step = direction &&
              (direction != (menu_last_input & 0x34) || (++menu_hold >= 24 && menu_hold % 6 == 0));
  if (direction != (menu_last_input & 0x34))
    menu_hold = 0;
  if (step) {
    unsigned count = FzeroTracksRuntimeCount();
    bool back = (direction & 16) != 0;
    FzeroTracksRuntimeSelect((menu_index + count + (back ? -1 : 1)) % count);
    fprintf(stderr, "[track-library] menu %u/%u: %s\n", menu_index + 1, count, active_cup->name);
  }
  menu_last_input = input;
  unsigned slot = imported_pack(active_pack) ? 0 : active_cup->slot;
  g_ram[FzeroDeluxeActive() ? 0x90 : 0x5a] = (uint8_t)slot;
  if (ram[0x58]) g_ram[0x53] = 0;
  return input & ~0x34;
}
unsigned FzeroTracksMenuIndex(void) {
  return menu_index;
}
bool FzeroTracksMenuVisible(void) {
  return FzeroTracksActive() && g_ram[0x54] == 1 &&
         (FzeroDeluxeActive() ? (g_ram[0x55] == 1 || (!g_ram[0x58] && g_ram[0x55] == 7)) &&
                                  g_ram[0x56] == (g_ram[0x58] ? 4 : 2)
                              : !g_ram[0x58] && g_ram[0x55] >= 5 && g_ram[0x55] <= 7);
}
bool FzeroTracksClassSelected(void) {
  return FzeroDeluxeActive() ? g_ram[0x14c98] != 0 : g_ram[0x55] >= 6;
}
void FzeroTracksRefreshCourse(void) {
  current_course();
  if (FzeroRecordsViewState()) return;
  uint8_t digest[32];
  const uint8_t *key = NULL;
  if (g_ram[0x54] && FzeroTracksRecordKey(menu_index, FzeroVehicleIdentity(), digest))
    key = digest;
  if (FzeroTracksActive()) {
    if (!FzeroTracksRecordsSelect(key)) course = NULL;
    else if (key) {
      uint8_t previous[0x400];
      if (previous_practice(menu_index, FzeroVehicleIdentity(), previous)) {
        FzeroRecordsMergeCup(g_sram, previous, active_cup->slot);
        memcpy(g_ram + 0x14800, g_sram, 0x200);
      }
    }
  }
}
