#include "fzero_vehicles.h"
#include "common_rtl.h"
#include "content_pack.h"
#include "cpu_state.h"
#include "fzero_gameplay.h"
#include "sha256.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern Snes *g_snes;

/* Stable identities are independent of the four guest racing slots. Grand Prix
 * uses the selected ship's donor cohort, retaining three native main rivals.
 * Practice composes its independently selected rival into a spare slot.
 * All enabled identities remain in one selector. Only curated vehicle data
 * and independently assembled mechanics enter the canonical cartridge. */
typedef struct Vehicle {
  const char *id, *name;
  unsigned group, slot;
} Vehicle;
static const Vehicle vehicles[] = {{"blue-falcon", "BLUE FALCON", 0, 0},
                                   {"wild-goose", "WILD GOOSE", 0, 1},
                                   {"golden-fox", "GOLDEN FOX", 0, 2},
                                   {"fire-stingray", "FIRE STINGRAY", 0, 3},
                                   {"moon-shadow", "MOON SHADOW", 1, 0},
                                   {"dragon-bird", "DRAGON BIRD", 1, 1},
                                   {"great-star", "GREAT STAR", 1, 2},
                                   {"death-anchor", "DEATH ANCHOR", 1, 3},
                                   {"p-emerald", "P. EMERALD", 2, 1},
                                   {"black-bull", "BLACK BULL", 2, 3},
                                   {"white-cat", "WHITE CAT", 3, 0},
                                   {"red-gazelle", "RED GAZELLE", 3, 2}};
static const unsigned cohort[4][4] = {
    {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 8, 2, 9}, {10, 1, 11, 3}};
static const unsigned menu_rows[] = {0, 2, 1, 3};
static uint8_t *stock_image, *art[3], *images[4];
static FzeroGameplaySettings choices, image_settings[4];
static unsigned roster[12], count, selected_image = 99;
static void menu_resources(bool upload);
static void rival_resources(bool upload);
static void rival_compose(unsigned id, unsigned slot);
static unsigned selected_rival = 99;
static unsigned selected_rival_slot = 99;
static uint8_t race_acceleration[4][4][29], race_turn[4][4][30];
enum { IMAGE_SIZE = 0x400000, ID_ADDRESS = 0x14dff,
       INFO_BASE = 0x80000, INFO_CARD_SIZE = 0x580,
       INFO_ENTRY_SIZE = INFO_CARD_SIZE + 6,
       /* Reserved gap after the ghost codec ($14CA0..$14CE5) and before
        * record times ($14CF0). The car-record tail is copied on load. */
       RIVAL_ID = 0x14ce8, RIVAL_FIRST = 0x14ce9,
       RIVAL_VERSION = 0x14cea, RIVAL_SLOT = 0x14ceb,
       RIVAL_ART = 0x350000, RIVAL_PALETTES = 0x358000 };

bool FzeroVehiclesActive(void) {
  const FzeroGameplaySettings *s = FzeroGameplaySettingsCurrent();
  return s->vehicle_packs || s->stock_rebalance;
}
unsigned FzeroVehicleCount(void) { return count; }
static bool enabled(unsigned id) {
  return id < 4 || (id < 12 &&
                    (choices.vehicle_packs & (1u << (vehicles[id].group - 1))));
}
unsigned FzeroVehicleSelected(void) {
  unsigned id = g_ram[ID_ADDRESS];
  return id < 12 && enabled(id) ? id : 0;
}
const char *FzeroVehicleIdentity(void) {
  return count ? vehicles[FzeroVehicleSelected()].id : NULL;
}
const char *FzeroVehicleRecordIdentity(unsigned index) {
  return index < count ? vehicles[roster[index]].id : NULL;
}
const char *FzeroVehicleRecordName(unsigned index) {
  return index < count ? vehicles[roster[index]].name : "SHARED RECORDS";
}
static unsigned group_for(unsigned id) {
  if (id >= 4)
    return vehicles[id].group;
  return (choices.stock_rebalance & (1u << id)) ? (id == 0 || id == 2 ? 2 : 3)
                                                : 0;
}
bool FzeroVehicleRecordIcon(unsigned index, uint32_t pixels[16 * 16]) {
  if (index >= count || !pixels) return false;
  unsigned id = roster[index], group = group_for(id), slot = vehicles[id].slot;
  const uint8_t *source = group ? art[group - 1] : stock_image;
  const uint8_t *graphic = source + 0x40000 + slot * 0x8000 + 0x6aa0;
  const uint8_t *palette = source + 0x7cd80 + slot * 32;
  for (unsigned y = 0; y < 16; ++y)
    for (unsigned x = 0; x < 16; ++x) {
      unsigned at = (y / 8 * 2 + x / 8) * 32 + y % 8 * 2, color = 0;
      for (unsigned bit = 0; bit < 4; ++bit)
        color |= ((graphic[at + bit / 2 * 16 + bit % 2] >> (7 - x % 8)) & 1) << bit;
      unsigned rgb = palette[color * 2] | (unsigned)palette[color * 2 + 1] << 8;
      pixels[y * 16 + x] = color ? 0xff000000u | ((rgb & 31) * 255 / 31 << 16) |
          (((rgb >> 5) & 31) * 255 / 31 << 8) | (((rgb >> 10) & 31) * 255 / 31) : 0;
    }
  return true;
}
static unsigned gp_group_for(unsigned id) {
  /* Roster membership is independent of the player's handling choice.
   * Retail members join P2/P3 whenever that additions pack is enabled. */
  unsigned group = id >= 4 ? vehicles[id].group : (id == 0 || id == 2 ? 2 : 3);
  return choices.vehicle_packs & (1u << (group - 1)) ? group : group_for(id);
}
static bool needed(unsigned group) {
  return (choices.vehicle_packs & (1u << (group - 1))) ||
         (group == 2 && (choices.stock_rebalance & 5)) ||
         (group == 3 && (choices.stock_rebalance & 10));
}
static bool manifest(unsigned group, CpPack *pack, char *error, size_t cap) {
  char path[96], line[256];
  snprintf(path, sizeof(path), "assets/vehicle-packs/cgp-p%u.ini", group);
  FILE *f = fopen(path, "rb");
  if (!f) {
    snprintf(error, cap, "Missing CGP P%u vehicle manifest", group);
    return false;
  }
  memset(pack, 0, sizeof(*pack));
  unsigned seen = 0, slots = 0;
  bool ok = true;
  while (ok && fgets(line, sizeof(line), f)) {
    size_t n = strlen(line);
    if (n == sizeof(line) - 1 && line[n - 1] != '\n') {
      ok = false;
      break;
    }
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
      line[--n] = 0;
    if (!n || line[0] == '#')
      continue;
    char *v = strchr(line, '=');
    if (!v) {
      ok = false;
      break;
    }
    *v++ = 0;
    unsigned bit = !strcmp(line, "format")          ? 1
                   : !strcmp(line, "id")            ? 2
                   : !strcmp(line, "profile")       ? 4
                   : !strcmp(line, "source_sha256") ? 8
                   : !strcmp(line, "target_sha256") ? 16
                                                    : 0;
    if (bit) {
      if (seen & bit) {
        ok = false;
        break;
      }
      seen |= bit;
      char expected[24];
      snprintf(expected, sizeof(expected), "cgp-p%u", group);
      if (bit == 1)
        ok = !strcmp(v, "fzero-vehicles-2");
      if (bit == 2)
        ok = !strcmp(v, expected);
      if (bit == 4)
        ok = strlen(v) == 1 && v[0] == '0' + (int)group;
      if (bit == 8)
        ok = cp_hash_parse(v, pack->source_hash) != 0;
      if (bit == 16)
        ok = cp_hash_parse(v, pack->target_hash) != 0;
    } else if (!strcmp(line, "vehicle")) {
      char *name = strchr(v, '|'), *slot = name ? strchr(name + 1, '|') : NULL;
      char *role = slot ? strchr(slot + 1, '|') : NULL;
      if (!name || !slot || !role) {
        ok = false;
        break;
      }
      *name++ = *slot++ = *role++ = 0;
      if (strlen(slot) != 1 || *slot < '0' || *slot > '3') {
        ok = false;
        break;
      }
      unsigned index = (unsigned)(*slot - '0'), id = cohort[group][index];
      if (slots & (1u << index)) {
        ok = false;
        break;
      }
      slots |= 1u << index;
      for (char *p = name; *p; ++p)
        *p = (char)toupper((unsigned char)*p);
      ok = !strcmp(v, vehicles[id].id) && !strcmp(name, vehicles[id].name) &&
           !strcmp(role, id < 4 ? "rebalance" : "new");
    } else
      ok = false;
  }
  if (ferror(f))
    ok = false;
  fclose(f);
  if (!ok || seen != 31 || slots != 15) {
    snprintf(error, cap, "Invalid CGP P%u vehicle manifest", group);
    return false;
  }
  return true;
}
bool FzeroVehiclesLoad(const uint8_t *stock, size_t size, char *error,
                       size_t cap) {
  free(stock_image);
  stock_image = NULL;
  for (unsigned i = 0; i < 3; ++i) {
    free(art[i]);
    art[i] = NULL;
  }
  for (unsigned i = 0; i < 4; ++i) {
    free(images[i]);
    images[i] = NULL;
  }
  count = 0;
  selected_image = 99;
  choices = *FzeroGameplaySettingsCurrent();
  if (!FzeroVehiclesActive())
    return true;
  if (size != 0x80000 || choices.vehicle_packs > 7 ||
      choices.stock_rebalance > 15) {
    snprintf(error, cap, "Invalid vehicle catalog input");
    return false;
  }
  stock_image = malloc(size);
  if (!stock_image) {
    snprintf(error, cap, "Cannot allocate vehicle source");
    return false;
  }
  memcpy(stock_image, stock, size);
  for (unsigned group = 1; group <= 3; ++group) {
    if (!needed(group))
      continue;
    CpPack pack;
    char path[96];
    size_t length = 0;
    snprintf(path, sizeof(path), "assets/vehicle-packs/cgp-p%u.ips", group);
    if (!manifest(group, &pack, error, cap) ||
        !cp_pack_apply(&pack, stock, size, path, &art[group - 1], &length,
                       error, cap))
      return false;
    if (length != size + 8 + 4 * INFO_ENTRY_SIZE ||
        memcmp(art[group - 1] + size, "FZCARD1\0", 8)) {
      snprintf(error, cap, "Invalid vehicle artwork/card payload");
      return false;
    }
    if (memcmp(art[group - 1] + 0x5ec00, stock + 0x5ec00, 0x400) ||
        memcmp(art[group - 1] + 0x46f80, stock + 0x46f80, 0x80)) {
      snprintf(error, cap, "Vehicle artwork changes shared HUD/fog resources");
      return false;
    }
  }
  /* Each enabled author's four-car group owns a complete native column.
   * Stock identities remain single entries; their optional rebalances affect
   * presentation/handling, never which column they belong to. Original cars
   * whose CGP group is disabled follow in a separate, possibly partial column. */
  unsigned placed = 0;
  for (unsigned group = 1; group <= 3; ++group) {
    if (!(choices.vehicle_packs & (1u << (group - 1))))
      continue;
    for (unsigned row = 0; row < 4; ++row) {
      unsigned id = cohort[group][menu_rows[row]];
      roster[count++] = id;
      placed |= 1u << id;
    }
  }
  for (unsigned row = 0; row < 4; ++row) {
    unsigned id = menu_rows[row];
    if (!(placed & (1u << id)))
      roster[count++] = id;
  }
  return true;
}
static void copy_changes(uint8_t *out, const uint8_t *source, unsigned start,
                         unsigned size) {
  for (unsigned i = start; i < start + size; ++i)
    if (source[i] != stock_image[i])
      out[i] = source[i];
}
static void copy_stats(uint8_t *out, const uint8_t *source, unsigned slot) {
  static const unsigned fields[][2] = {
      {0xfa81, 1}, {0xfa85, 1}, {0xfa89, 1}, {0xfa8d, 1}, {0xfa91, 2},
      {0xfa99, 2}, {0xfaa1, 2}, {0xfaa9, 1}, {0xfaad, 1}, {0xfab1, 1},
      {0xfab5, 1}, {0xfab9, 1}, {0xfabd, 1}, {0xfac1, 1}, {0xfac5, 2},
      {0xfacd, 2}, {0xfad5, 2}, {0xfadd, 2}, {0xfae5, 2}, {0xfaed, 1}};
  unsigned field = 0x47;
  for (unsigned i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
    unsigned off = fields[i][0] - 0x8000 + slot * fields[i][1];
    memcpy(out + off, source + off, fields[i][1]);
    memcpy(out + 0xf0000 + slot * 256 + field, source + off, fields[i][1]);
    field += fields[i][1];
  }
}
bool FzeroVehiclesPrepare(uint8_t **rom, size_t *size, char *error,
                          size_t cap) {
  if (!count) {
    bool ok = FzeroGameplayPrepare(rom, size);
    if (!ok)
      snprintf(error, cap, "%s", FzeroGameplayError());
    return ok;
  }
  size_t base_size = *size;
  uint8_t *base = malloc(base_size);
  if (!base) {
    snprintf(error, cap, "Cannot allocate vehicle adapter");
    return false;
  }
  memcpy(base, *rom, base_size);
  /* The new catalog's original identities come from retail, independently
   * of incidental differences in the Deluxe metadata conversion. The legacy
   * BS mode takes the separate no-catalog path and retains its baseline. */
  for (unsigned slot = 0; slot < 4; ++slot) {
    copy_stats(base, stock_image, slot);
    memcpy(base + 0xf0063 + slot * 256, stock_image + 0x149ab + slot * 19, 19);
    memcpy(base + 0xf0076 + slot * 256, stock_image + 0x14a37 + slot * 19, 19);
  }
  uint8_t identity[4 * 32 + 8] = {3, 0, 0, 0};
  identity[4] = (uint8_t)choices.vehicle_packs;
  identity[5] = (uint8_t)choices.stock_rebalance;
  bool tracks = FzeroBsTracks(), ok = true;
  for (unsigned group = 0; group < 4 && ok; ++group) {
    if (group && !needed(group))
      continue;
    image_settings[group] = choices;
    image_settings[group].enabled &= ~7u;
    if (group) {
      image_settings[group].enabled |= 7;
      image_settings[group].tuning = image_settings[group].boost =
          image_settings[group].exhaust = group - 1;
    }
    size_t length = IMAGE_SIZE;
    images[group] = calloc(1, length);
    if (!images[group]) {
      ok = false;
      break;
    }
    memcpy(images[group], base, base_size);
    FzeroGameplayConfigure(&image_settings[group], false, tracks);
    if (group) {
      for (unsigned slot = 0; slot < 4; ++slot) {
        const uint8_t *donor = art[group - 1];
        copy_changes(images[group], donor, 0x40000 + slot * 0x8000, 0x8000);
        copy_changes(images[group], donor, 0x76200 + slot * 0x180, 0x180);
        copy_changes(images[group], donor, 0x7cd80 + slot * 32, 32);
      }
    }
    if (group) {
      /* CGP race images include their energy-boost HUD artwork. The stock
       * image and the separate stock BS path keep the original POWER HUD. */
      memcpy(images[group] + 0x640d0, art[group - 1] + 0x640d0, 0x30);
      memcpy(images[group] + 0x641b0, art[group - 1] + 0x641b0, 0x80);
    }
    ok = FzeroGameplayPrepare(&images[group], &length);
    if (!ok)
      break;
  }
  /* Compose each racing slot by identity, including independently selected
   * retail rebalances. Disabled packs cannot supply hidden CPU opponents. */
  uint8_t *raw[4];
  memcpy(raw, images, sizeof(raw));
  memset(images, 0, sizeof(images));
  for (unsigned group = 0; group < 4 && ok; ++group) {
    if (!raw[group])
      continue;
    images[group] = malloc(IMAGE_SIZE);
    if (!images[group]) {
      ok = false;
      break;
    }
    memcpy(images[group], raw[group], IMAGE_SIZE);
    unsigned common = 0;
    for (unsigned slot = 0; slot < 4; ++slot) {
      unsigned id = cohort[group][slot];
      if (!enabled(id))
        id = slot;
      unsigned source = group_for(id);
      if (source)
        common = source;
      uint8_t *out = images[group];
      /* Undo only the original cohort's owned artwork delta, preserving the
       * independently assembled HUD and common mechanics in this image. */
      unsigned starts[] = {0x40000 + slot * 0x8000, 0x76200 + slot * 0x180,
                           0x7cd80 + slot * 32};
      unsigned lengths[] = {0x8000, 0x180, 32};
      for (unsigned region = 0; region < 3; ++region) {
        unsigned start = starts[region], length = lengths[region];
        if (group)
          for (unsigned i = start; i < start + length; ++i)
            if (art[group - 1][i] != stock_image[i])
              out[i] = base[i];
        if (source)
          copy_changes(out, art[source - 1], start, length);
      }
      copy_stats(out, raw[source], slot);
      memcpy(out + 0xf0000 + slot * 256, raw[source] + 0xf0000 + slot * 256,
             256);
      const uint8_t *record = raw[source] + 0xf0000 + slot * 256;
      if (source) {
        memcpy(race_turn[group][slot], raw[source] + 0x149ab + slot * 30, 30);
        memcpy(race_acceleration[group][slot],
               raw[source] + 0x14a42 + slot * 29, 29);
      } else {
        memcpy(race_turn[group][slot], record + 0x63, 19);
        memcpy(race_turn[group][slot] + 19, base + 0x149f7 + 19, 11);
        memcpy(race_acceleration[group][slot], record + 0x76, 19);
        memset(race_acceleration[group][slot] + 19, 0, 10);
      }
      if (group) {
        memcpy(out + 0x149ab + slot * 30, race_turn[group][slot], 30);
        memcpy(out + 0x14a42 + slot * 29, race_acceleration[group][slot], 29);
      }
      out[0xf00ff + slot * 256] = (uint8_t)id;
      /* Native name labels are fixed 16-byte, zero-terminated fields. */
      unsigned row = slot == 1 ? 2 : slot == 2 ? 1 : slot;
      char *label = (char *)images[group] + 0xf5b20 + row * 16;
      memset(label, ' ', 15);
      label[15] = 0;
      size_t n = strlen(vehicles[id].name);
      memcpy(label + (15 - n) / 2, vehicles[id].name, n);
      for (unsigned j = 0; j < 15; ++j)
        if (label[j] == '.')
          label[j] = '_';
    }
    if (common) {
      memcpy(images[group] + 0x14ad6, raw[common] + 0x14ad6, 29);
      for (unsigned slot = 0; slot < 4; ++slot)
        images[group][0xf009b + slot * 256] = 0x94;
    }
    sha256_compute(images[group], IMAGE_SIZE, identity + 8 + group * 32);
  }
  for (unsigned i = 0; i < 4; ++i)
    free(raw[i]);
  free(base);
  FzeroGameplayConfigure(&image_settings[0], false, tracks);
  if (!ok) {
    snprintf(error, cap, "Cannot prepare CGP vehicle runtime: %.120s",
             FzeroGameplayError());
    return false;
  }
  uint8_t *grown = realloc(*rom, IMAGE_SIZE);
  if (!grown) {
    snprintf(error, cap, "Cannot allocate vehicle cartridge");
    return false;
  }
  *rom = grown;
  *size = IMAGE_SIZE;
  memcpy(grown, images[0], IMAGE_SIZE);
  uint8_t hash[32];
  sha256_compute(identity, sizeof(identity), hash);
  FzeroGameplaySetSignature(hash);
  static const DispatchEntry empty[1] = {{0}};
  cpu_select_program(empty, 0, NULL, 0);
  interp_bridge_set_scheduler_aot_policy(0);
  fprintf(stderr, "[vehicles] %u identities; CGP packs=%u rebalances=%u\n",
          count, choices.vehicle_packs, choices.stock_rebalance);
  return true;
}
unsigned FzeroVehicleAcceleration(unsigned slot, unsigned speed) {
  if (g_ram[0x58] && selected_rival < 12 &&
      slot == selected_rival_slot)
    return race_acceleration[group_for(selected_rival)][vehicles[selected_rival].slot]
                            [speed < 29 ? speed : 28];
  unsigned group = selected_image < 4 ? selected_image : 0;
  return race_acceleration[group][slot & 3][speed < 29 ? speed : 28];
}
unsigned FzeroVehicleTurn(unsigned speed) {
  unsigned group = selected_image < 4 ? selected_image : 0;
  return race_turn[group][vehicles[FzeroVehicleSelected()].slot]
                  [speed < 30 ? speed : 29];
}
void FzeroVehiclesSync(void) {
  if (!count || !g_snes || !g_snes->cart || g_snes->cart->romSize != IMAGE_SIZE)
    return;
  unsigned id = FzeroVehicleSelected();
  /* The native recorded title demo uses Blue Thunder's BS slot and its own
   * replay. A previously selected CGP car must not lend its cohort's shared
   * HUD, palettes or mechanics to that replay. Leave the stored selection
   * untouched; switch only the active presentation/handling image. */
  bool demo = g_ram[0x5b] != 0;
  unsigned group = demo ? 0 : g_ram[0x58] ? group_for(id) : gp_group_for(id);
  /* No Rival clears the native rival workspace on race entry. Preserve its
   * sentinel instead of interpreting that cleared byte as Blue Falcon. */
  if (g_ram[0x58] && g_ram[0x54] >= 2 && g_ram[0xcf2] >= 0xfe) {
    g_ram[RIVAL_ID] = g_ram[0xcf2];
    g_ram[RIVAL_VERSION] = 1;
  }
  unsigned rival = !demo && g_ram[0x58] ? g_ram[RIVAL_ID] : 99;
  if (rival >= 12 || !enabled(rival)) rival = 99;
  if (g_ram[0x54] < 2)
    g_ram[RIVAL_SLOT] = (uint8_t)((vehicles[id].slot + 1) % 4);
  unsigned rival_slot = g_ram[RIVAL_SLOT] & 3;
  if (group == selected_image && rival == selected_rival &&
      (rival >= 12 || rival_slot == selected_rival_slot))
    return;
  memcpy(g_snes->cart->rom, images[group], IMAGE_SIZE);
  FzeroGameplayActivateVehicles(&image_settings[group], g_snes->cart->rom);
  selected_image = group;
  selected_rival = rival;
  selected_rival_slot = rival_slot;
  if (rival < 12)
    rival_compose(rival, rival_slot);
  menu_resources(false);
  fprintf(stderr, "[vehicles] active %s (cohort %u)\n", vehicles[id].id, group);
}
void FzeroVehiclesLoaded(void) {
  if (count && g_ram[0x58] && g_ram[RIVAL_VERSION] != 1) {
    unsigned slot = g_ram[0xcf2], group = group_for(FzeroVehicleSelected());
    unsigned id = slot < 4 ? cohort[group][slot] : slot;
    if (id < 12 && !enabled(id)) id = slot;
    g_ram[RIVAL_ID] = (uint8_t)id;
    g_ram[RIVAL_FIRST] = 0;
    g_ram[RIVAL_VERSION] = 1;
    g_ram[RIVAL_SLOT] = (uint8_t)(slot & 3);
  }
  selected_image = 99;
  FzeroVehiclesSync();
}

/* Two native screen columns are a viewport onto the additive roster. The
 * guest still owns its cursor, slide, dimming, confirmation and info panel.
 * Page bindings live in WRAM so rewind/snapshots restore the same viewport. */
enum {
  MENU_STATE = 0x14ce0,
  MENU_DIRECTION = 0x14ce7,
  MENU_ART = 0x300000,
  MENU_PALETTES = 0x340000,
  MENU_CARDS = 0x341000
};
static unsigned page_count(void) { return (count + 3) / 4; }
static unsigned page(unsigned column) {
  unsigned value = g_ram[MENU_STATE + 1 + column];
  return value < page_count() ? value : 0;
}
static unsigned menu_id(unsigned position) {
  if (count <= 4 && position >= 4)
    return 99;
  unsigned index = page(position / 4) * 4 + position % 4;
  return index < count ? roster[index] : 99;
}
static unsigned read16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned address(unsigned offset) {
  return ((offset / 0x8000) << 16) | 0x8000 | (offset & 0x7fff);
}
static void pointer(uint8_t *p, unsigned offset) {
  unsigned bus = address(offset);
  p[0] = bus;
  p[1] = bus >> 8;
  p[2] = bus >> 16;
}
static void rival_compose(unsigned id, unsigned slot) {
  unsigned group = group_for(id), original = vehicles[id].slot;
  uint8_t *rom = g_snes->cart->rom, *record = rom + 0xf0000 + slot * 256;
  memcpy(rom + RIVAL_ART, images[group] + 0x40000 + original * 0x8000, 0x8000);
  memcpy(record, images[group] + 0xf0000 + original * 256, 256);
  record[0] = (uint8_t)(address(RIVAL_ART) >> 16);
  const uint8_t *source = group ? art[group - 1] : stock_image;
  memcpy(rom + RIVAL_PALETTES, source + 0x7cd80 + original * 32, 32);
  pointer(record + 0x97, RIVAL_PALETTES);
  /* Allow the rival's authored top speed without changing the player's
   * identity-owned acceleration, steering, boost or exhaust tables. */
  if (group) rom[0x16f6] = 0x80;
}
static unsigned rival_id(unsigned position) {
  unsigned index = g_ram[RIVAL_FIRST] + position;
  return index < count ? roster[index] : 99;
}
static void rival_resources(bool upload) {
  uint8_t *rom = g_snes->cart->rom;
  static const unsigned tiles[] = {0x164, 0x167, 0x16a, 0x16d,
                                    0x184, 0x187, 0x18a, 0x18d};
  for (unsigned position = 0; position < 8; ++position) {
    unsigned id = rival_id(position), slot = id < 12 ? vehicles[id].slot : 0;
    unsigned group = id < 12 ? group_for(id) : 0;
    const uint8_t *source = group ? art[group - 1] : stock_image;
    unsigned palettes = RIVAL_PALETTES + 32 + position * 64;
    memcpy(rom + palettes, source + 0x76180 + slot * 32, 32);
    memcpy(rom + palettes + 32, source + 0x7cd80 + slot * 32, 32);
    if (id >= 12) memset(rom + palettes, 0, 64);
    for (unsigned entry = 0; entry < 12; ++entry) {
      uint8_t *p = rom + 0xf4386 + entry * 16;
      if (p[10] != position) continue;
      /* These are MVN source/destination pairs, not ordinary 24-bit pointers. */
      for (unsigned kind = 0; kind < 2; ++kind) {
        unsigned bus = address(palettes + kind * 32);
        p[kind * 4] = bus;
        p[kind * 4 + 1] = bus >> 8;
        p[kind * 4 + 3] = bus >> 16;
      }
    }
    if (!upload) continue;
    for (unsigned tile = 0; tile < 6; ++tile) {
      unsigned dest = 0x4000 + (tiles[position] + tile / 3 * 16 + tile % 3) * 32;
      const uint8_t *pixels = source + 0x40000 + slot * 0x8000 + 0x6860 + tile * 32;
      for (unsigned j = 0; j < 16; ++j) {
        unsigned value = id < 12 ? read16(pixels + j * 2) : 0;
        g_ppu->vram[dest / 2 + j] = (uint16_t)value;
        g_ram[0x18000 + dest + j * 2] = value;
        g_ram[0x18001 + dest + j * 2] = value >> 8;
      }
    }
  }
}
static void menu_resources(bool upload) {
  if (!count || !g_snes || !g_snes->cart || !images[0])
    return;
  uint8_t *rom = g_snes->cart->rom;
  static const unsigned tile_base[8] = {0, 0x30, 0x60, 0x0a,
                                        5, 0x35, 0x65, 0x3a};
  static const unsigned tile_source[15] = {
      0x55e0, 0x5600, 0x5620, 0x5640, 0x5660, 0x56e0, 0x5700, 0x5720,
      0x5740, 0x5760, 0x5680, 0x56a0, 0x56c0, 0x5780, 0x57a0};
  for (unsigned position = 0; position < 8; ++position) {
    unsigned id = menu_id(position), slot = id < 12 ? vehicles[id].slot : 0;
    unsigned group = id < 12 ? group_for(id) : 0;
    const uint8_t *source = group ? art[group - 1] : stock_image;
    unsigned bank = MENU_ART + position * 0x8000;
    memcpy(rom + bank, source + 0x40000 + slot * 0x8000, 0x8000);
    unsigned normal = MENU_PALETTES + position * 32, dim = normal + 256;
    memcpy(rom + normal, source + 0x7cd80 + slot * 32, 32);
    memcpy(rom + dim, source + 0x76180 + slot * 32, 32);
    if (id >= 12) {
      memset(rom + bank, 0, 0x8000);
      memset(rom + normal, 0, 32);
      memset(rom + dim, 0, 32);
    }
    /* Palette setup contains the same sources as the selection tables. */
    for (unsigned entry = 0; entry < 16; ++entry) {
      const uint8_t *original = images[0] + 0xf42a2 + entry * 6;
      for (unsigned kind = 0; kind < 2; ++kind) {
        const uint8_t *lookup =
            images[0] + (kind ? 0xf44fc : 0xf44dc) + position * 4;
        if (!memcmp(original, lookup, 3))
          pointer(rom + 0xf42a2 + entry * 6, kind ? dim : normal);
      }
    }
    pointer(rom + 0xf44dc + position * 4, normal);
    pointer(rom + 0xf44fc + position * 4, dim);
    rom[0xf41aa + position] = (uint8_t)(address(bank) >> 16);
    char *label = (char *)rom + 0xf5b20 + position * 16;
    memset(label, ' ', 15);
    label[15] = 0;
    if (id < 12) {
      size_t n = strlen(vehicles[id].name);
      memcpy(label + (15 - n) / 2, vehicles[id].name, n);
      for (unsigned j = 0; j < 15; ++j)
        if (label[j] == '.')
          label[j] = '_';
    }
    unsigned row = slot == 1 ? 2 : slot == 2 ? 1 : slot;
    const uint8_t *card = images[0] + 0xf5ba0 + row * 4;
    unsigned card_offset = (unsigned)card[2] * 0x8000 + (read16(card) & 0x7fff);
    card = group ? source + INFO_BASE + 8 + slot * INFO_ENTRY_SIZE
                 : images[0] + card_offset;
    memcpy(rom + MENU_CARDS + position * INFO_CARD_SIZE, card, INFO_CARD_SIZE);
    pointer(rom + 0xf5ba0 + position * 4, MENU_CARDS + position * INFO_CARD_SIZE);
    if (!upload)
      continue;
    /* Native static previews are 5x3 BG tiles, with their own authored dim
     * palette. Reuse that layout rather than drawing a host overlay. */
    for (unsigned tile = 0; tile < 15; ++tile) {
      unsigned dest =
          0x4000 + (tile_base[position] + tile / 5 * 16 + tile % 5) * 32;
      const uint8_t *pixels = rom + bank + tile_source[tile];
      memcpy(g_ram + 0x18000 + dest, pixels, 32);
      for (unsigned j = 0; j < 16; ++j)
        g_ppu->vram[dest / 2 + j] = read16(pixels + j * 2);
    }
    const uint8_t *dest = rom + 0xf451c + position * 4;
    unsigned ram_address = (dest[2] == 0x7f ? 0x10000 : 0) + read16(dest);
    memcpy(g_ram + ram_address, rom + dim, 32);
  }
  rival_resources(upload);
}

/* The guest's eight display slots still own selection, dimming, sprite
 * animation and confirmation. Present those slots through one continuous
 * strip instead of exposing their alternating 0/48-pixel backing buffers.
 * Only native BG tiles, palettes and OBJ positions are adapted; the guest
 * frame/text/info screens and the stock BS menu are unchanged.
 *
 * A 56-pixel pitch leaves equal 16-pixel neighboring previews inside the
 * original 104-pixel pane, clear of the native cursor. Direction lives in
 * guest WRAM; motion comes from the native slide counter, including rewind.
 */
enum { PANE_TILES = 0x4000, PANE_TILE_WORDS = (12 * 15 + 1) * 16 };
static uint16_t pane_map_backup[1024], pane_tiles_backup[PANE_TILE_WORDS];
static uint16_t pane_obj_backup[512];
static unsigned pane_obj_base, pane_map_base;
static const unsigned pane_sprite_x[] = {0, 16, 32, 0, 16, 32, 48, 48};
static const unsigned pane_sprite_y[] = {0, 0, 0, 16, 16, 16, 0, 16};
static bool pane_active;
static bool practice_results_active;
static unsigned practice_results_base, practice_results_sprites[6], practice_results_count;
static uint16_t practice_results_tiles[2][128], practice_results_palette[16];
static uint16_t practice_results_attrs[6];
static int pane_offset(const uint8_t *ram) {
  int column = (ram[0x14c84] & 4) / 4;
  int remaining = abs(column * 48 - (int)read16(ram + 0x14c86));
  if (remaining > 48) remaining = 48;
  return (ram[MENU_DIRECTION] == 255 ? -1 : 1) * remaining * 56 / 48;
}
static unsigned pane_id(const uint8_t *ram, int relative, unsigned row) {
  unsigned column = (ram[0x14c84] & 4) / 4;
  int current = ram[MENU_STATE + 1 + column] % page_count();
  unsigned neighbor = (current + (int)page_count() * 2 + relative) % page_count();
  unsigned index = neighbor * 4 + row;
  return index < count ? roster[index] : 99;
}
void FzeroVehiclesBeginFrame(Ppu *ppu, const uint8_t *ram) {
  /* Practice's native BEST 5/BEST LAP screen loads retail car icons from its
   * own OBJ stream. Records already belong to the selected stable identity;
   * replace only those icon tiles and their palette during presentation.
   * Keep the guest's times, text, sprite positions and transparent backdrop. */
  unsigned id = FzeroVehicleSelected();
  practice_results_active = count > 4 && id >= 4 && ram[0x58] &&
      ram[0x54] == 3 && ram[0x55] == 3 && ram[0x56] >= 4 && ram[0x56] <= 6;
  practice_results_count = 0;
  if (practice_results_active) {
    for (unsigned i = 0; i < 128 && practice_results_count < 6; ++i) {
      unsigned position = ppu->oam[i * 2], attr = ppu->oam[i * 2 + 1];
      unsigned high = ppu->highOam[i / 4] >> ((i % 4) * 2) & 3;
      /* Native 16x16 car reservations use bank 1, even tiles 0..6 and
       * the corresponding retail car palette (0..3). Empty ranked rows
       * have their sprites hidden offscreen. */
      if (high != 2 || (position >> 8) >= 224 ||
          (attr & 0x1ff) < 0x100 || (attr & 0x1ff) > 0x106 ||
          (attr & 1) || (attr & 0xe00) != (((attr & 7) / 2) << 9)) continue;
      unsigned n = practice_results_count++;
      practice_results_sprites[n] = i;
      practice_results_attrs[n] = (uint16_t)attr;
    }
    practice_results_active = practice_results_count != 0;
    if (practice_results_active) {
      practice_results_base = (((ppu->obsel & 7) << 13) +
          (((ppu->obsel & 0x18) + 8) << 9)) & 0x7fff;
      for (unsigned row = 0; row < 2; ++row)
        memcpy(practice_results_tiles[row],
               ppu->vram + practice_results_base + row * 256, sizeof(practice_results_tiles[row]));
      memcpy(practice_results_palette, ppu->cgram + 240, sizeof(practice_results_palette));
      const uint8_t *source = art[group_for(id) - 1] +
          0x40000 + vehicles[id].slot * 0x8000 + 0x6aa0;
      for (unsigned slot = 0; slot < 4; ++slot)
        for (unsigned tile = 0; tile < 4; ++tile)
          for (unsigned word = 0; word < 16; ++word)
            ppu->vram[practice_results_base + slot * 32 + tile / 2 * 256 +
                      tile % 2 * 16 + word] = (uint16_t)read16(source + tile * 32 + word * 2);
    }
  }
  pane_active = count > 4 && ram[MENU_STATE] == 1 &&
                ram[0x54] == 1 && (ram[0x55] == 1 || ram[0x55] == 7) &&
                ram[0x56] <= (ram[0x58] ? 1 : 2);
  if (!pane_active) return;
  /* Opening the card switches BG2 from $0400 to $0800. That second map
   * contains the stats panel too: adapt only its left-hand car pane and
   * leave the native panel/window animation intact. */
  pane_map_base = (ppu->bgXsc[1] & 0x7c) << 8;
  memcpy(pane_map_backup, ppu->vram + pane_map_base, sizeof(pane_map_backup));
  memcpy(pane_tiles_backup, ppu->vram + PANE_TILES, sizeof(pane_tiles_backup));
  if (pane_map_base == 0x400)
    memset(ppu->vram + pane_map_base, 0, sizeof(pane_map_backup));
  else
    for (unsigned y = 0; y < 32; ++y)
      memset(ppu->vram + pane_map_base + y * 32, 0, 13 * sizeof(uint16_t));
  static const unsigned source_tiles[15] = {
      0x55e0, 0x5600, 0x5620, 0x5640, 0x5660, 0x56e0, 0x5700, 0x5720,
      0x5740, 0x5760, 0x5680, 0x56a0, 0x56c0, 0x5780, 0x57a0};
  for (unsigned i = 0; i < count; ++i) {
    unsigned id = roster[i], group = group_for(id), slot = vehicles[id].slot;
    const uint8_t *source = (group ? art[group - 1] : stock_image) + 0x40000 + slot * 0x8000;
    for (unsigned tile = 0; tile < 15; ++tile)
      for (unsigned j = 0; j < 16; ++j)
        ppu->vram[PANE_TILES + (i * 15 + tile) * 16 + j] =
            (uint16_t)read16(source + source_tiles[tile] + j * 2);
  }
  memset(ppu->vram + PANE_TILES + 12 * 15 * 16, 0, 32);
  int offset = pane_offset(ram);
  /* Only the six sliding-car sprites own these two uploaded tile rows.
   * Clip their pixels, not the entire OBJ layer: the text also uses OBJs.
   * The stationary info-card spin owns a different eight-piece layout. */
  pane_obj_base = (((ppu->obsel & 7) << 13) + (((ppu->obsel & 0x18) + 8) << 9) + 0xe00) & 0x7fff;
  memcpy(pane_obj_backup, ppu->vram + pane_obj_base, sizeof(pane_obj_backup));
  for (unsigned sprite = 0; pane_map_base == 0x400 && sprite < 6; ++sprite) {
    unsigned attr = ppu->oam[sprite * 2 + 1];
    unsigned base = ((ppu->obsel & 7) << 13);
    if (attr & 0x100) base += (((ppu->obsel & 0x18) + 8) << 9);
    for (unsigned y = 0; y < 16; ++y)
      for (unsigned x = 0; x < 16; ++x) {
        int screen_x = 28 + (int)pane_sprite_x[sprite] + offset + (int)x;
        if (screen_x >= 0 && screen_x < 104) continue;
        unsigned cx = attr & 0x4000 ? 15 - x : x;
        unsigned cy = attr & 0x8000 ? 15 - y : y;
        unsigned tile = ((((attr & 255) >> 4) + cy / 8) << 4) |
                        (((attr & 15) + cx / 8) & 15);
        unsigned word = (base + tile * 16 + (cy & 7)) & 0x7fff;
        if (word < pane_obj_base || word + 8 >= pane_obj_base + 512) continue;
        uint16_t mask = (uint16_t)~(0x101u << (7 - (cx & 7)));
        ppu->vram[word] &= mask;
        ppu->vram[word + 8] &= mask;
      }
  }
  unsigned column = (ram[0x14c84] & 4) / 4;
  for (int relative = -2; relative <= 2; ++relative) {
    int x = 32 + relative * 56;
    if (x + offset >= 104 || x + offset + 40 <= 0) continue;
    unsigned palette = 4 + (relative + 3) % 3;
    for (unsigned row = 0; row < 4; ++row) {
      unsigned id = pane_id(ram, relative, row), index = 0;
      while (index < count && roster[index] != id) ++index;
      for (unsigned tile = 0; tile < 15; ++tile) {
        int tile_x = x + (int)(tile % 5) * 8;
        /* The open panel no longer windows off BG2's right-hand side.
         * Do not wrap the left neighbor's clipped tiles into that panel. */
        if (pane_map_base != 0x400 && (tile_x < 0 || tile_x >= 104)) continue;
        unsigned word = pane_map_base + (6 + row * 5 + tile / 5) * 32 +
                        ((x / 8 + (int)(tile % 5)) & 31);
        bool blank = index == count || (!relative && row == (ram[0x14c84] & 3));
        ppu->vram[word] = (uint16_t)(palette * 0x400 + 0x200 +
                                    (blank ? 12 * 15 : index * 15 + tile));
      }
    }
  }
  /* Reuse the active native arrow's color indices, always on the left and
   * facing the selected ship. It travels with that column during the slide. */
  unsigned row = ram[0x14c84] & 3;
  for (unsigned y = 0; y < 2; ++y)
    ppu->vram[pane_map_base + (7 + row * 5 + y) * 32 + 2] =
        (uint16_t)(0x920 + column * 8 + row * 2 + y);
}
void FzeroVehiclesRaster(Ppu *ppu, const uint8_t *ram, unsigned line) {
  if (practice_results_active) {
    unsigned id = FzeroVehicleSelected();
    const uint8_t *palette = art[group_for(id) - 1] + 0x7cd80 + vehicles[id].slot * 32;
    for (unsigned i = 0; i < 16; ++i) ppu->cgram[240 + i] = (uint16_t)read16(palette + i * 2);
    for (unsigned n = 0; n < practice_results_count; ++n) {
      unsigned sprite = practice_results_sprites[n];
      ppu->oam[sprite * 2 + 1] = (uint16_t)((ppu->oam[sprite * 2 + 1] & ~0xe00) | 0xe00);
    }
  }
  if (!pane_active) return;
  int offset = pane_offset(ram);
  ppu->hScroll[1] = (uint16_t)-offset;
  unsigned selected_row = ram[0x14c84] & 3;
  /* Once the card opens, native OAM already centers the rotating car and
   * changes piece positions/sizes with its angle. Keep that authored layout. */
  for (unsigned i = 0; pane_map_base == 0x400 && i < 8; ++i) {
    int x = 28 + (int)pane_sprite_x[i] + offset;
    unsigned y = 44 + selected_row * 40 + pane_sprite_y[i];
    ppu->oam[i * 2] = (uint16_t)((y << 8) | (x & 255));
    unsigned shift = (i % 4) * 2;
    ppu->highOam[i / 4] = (uint8_t)((ppu->highOam[i / 4] & ~(3u << shift)) |
                                   ((2u | ((unsigned)x >> 8 & 1)) << shift));
  }
  if (line < 48) return;
  unsigned row = (line - 48) / 40;
  if (row >= 4) return;
  for (int relative = -2; relative <= 2; ++relative) {
    int x = 32 + relative * 56 + offset;
    if (x >= 104 || x + 40 <= 0) continue;
    unsigned id = pane_id(ram, relative, row), group = id < 12 ? group_for(id) : 0;
    unsigned slot = id < 12 ? vehicles[id].slot : 0;
    const uint8_t *palette = (group ? art[group - 1] : stock_image) + 0x76180 + slot * 32;
    unsigned dest = (4 + (relative + 3) % 3) * 16;
    for (unsigned i = 0; i < 16; ++i)
      ppu->cgram[dest + i] = id < 12 ? (uint16_t)read16(palette + i * 2) : 0;
  }
}
void FzeroVehiclesEndFrame(Ppu *ppu) {
  if (practice_results_active) {
    for (unsigned row = 0; row < 2; ++row)
      memcpy(ppu->vram + practice_results_base + row * 256,
             practice_results_tiles[row], sizeof(practice_results_tiles[row]));
    memcpy(ppu->cgram + 240, practice_results_palette, sizeof(practice_results_palette));
    for (unsigned n = 0; n < practice_results_count; ++n)
      ppu->oam[practice_results_sprites[n] * 2 + 1] = practice_results_attrs[n];
    practice_results_active = false;
  }
  if (!pane_active) return;
  memcpy(ppu->vram + pane_map_base, pane_map_backup, sizeof(pane_map_backup));
  memcpy(ppu->vram + PANE_TILES, pane_tiles_backup, sizeof(pane_tiles_backup));
  memcpy(ppu->vram + pane_obj_base, pane_obj_backup, sizeof(pane_obj_backup));
  pane_active = false;
}
static void menu_hook(CpuState *cpu, uint32_t pc) {
  if (pc == 0x1eb4e7) {
    /* Deluxe saves a four/eight-car number in its ghost header. Reserve bit
     * 7 for our stable identity; leave its recording/checksum format intact. */
    cpu_write_a_m(cpu, (uint16_t)(0x80 | FzeroVehicleSelected()));
  } else if (pc == 0x1eb24f) {
    unsigned saved = cpu->A;
    if (saved & 0x80) {
      unsigned id = saved & ~0x80u;
      cpu_write_a_m(cpu, (uint16_t)(id < 12 && enabled(id) ? vehicles[id].slot : 0xffff));
    }
  } else if (pc == 0x1eb3f3) {
    unsigned saved = g_ram[0x14cfd], id = saved & 0x7f;
    /* Authored BS ghosts keep the ordinary native path. A CGP ghost owns
     * the same spare slot as a chosen Practice rival, with its own artwork. */
    if (!(g_ram[0x14ca0] & 0x40) && (saved & 0x80) && id < 12 && enabled(id)) {
      unsigned slot = (vehicles[FzeroVehicleSelected()].slot + 1) % 4;
      g_ram[RIVAL_ID] = (uint8_t)id;
      g_ram[RIVAL_SLOT] = (uint8_t)slot;
      g_ram[RIVAL_VERSION] = 1;
      g_ram[0xcf2] = (uint8_t)slot;
      FzeroVehiclesSync();
      cpu_write_a_m(cpu, (uint16_t)slot);
      cpu->_flag_Z = !slot;
      cpu->_flag_N = 0;
      interp_bridge_pre_opcode_redirect(0x1eb3f7);
    }
  } else if (pc == 0x1ec4da) {
    /* Native title/frame palettes follow the selected identity, not its
     * current display row. Original cars retain their retail colors unless
     * their individual rebalance is enabled. Leave the copy routine intact. */
    unsigned id = FzeroVehicleSelected(), group = group_for(id);
    unsigned slot = vehicles[id].slot;
    const uint8_t *colors = group
        ? art[group - 1] + INFO_BASE + 8 + slot * INFO_ENTRY_SIZE + INFO_CARD_SIZE
        : stock_image + 0x18901 + slot * 6;
    memcpy(g_ram + 0x532, colors, 6);
    memcpy(g_ram + 0x544, colors, 6);
  } else if (pc == 0x00a1ac || pc == 0x00c163) {
    /* Exhaust animation alternates $0760/$0860 into the HUD palette. Those
     * legacy tables can contain a retail or another donor car's marker.
     * Resolve only that color by identity in both phases, retaining every
     * exhaust color and the original animation/copy routine. Apply it after
     * the initial palette load too: the course intro is already visible
     * before the first exhaust update, and shares these text colors. */
    static const unsigned hud_row[] = {3, 0, 2, 1};
    for (unsigned slot = 0; slot < 4; ++slot) {
      unsigned id = g_snes->cart->rom[0xf00ff + slot * 256];
      if (id >= 12) continue;
      unsigned group = group_for(id);
      const uint8_t *source = group ? art[group - 1] : stock_image;
      const uint8_t *color = source + 0x7cd06 + hud_row[vehicles[id].slot] * 32;
      unsigned offset = 6 + hud_row[slot] * 32;
      memcpy(g_ram + 0x600 + offset, color, 2);
      memcpy(g_ram + 0x700 + offset, color, 2);
      memcpy(g_ram + 0x800 + offset, color, 2);
    }
  } else if (pc == 0x1edd01) {
    g_ram[MENU_STATE] = 1;
    g_ram[MENU_DIRECTION] = 1;
    unsigned index = 0;
    while (index + 1 < count && roster[index] != FzeroVehicleSelected())
      ++index;
    g_ram[MENU_STATE + 1] = (uint8_t)(index / 4);
    g_ram[MENU_STATE + 2] = (uint8_t)((index / 4 + 1) % page_count());
    g_ram[RIVAL_ID] = (uint8_t)roster[0];
    g_ram[RIVAL_FIRST] = 0;
    g_ram[RIVAL_VERSION] = 1;
    g_ram[0x14c84] = (uint8_t)(index % 4);
    FzeroVehiclesSync();
    menu_resources(true);
  } else if (pc == 0x1ec76e) {
    unsigned index = 0;
    while (index + 1 < count && roster[index] != FzeroVehicleSelected())
      ++index;
    g_ram[0x14c84] = (uint8_t)(index % 4);
    g_ram[0x14c85] = 0;
    g_ram[0x14c86] = g_ram[0x14c87] = 0;
    g_ram[0x14c88] = g_ram[0x14c89] = 0;
    g_ram[0x14c8a] = g_ram[0x14c8b] = 0;
  } else if (pc == 0x1ed90a) {
    unsigned old = read16(g_ram + 0x14c84) & 7;
    unsigned next = cpu->A & 7, column = old / 4,
             rows = count - page(column) * 4;
    if (rows > 4)
      rows = 4;
    unsigned buttons = read16(g_ram + 0x67);
    if ((buttons & 0x2f00) == 0x2000) {
      unsigned index = (page(column) * 4 + old % 4 + 1) % count;
      if (index / 4 != page(column))
        column ^= 1;
      g_ram[MENU_STATE + 1 + column] = (uint8_t)(index / 4);
      next = column * 4 + index % 4;
    } else if ((buttons & 0x300) == 0x100 || (buttons & 0x300) == 0x200) {
      if (page_count() > 1) {
        unsigned target =
            (page(column) + page_count() + ((buttons & 0x200) ? -1 : 1)) %
            page_count();
        column ^= 1;
        g_ram[MENU_STATE + 1 + column] = (uint8_t)target;
        rows = count - target * 4;
        if (rows > 4)
          rows = 4;
        next = column * 4 + ((old % 4 < rows) ? old % 4 : rows - 1);
      } else
        next = old;
    } else {
      next = column * 4 + ((next % 4 < rows) ? next % 4 : rows - 1);
    }
    const char *test = getenv("FZERO_TEST_VEHICLE");
    if (test)
      for (unsigned i = 0; i < count; ++i)
        if (!strcmp(test, vehicles[roster[i]].id)) {
          if (page(column) != i / 4)
            column ^= 1;
          g_ram[MENU_STATE + 1 + column] = (uint8_t)(i / 4);
          next = column * 4 + i % 4;
        }
    if (next / 4 != old / 4)
      g_ram[MENU_DIRECTION] = (buttons & 0x300) == 0x200 ? 255 : 1;
    cpu->A = (uint16_t)next;
    unsigned id = menu_id(next);
    g_ram[ID_ADDRESS] = (uint8_t)id;
    FzeroVehiclesSync();
    if (next != old) {
      g_ram[0x14c84] = (uint8_t)next;
      menu_resources(true);
      g_ram[0x14c84] = (uint8_t)old;
    }
  } else if (pc == 0x1edb0c) {
    /* Native info/handling record lookup uses the physical racing slot. */
    cpu->A = (uint16_t)vehicles[FzeroVehicleSelected()].slot;
  } else if (pc == 0x1ec81b) {
    cpu->A = (cpu->A & 0xff00) | vehicles[FzeroVehicleSelected()].slot;
  } else if (pc == 0x1ed04f) {
    unsigned old = g_ram[0x14c8a], next = old;
    unsigned index = g_ram[RIVAL_FIRST] + old;
    unsigned buttons = read16(g_ram + 0x67);
    if ((buttons & 0x2f00) == 0x2000) {
      if (old >= 0xfe) index = old == 0xff ? count + 1 : 0;
      else ++index;
    } else if (old < 8) {
      if ((buttons & 0x300) == 0x100) index = index + 2 < count ? index + 2 : index;
      if ((buttons & 0x300) == 0x200) index = index >= 2 ? index - 2 : index;
      if ((buttons & 0xc00) == 0x800) index = index & 1 ? index - 1 : count;
      if ((buttons & 0xc00) == 0x400) index = !(index & 1) && index + 1 < count ? index + 1 : count;
    } else {
      /* Preserve native no-rival/ghost navigation, resolving a car through
       * the current viewport rather than truncating it to the first four. */
      unsigned candidate = cpu->A & 255;
      index = candidate < 8 ? g_ram[RIVAL_FIRST] + candidate
                           : candidate == 0xfe ? count + 1 : count;
    }
    if (index < count) {
      while (index < g_ram[RIVAL_FIRST]) g_ram[RIVAL_FIRST] -= 2;
      while (index >= g_ram[RIVAL_FIRST] + 8u) g_ram[RIVAL_FIRST] += 2;
      next = index - g_ram[RIVAL_FIRST];
      g_ram[RIVAL_ID] = (uint8_t)roster[index];
    } else {
      next = index == count ? 0xff : 0xfe;
      g_ram[RIVAL_ID] = (uint8_t)next;
    }
    cpu->A = (uint16_t)next;
    FzeroVehiclesSync();
    rival_resources(true);
  } else if (pc == 0x1ec831) {
    unsigned rival = g_ram[RIVAL_ID];
    cpu_write_a_m(cpu, (uint16_t)(rival < 12 ? g_ram[RIVAL_SLOT] : rival));
  } else if (pc == 0x00d54c) {
    /* Native rival difficulty follows its original identity, independently
     * of the spare physical slot used for this player/rival pair. */
    unsigned source = selected_rival < 12 ? vehicles[selected_rival].slot : 99;
    unsigned value = source < 4 ? g_snes->cart->rom[0x17fda + source] : 0;
    cpu_write_a_m(cpu, (uint16_t)value);
    cpu->_flag_Z = !(cpu->A & 255);
    cpu->_flag_N = (cpu->A & 128) != 0;
    interp_bridge_pre_opcode_redirect(0x00d54f);
  }
}
void FzeroVehiclesInstallHooks(void) {
  if (!count)
    return;
  const unsigned sites[] = {0x1edd01, 0x1ec76e, 0x1ed90a, 0x1edb0c, 0x1ec81b,
                            0x1ed04f, 0x1ec831, 0x00d54c, 0x00c163, 0x1ec4da,
                            0x1eb4e7, 0x1eb24f, 0x1eb3f3, 0x00a1ac};
  for (unsigned i = 0; i < sizeof(sites) / sizeof(*sites); ++i)
    interp_bridge_set_pre_opcode_hook(sites[i], menu_hook);
}

uint8_t *FzeroVehicleVersusImage(const uint8_t *stock,size_t size,unsigned group,char *error,size_t cap) {
  if(!stock || size!=0x80000 || group<1 || group>3)return NULL;
  CpPack pack;char path[96];size_t length=0;uint8_t *source=NULL;
  snprintf(path,sizeof(path),"assets/vehicle-packs/cgp-p%u.ips",group);
  if(!manifest(group,&pack,error,cap) || !cp_pack_apply(&pack,stock,size,path,&source,&length,error,cap))return NULL;
  if(length!=size+8+4*INFO_ENTRY_SIZE || memcmp(source+size,"FZCARD1\0",8) ||
      memcmp(source+0x5ec00,stock+0x5ec00,0x400) || memcmp(source+0x46f80,stock+0x46f80,0x80)) {
    snprintf(error,cap,"Invalid CGP P%u VS resources",group);free(source);return NULL;
  }
  uint8_t *image=calloc(1,IMAGE_SIZE);
  if(!image) {free(source);snprintf(error,cap,"Cannot allocate vehicle profile");return NULL;}
  memcpy(image,source,length);free(source);FzeroGameplayBuildVehicleProfile(image,group);return image;
}
