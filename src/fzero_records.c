#include "fzero_records.h"
#include "fzero_course_runtime.h"
#include "fzero_tracks.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay.h"
#include "fzero_vehicles.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <string.h>
extern Snes *g_snes;

static bool scene(void) {
  return !g_ram[0x54] && g_ram[0x55] >= 2 && g_ram[0x55] <= (FzeroDeluxeActive() ? 3 : 5);
}
bool FzeroRecordsDetail(void) {
  return FzeroRecordsViewState() && scene() &&
      (FzeroDeluxeActive() ? g_ram[0x55] == 3 && g_ram[0x56] >= 2 && g_ram[0x56] <= 4 : g_ram[0x55] >= 4);
}
unsigned FzeroRecordsCup(void) {
  FzeroRecordsView *v = FzeroRecordsViewState();
  return v ? v->page * 3 + v->selected / 5 : 0;
}
unsigned FzeroRecordsOrder(void) {
  FzeroRecordsView *v = FzeroRecordsViewState();
  return v ? v->selected % 5 : 0;
}
static bool valid(unsigned slot) {
  FzeroRecordsView *v = FzeroRecordsViewState();
  return v && slot < 15 && FzeroTracksRuntimeTrack(v->page * 3 + slot / 5, slot % 5);
}
static unsigned record_offset(unsigned slot) {
  static const unsigned starts[] = {5, 0xac, 0x153, 0x205, 0x2ac};
  return slot < 25 ? starts[slot / 5] + slot % 5 * 33 : 0x353 + (slot - 25) * 33;
}
static unsigned time_value(const uint8_t *time) {
  return ((unsigned)(time[0] & 15) << 16) | ((unsigned)time[1] << 8) | time[2];
}
static void merge_rows(uint8_t *dest, uint8_t owners[11], const uint8_t *source, unsigned vehicle) {
  for (unsigned row = 0; row < 10; ++row) {
    const uint8_t *candidate = source + row * 3;
    if ((candidate[0] & 15) >= 9) continue;
    bool duplicate = false;
    for (unsigned i = 0; i < 10; ++i)
      if (owners[i] == vehicle && !memcmp(dest + i * 3, candidate, 3)) duplicate = true;
    if (duplicate) continue;
    for (unsigned i = 0; i < 10; ++i) {
      if (time_value(candidate) < time_value(dest + i * 3)) {
        memmove(dest + (i + 1) * 3, dest + i * 3, (9 - i) * 3);
        memmove(owners + i + 1, owners + i, 9 - i);
        memcpy(dest + i * 3, candidate, 3);
        owners[i] = (uint8_t)vehicle;
        break;
      }
    }
  }
  if (time_value(source + 30) < time_value(dest + 30)) {
    memcpy(dest + 30, source + 30, 3);
    owners[10] = (uint8_t)vehicle;
  }
}
static void page_records(void) {
  FzeroRecordsView *v = FzeroRecordsViewState();
  uint8_t data[0x400];
  for (unsigned column = 0; column < 3; ++column) {
    unsigned index = v->page * 3 + column;
    const CpPack *p = NULL;
    const CpCup *cup = FzeroTracksRuntimeCup(index, &p);
    FzeroTracksReadRecords(index, FzeroVehicleRecordIdentity(v->vehicle), data);
    uint8_t all[12][0x400];
    if (v->vehicle == FzeroVehicleCount())
      for (unsigned car = 0; car < FzeroVehicleCount(); ++car)
        FzeroTracksReadRecords(index, FzeroVehicleRecordIdentity(car), all[car]);
    for (unsigned row = 0; row < 5; ++row) {
      uint8_t *dest = g_sram + record_offset(column * 5 + row);
      uint8_t *owners = FzeroRecordsRowVehicles(column * 5 + row);
      memset(owners, v->vehicle < FzeroVehicleCount() ? v->vehicle : 255, 11);
      const CpTrack *track = FzeroTracksRuntimeTrack(index, row);
      if (track) {
        bool native = strcmp(p->adapter, "fzero-course-v1") != 0;
        unsigned source = native ? cup->slot * 5 + row : row;
        memcpy(dest, data + record_offset(source), 33);
        if (v->vehicle == FzeroVehicleCount()) {
          /* The unkeyed legacy records remain visible alongside every
           * enabled car's private GP/Practice records. This is a read-only
           * merge; its per-row identities live in the browser snapshot. */
          for (unsigned car = 0; car < FzeroVehicleCount(); ++car) {
            merge_rows(dest, owners, all[car] + record_offset(source), car);
          }
        }
      } else {
        for (unsigned i = 0; i < 11; ++i) memcpy(dest + i * 3, "\x09\x59\x99", 3);
      }
    }
    unsigned start = record_offset(column * 5), sum = 0;
    for (unsigned i = 0; i < 165; ++i) sum += g_sram[start + i];
    g_sram[start + 165] = (uint8_t)sum;
    g_sram[start + 166] = (uint8_t)(sum >> 8);
  }
  memcpy(g_ram + 0x14800, g_sram, 0x200);
  if (!valid(v->selected)) {
    v->selected = 0;
    while (v->selected < 15 && !valid(v->selected)) ++v->selected;
  }
  fprintf(stderr, "[records-browser] page=%u vehicle=%s selected=%u\n",
      v->page, FzeroVehicleRecordName(v->vehicle), v->selected);
}
void FzeroRecordsTick(void) {
  FzeroRecordsView *v = FzeroRecordsViewState();
  if (!scene()) {
    if (v) FzeroRecordsViewEnd();
    return;
  }
  if (!FzeroTracksActive() && !FzeroGameplaySettingsCurrent()->always_records) return;
  if (!v) {
    if (!FzeroRecordsViewBegin()) return;
    v = FzeroRecordsViewState();
    v->previous_cup = (uint16_t)FzeroTracksMenuIndex();
    v->page = v->previous_cup / 3;
    v->selected = (v->previous_cup % 3) * 5;
    v->vehicle = (uint8_t)FzeroVehicleCount();
    page_records();
  }
  /* Input selects the next course before the native fade/DMA completes.
   * Keep presentation on the loaded page until the next one is ready. This
   * lives in the records snapshot so a mid-transition load behaves the same. */
  bool detail_ready = FzeroDeluxeActive() ? g_ram[0x55] == 3 && g_ram[0x56] == 4
                                         : g_ram[0x55] == 5;
  bool overview_ready = g_ram[0x55] == 3 && (!FzeroDeluxeActive() || !g_ram[0x56]);
  if (detail_ready || overview_ready) {
    if (v->display_selected != v->selected || v->display_detail != detail_ready)
      v->label_tick = 0;
    v->display_selected = v->selected;
    v->display_detail = detail_ready;
  }
}
static bool overview(void) {
  return scene() && g_ram[0x55] == 3 && (!FzeroDeluxeActive() || !g_ram[0x56]);
}
static void select_native(void) {
  FzeroRecordsView *v = FzeroRecordsViewState();
  if (!v) return;
  if (FzeroDeluxeActive()) {
    g_ram[0x53] = v->selected < 15 ? v->selected : 255;
    g_ram[0x14c88] = g_ram[0x14c89] = 0;
  } else g_ram[0xf2] = v->selected;
}
uint16_t FzeroRecordsInput(uint16_t input) {
  FzeroRecordsView *v = FzeroRecordsViewState();
  if (!v) return input;
  unsigned edge = input & ~v->input;
  unsigned direction = input & 0xf0;
  if (direction && direction == (v->input & 0xf0) && ++v->hold >= 24 && v->hold % 6 == 0)
    edge |= direction;
  if (direction != (v->input & 0xf0)) v->hold = 0;
  bool detail_ready = FzeroRecordsDetail() &&
      (FzeroDeluxeActive() ? g_ram[0x56] == 4 : g_ram[0x55] == 5);
  if (overview()) {
    if (edge) v->label_tick = 0;
    else ++v->label_tick;
  } else if (detail_ready && !(edge & 0xf0)) {
    ++v->label_tick;
  }
  v->input = input;
  if (overview()) {
    if (edge & (1024 | 2048 | 512 | 2)) {
      unsigned pages = (FzeroTracksRuntimeCount() + 2) / 3, cars = FzeroVehicleCount() + 1;
      if (edge & 1024) v->page = (v->page + pages - 1) % pages;
      if (edge & 2048) v->page = (v->page + 1) % pages;
      if (edge & 2) v->vehicle = (v->vehicle + cars - 1) % cars;
      if (edge & 512) v->vehicle = (v->vehicle + 1) % cars;
      page_records();
      /* Only the records and labels change. Keep the native frame in place
       * instead of replaying its fade/music setup on every page or car. */
    } else if (edge & 0xf0) {
      unsigned candidate = v->selected;
      if (edge & (64 | 128)) {
        candidate = candidate < 10 ? (candidate % 5 + 10) : (candidate == 15 ? 5 : candidate % 5);
        if (valid(candidate)) v->selected = (uint8_t)candidate;
        else v->selected = 15;
      } else {
        do { candidate = (candidate + ((edge & 16) ? 15 : 1)) % 16; }
        while (candidate != 15 && !valid(candidate));
        v->selected = (uint8_t)candidate;
      }
    }
    if (edge & 1) { v->selected = 15; input = 8; }
    select_native();
    return input & (8 | 256);
  }
  if (FzeroRecordsDetail()) {
    if (detail_ready && (edge & 0xf0)) {
      unsigned candidate = v->selected;
      do { candidate = (candidate + ((edge & (16 | 64)) ? 14 : 1)) % 15; }
      while (!valid(candidate));
      v->selected = (uint8_t)candidate;
      select_native();
      if (FzeroDeluxeActive()) g_ram[0x56] = 3;
      else {
        /* Match native $038471: fade before state 4 replaces graphics. */
        g_ram[0x60] = 2;
        g_ram[0x55] = 4;
      }
    }
    /* Deletion operates on native slots. This browser is a read-only view;
     * don't expose a delete command which could target a different context. */
    return (input & (8 | 256)) | ((edge & 1) ? 8 : 0);
  }
  return 0;
}
static void record_hook(CpuState *cpu, uint32_t pc) {
  if (pc == 0x0380fe || pc == 0x038151) {
    /* FZEdit's F-Zero_Final.asm controls title visibility/selectability at
     * these two branches. Take the available-record path without inventing
     * a record or changing SRAM. Both native engines retain these sites. */
    if (FzeroGameplaySettingsCurrent()->always_records)
      interp_bridge_pre_opcode_redirect(pc == 0x0380fe ? 0x03810a : 0x038154);
    return;
  }
  FzeroRecordsTick();
  FzeroRecordsView *v = FzeroRecordsViewState();
  if (!v) {
    /* Same predicate as the gameplay hook this site replaces. */
    if (pc == 0x1eb805 && !FzeroBsTracks() && (cpu->A & 255) >= 15 && (cpu->A & 255) < 25) {
      cpu->_flag_Z = 1;
      interp_bridge_pre_opcode_redirect(0x1eb80a);
    }
    return;
  }
  switch (pc) {
  case 0x03822e: case 0x1eef87:
    select_native();
    /* Native completed-course indices refer to the raced context, not the
     * assembled three-cup page. Don't display them beneath another cup. */
    if (FzeroDeluxeActive()) memset(g_ram + 0x14cf0, 255, 10);
    break;
  case 0x1eefe7:
    cpu->A = v->selected < 15 ? v->selected : 0xffff;
    interp_bridge_pre_opcode_redirect(0x1eefea);
    break;
  case 0x038271: case 0x1ef024:
    select_native();
    break;
  case 0x1eb805:
    if (overview() || g_ram[0x55] == 2) {
      cpu->_flag_Z = !valid(cpu->A & 255);
      interp_bridge_pre_opcode_redirect(0x1eb80a);
    }
    break;
  case 0x1eeb1a: {
    /* Keep the native detail layout/renderer, but resolve resources from the
     * catalog. Its record slot stays virtual and never becomes a race slot. */
    const CpPack *p;
    const CpCup *cup = FzeroTracksRuntimeCup(FzeroRecordsCup(), &p);
    unsigned native = cup && strcmp(p->adapter, "fzero-course-v1") ? cup->slot * 5 + FzeroRecordsOrder() : 0;
    FzeroTracksRefreshCourse();
    const FzeroCourse *c = FzeroTracksCurrentCourse();
    if (c) {
      /* Keep the native detail-screen setup and sprite reservations. The
       * host overlay replaces its stock illustration with course scenery;
       * decoded course data also supplies the minimap and palette below. */
      for (unsigned i = 0; i < 15; ++i)
        if ((g_snes->cart->rom[0x16129 + i] & 15) == (c->setting & 15)) { native = i; break; }
      for (unsigned i = 0; i < 15; ++i)
        if (g_snes->cart->rom[0x16129 + i] == c->setting) { native = i; break; }
    }
    memcpy(g_ram + 0x14c00, g_snes->cart->rom + 0xf2000 + native * 64, 64);
    g_ram[0x14c25] = g_ram[0x14c27] = g_ram[0x14c28] = v->selected;
    g_ram[0x14c26] = 255; g_ram[0x14c29] = 0;
    FzeroTracksRefreshCourse();
    const CpTrack *track = FzeroTracksRuntimeTrack(FzeroRecordsCup(), FzeroRecordsOrder());
    fprintf(stderr, "[records-browser] detail %s/%s course=%s slot=%u\n", p->id, cup->id, track->id, v->selected);
    break;
  }
  case 0x1eec0b: {
    FzeroTracksRefreshCourse();
    const FzeroCourse *c = FzeroTracksCurrentCourse();
    if (c) {
      for (unsigned row = 0; row < 32; ++row) {
        memcpy(g_ram + 0x103b0 + row * 32, c->minimap + row * 16, 16);
        memset(g_ram + 0x103c0 + row * 32, 0, 16);
      }
      interp_bridge_pre_opcode_redirect(0x1eec45);
    }
    break;
  }
  case 0x1ee826: {
    const FzeroCourse *c = FzeroTracksCurrentCourse();
    if (c) memcpy(g_ram + 0x5c0, c->palette + 0xa0, 0x40);
    break;
  }
  }
}
void FzeroRecordsInstallHooks(void) {
  if (FzeroGameplaySettingsCurrent()->always_records) {
    interp_bridge_set_pre_opcode_hook(0x0380fe, record_hook);
    interp_bridge_set_pre_opcode_hook(0x038151, record_hook);
  }
  if (!FzeroTracksActive() && !FzeroGameplaySettingsCurrent()->always_records) return;
  const uint32_t stock[] = {0x03822e, 0x038271};
  const uint32_t deluxe[] = {0x1eef87, 0x1eefe7, 0x1ef024, 0x1eb805, 0x1eeb1a, 0x1eec0b, 0x1ee826};
  const uint32_t *sites = FzeroDeluxeActive() ? deluxe : stock;
  unsigned count = FzeroDeluxeActive() ? sizeof(deluxe)/sizeof(*deluxe) : sizeof(stock)/sizeof(*stock);
  for (unsigned i = 0; i < count; ++i) interp_bridge_set_pre_opcode_hook(sites[i], record_hook);
}
