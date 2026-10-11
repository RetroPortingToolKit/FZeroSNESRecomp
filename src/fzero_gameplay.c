#include "fzero_gameplay.h"
#include "fzero_msu.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "fzero_course_runtime.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay_patches.inc"
#include "fzero_tracks.h"
#include "fzero_vehicles.h"
#include "sha256.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FzeroGameplaySettings settings;
static bool cars, tracks;
static char error[192];
static uint8_t signature[32];
static const DispatchEntry patched_program[1] = {{0}};
static uint8_t turn[4][30], acceleration[4][29];
static uint8_t native_frequency[45];
static uint16_t class_last_input;
static unsigned class_hold;
bool FzeroRuleEnabled(FzeroRule r) {
  return r != FZERO_RULE_CREDITS && (settings.enabled & (1u << r)) != 0;
}
bool FzeroBsCars(void) { return cars; }
bool FzeroBsTracks(void) { return tracks; }
bool FzeroGameplayActive(void) { return settings.enabled || cars != tracks || settings.vehicle_packs || settings.stock_rebalance; }
const FzeroGameplaySettings *FzeroGameplaySettingsCurrent(void) {
  return &settings;
}
const char *FzeroGameplayError(void) { return error; }
const uint8_t *FzeroGameplaySignature(void) { return signature; }
static void sign_program(const uint8_t *rom, size_t size) {
  /* Bump the adapter version when a host rule changes its guest semantics. */
  uint8_t key[36] = {3, 0, 0, 0};
  sha256_compute(rom, size, key + 4);
  sha256_compute(key, sizeof(key), signature);
}
void FzeroGameplayConfigure(const FzeroGameplaySettings *s, bool vehicles,
                            bool courses) {
  settings = *s;
  settings.enabled &= FZERO_RULE_ACTIVE_MASK;
  if (vehicles) settings.vehicle_packs = settings.stock_rebalance = 0;
  cars = vehicles;
  tracks = courses &&
           !FzeroTrackLoaderEnabled();
}
void FzeroGameplayHeadless(bool deluxe) {
  FzeroGameplaySettings s = {.tuning = 2, .boost = 2, .exhaust = 2};
  const char *env = getenv("FZERO_RULES");
  s.always_records = getenv("FZERO_ALWAYS_RECORDS") && atoi(getenv("FZERO_ALWAYS_RECORDS")) != 0;
  char copy[1024];
  snprintf(copy, sizeof(copy), "%s", env ? env : "");
  for (char *part = strtok(copy, ","); part; part = strtok(NULL, ",")) {
    if (!strcmp(part, "all"))
      s.enabled = (1u << FZERO_RULE_MSU) - 1;
    for (unsigned i = 0; i < FZERO_RULE_COUNT; ++i)
      if (!strcmp(part, fzero_rules[i].id))
        s.enabled |= 1u << i;
  }
  env = getenv("FZERO_CGP_PROFILE");
  if (env && env[0] >= '1' && env[0] <= '3')
    s.tuning = s.boost = s.exhaust = (unsigned)(env[0] - '1');
  env = getenv("FZERO_BS_CARS");
  bool vehicles = env ? atoi(env) != 0 : deluxe;
  const char *packs = getenv("FZERO_CGP_CARS"), *rebalance = getenv("FZERO_CGP_REBALANCE");
  if (packs) s.vehicle_packs = (unsigned)atoi(packs);
  if (rebalance) s.stock_rebalance = (unsigned)atoi(rebalance);
  if ((s.vehicle_packs || s.stock_rebalance) && !env) vehicles = false;
  env = getenv("FZERO_BS_TRACKS");
  bool courses = env ? atoi(env) != 0 : deluxe;
  if (courses && env)
    FzeroTrackLoaderEnable(false);
  if (!getenv("FZERO_TEST_LEGACY_PROFILES")) s.enabled &= ~7u;
  FzeroGameplayConfigure(&s, vehicles, courses);
}
static unsigned r16(unsigned a) {
  return g_ram[a] | (unsigned)g_ram[a + 1] << 8;
}
static void w16(unsigned a, unsigned v) {
  g_ram[a] = (uint8_t)v;
  g_ram[a + 1] = (uint8_t)(v >> 8);
}
static void accum(CpuState *cpu, unsigned value) {
  cpu_write_a_m(cpu, (uint16_t)value);
  cpu->_flag_Z = !value;
  cpu->_flag_N = (value & (cpu->m_flag ? 128 : 32768)) != 0;
}
static uint8_t rom8(unsigned pc) {
  return cpu_read8(&g_cpu, (uint8_t)(pc >> 16), (uint16_t)pc);
}
static unsigned feature_for_rule(FzeroRule rule) {
  return rule == FZERO_RULE_DMAG ? FZERO_COURSE_GRIP_MAGNETS
         : rule == FZERO_RULE_UP_MAGNET ? FZERO_COURSE_UP_MAGNETS
         : rule == FZERO_RULE_RAINBOW ? FZERO_COURSE_RAINBOW : 0;
}
static bool effective_rule(FzeroRule rule) {
  const FzeroCourse *course = FzeroTracksCurrentCourse();
  /* Authored terrain semantics are mandatory only on the declaring course. */
  return course && (course->required & feature_for_rule(rule));
}
static bool available_rule(FzeroRule rule) {
  return (FzeroTracksRequiredFeatures() & feature_for_rule(rule)) != 0;
}
static void up_magnet(CpuState *cpu) {
  unsigned actor = cpu->X & 255, tile = r16(0xcd0 + actor);
  unsigned velocity = r16(0xbb0 + actor), result;
  bool up = tile == 0xb6 || (tile >= 0xcc && tile < 0xd0);
  if (!up) {
    unsigned amount = tile >= 0xd0 ? 0x60 : 0x61;
    result = (velocity - amount) & 65535;
    cpu->_flag_C = velocity >= amount;
  } else {
    static const unsigned gain[] = {7, 8, 5}, limit[] = {0x190, 0x200, 0x120};
    unsigned tilt = actor ? 1 : (g_ram[0xb10] & 12) >> 2;
    if (tilt > 2) tilt = 0;
    unsigned height = r16(0xbc0 + actor);
    unsigned strength = height <= 0x7000 ? (0x7000 - height) >> 10 : 0;
    cpu_write16(cpu, 0, 0x4202, (uint16_t)strength);
    cpu_write16(cpu, 0, 0x4203, (uint16_t)gain[tilt]);
    unsigned gravity = r16(0x14);
    unsigned fall = (velocity - gravity) & 65535;
    unsigned product = strength * gain[tilt];
    result = (fall + product + (velocity >= gravity)) & 65535;
    cpu->_flag_V = ((~(fall ^ product) & (fall ^ result)) & 0x8000) != 0;
    cpu->_flag_C = result >= limit[tilt];
    if (!((result - limit[tilt]) & 0x8000)) result = limit[tilt];
  }
  accum(cpu, result);
  interp_bridge_pre_opcode_redirect(0x009c71);
}
static void apply(uint8_t *rom, unsigned patch) {
  const RulePatch *p = &rule_patches[FzeroDeluxeActive()][patch];
  for (unsigned i = 0; i < p->count; ++i)
    memcpy(rom + p->writes[i].offset, p->writes[i].data, p->writes[i].size);
}
static void tuning_metadata(uint8_t *rom) {
  /* Deluxe unpacks the stock column tables into one record per vehicle.
   * Keep the BS records intact; do not mistake their IDs for table offsets. */
  static const unsigned fields[][3] = {
      {0xfa81, 0x47, 1}, {0xfa85, 0x48, 1}, {0xfa89, 0x49, 1},
      {0xfa8d, 0x4a, 1}, {0xfa91, 0x4b, 2}, {0xfa99, 0x4d, 2},
      {0xfaa1, 0x4f, 2}, {0xfaa9, 0x51, 1}, {0xfaad, 0x52, 1},
      {0xfab1, 0x53, 1}, {0xfab5, 0x54, 1}, {0xfab9, 0x55, 1},
      {0xfabd, 0x56, 1}, {0xfac1, 0x57, 1}, {0xfac5, 0x58, 2},
      {0xfacd, 0x5a, 2}, {0xfad5, 0x5c, 2}, {0xfadd, 0x5e, 2},
      {0xfae5, 0x60, 2}, {0xfaed, 0x62, 1}};
  for (unsigned car = 0; car < 4; ++car) {
    memcpy(turn[car], rom + 0x149ab + 30 * car, 30);
    memcpy(acceleration[car], rom + 0x14a42 + 29 * car, 29);
    if (!FzeroDeluxeActive())
      continue;
    for (unsigned i = 0; i < sizeof(fields) / sizeof(*fields); ++i)
      memcpy(rom + 0xf0000 + car * 256 + fields[i][1],
             rom + fields[i][0] - 0x8000 + car * fields[i][2], fields[i][2]);
    memcpy(rom + 0xf0063 + car * 256, turn[car], 19);
    memcpy(rom + 0xf0076 + car * 256, acceleration[car], 19);
  }
}
void FzeroGameplayActivateVehicles(const FzeroGameplaySettings *s, uint8_t *rom) {
  settings = *s;
  settings.enabled &= FZERO_RULE_ACTIVE_MASK;
  if (FzeroRuleEnabled(FZERO_RULE_TUNING)) tuning_metadata(rom);
}
void FzeroGameplaySetSignature(const uint8_t hash[32]) { memcpy(signature,hash,32); }
bool FzeroGameplayPrepare(uint8_t **rom, size_t *size) {
  error[0] = 0;
  if (settings.tuning > 2 || settings.boost > 2 || settings.exhaust > 2 ||
      settings.enabled >> FZERO_RULE_COUNT) {
    snprintf(error, sizeof(error), "Invalid CGP rule settings");
    return false;
  }
  memcpy(native_frequency, *rom + 0x17bda, sizeof(native_frequency));
  if (FzeroDeluxeActive() && !cars) {
    /* The stock four occupy the first carousel page. Keep that page and wrap
     * Select within it, without changing any vehicle or race tables. */
    for (unsigned i = 0; i < 4; ++i)
      (*rom)[0xf5958 + i] = (uint8_t)i;
    (*rom)[0xf5963] = 0;
  }
  if (!settings.enabled) {
    if (!FzeroMsuApplyConfigured(rom,size)) { snprintf(error,sizeof(error),"%.190s",FzeroMsuError());return false; }
    sign_program(*rom, *size);
    return true;
  }
  uint8_t *grown = realloc(*rom, 0x400000);
  if (!grown) {
    snprintf(error, sizeof(error), "Cannot allocate CGP rule cartridge");
    return false;
  }
  memset(grown + *size, 0, 0x400000 - *size);
  *rom = grown;
  *size = 0x400000;
  if (FzeroRuleEnabled(FZERO_RULE_TUNING)) {
    apply(grown, settings.tuning);
    tuning_metadata(grown);
  }
  if (FzeroRuleEnabled(FZERO_RULE_BOOST))
    apply(grown, 3 + settings.boost);
  if (FzeroRuleEnabled(FZERO_RULE_EXHAUST))
    apply(grown, 6 + settings.exhaust);
  for (unsigned rule = FZERO_RULE_ANIMATION; rule < FZERO_RULE_COUNT; ++rule) {
    if (!FzeroRuleEnabled((FzeroRule)rule))
      continue;
    /* Complete native adapters below supply these at course boundaries.
     * Installing their donor ASM globally would bypass scoping when a hook
     * declines to run on an unrelated course. */
    if (FZERO_RULE_COURSE_MASK & (1u << rule))
      continue;
    unsigned patch = rule + 6 + (rule > FZERO_RULE_RAINBOW);
    apply(grown, patch);
  }
  /* Legend's higher speeds need the common extended movement range, even
   * without selecting a vehicle rebalance. This is the independent CGP
   * velocity-limit change, not its vehicle tables. */
  if (FzeroRuleEnabled(FZERO_RULE_LEGEND))
    grown[0x16f6] = 0x80;
  /* These bytes are independently assembled mechanics, not a donor engine.
   * No compiled routine may bypass them. The canonical presentation, course,
   * projection and save hooks remain installed in the shared runtime. */
  cpu_select_program(patched_program, 0, NULL, 0);
  interp_bridge_set_scheduler_aot_policy(0);
  if (!FzeroMsuApplyConfigured(rom,size)) { snprintf(error,sizeof(error),"%.190s",FzeroMsuError());return false; }
  sign_program(*rom, *size);
  fprintf(
      stderr,
      "[cgp-rules] enabled=%08x profiles=%u,%u,%u bs-cars=%d bs-tracks=%d\n",
      settings.enabled, settings.tuning + 1, settings.boost + 1,
      settings.exhaust + 1, cars, tracks);
  return true;
}
static unsigned music_course(void) {
  const CpPack *pack = NULL;
  const CpCup *cup = FzeroTracksRuntimeCup(FzeroTracksMenuIndex(), &pack);
  if (!cup || !pack)
    return 0;
  unsigned order = g_ram[0x53];
  if (!strcmp(pack->adapter, "retail") || !strcmp(pack->adapter, "bs-deluxe"))
    return cup->slot * 5 + order;
  /* Unknown packs have no declared CGP soundtrack mapping. Returning an
   * intentionally missing track makes the author's SPC fallback take over. */
  return 200;
}
unsigned FzeroGameplayMusicTrack(unsigned command) {
  if (command != 6) {
    FzeroMsuSelectTrackSource("", false);
    return command;
  }
  const FzeroCourse *course = FzeroTracksCurrentCourse();
  if (course && course->msu_track) {
    FzeroMsuSelectTrackSource(course->msu_source, true);
    return course->msu_track;
  }
  unsigned track = music_course();
  FzeroMsuSelectTrackSource(track == 200 ? NULL : "", true);
  return 10 + track;
}
static void rule_hook(CpuState *cpu, uint32_t pc) {
  pc &= 0x7fffff;
  switch (pc) {
  case 0x1eb805:
    /* Native Practice and records share this availability predicate. CGP's
     * corrected BS courses belong to its imported GP cups; do not expose the
     * disabled original versions through the native Practice selector. */
    if (!tracks && (cpu->A & 255) >= 15 && (cpu->A & 255) < 25) {
      cpu->_flag_Z = 1;
      interp_bridge_pre_opcode_redirect(0x1eb80a);
    }
    break;
  case CGP_MSU_SELECTOR: {
    unsigned value = FzeroGameplayMusicTrack(g_ram[0x46] & 7);
    accum(cpu, value);
    interp_bridge_pre_opcode_redirect(CGP_MSU_RETURN);
    break;
  }
  case 0x008976:
    if (available_rule(FZERO_RULE_RAINBOW)) {
      g_ram[0xadf] = effective_rule(FZERO_RULE_RAINBOW) ? 255 : 0;
      if (g_ram[0xadf])
        g_ram[0x1075] = 0;
    }
    break;
  case 0x009c55:
    if (effective_rule(FZERO_RULE_UP_MAGNET)) {
      unsigned height = cpu->A & 65535;
      cpu->_flag_C = height >= r16(0x29);
      bool landed = !cpu->_flag_C;
      if (!landed) {
        cpu->_flag_C = height >= 0xc000;
        landed = cpu->_flag_C;
      }
      interp_bridge_pre_opcode_redirect(landed ? 0x009c8c : 0x009c5b);
    }
    break;
  case 0x009c6b:
    if (effective_rule(FZERO_RULE_UP_MAGNET)) up_magnet(cpu);
    break;
  case 0x0098b1:
  case 0x0098bc:
  case 0x0098ce:
  case 0x0098f1:
  case 0x0098f9:
    if (effective_rule(FZERO_RULE_DMAG)) {
      unsigned mask = pc == 0x0098ce ? 0x14
                      : pc == 0x0098bc || pc == 0x0098f1 ? 0xf4 : 4;
      if (pc == 0x0098f9) cpu->_flag_Z = !(cpu->A & mask);
      else accum(cpu, cpu->A & mask);
      interp_bridge_pre_opcode_redirect(pc + 2);
    }
    break;
  case 0x009292:
    if (effective_rule(FZERO_RULE_DMAG) && !(g_ram[0xd51] & 128) && (g_ram[0xd50] & 8)) {
      w16(0x17, 0x220);
      g_ram[0xad3] |= 0x80;
      interp_bridge_pre_opcode_redirect(0x0092f9);
    }
    break;
  case 0x009b59:
    if (effective_rule(FZERO_RULE_DMAG)) {
      cpu->X = 0;
      accum(cpu, (g_ram[0xd50] & 8) ? g_ram[0xd50] : g_ram[0xe1]);
      interp_bridge_pre_opcode_redirect((g_ram[0xd50] & 8) || !g_ram[0xe1] ? 0x009b85 : 0x009b5f);
    }
    break;
  case 0x0091e6:
    if (g_ram[0xadf] && effective_rule(FZERO_RULE_RAINBOW)) {
      g_ram[0x14] = 0x38;
      g_ram[0x02] = 0;
      accum(cpu, g_ram[0xb10 + (cpu->X & 255)]);
      interp_bridge_pre_opcode_redirect(0x0091eb);
    }
    break;
  case 0x0098f5:
    if (g_ram[0xadf] && effective_rule(FZERO_RULE_RAINBOW)) {
      unsigned flags = g_ram[0] & 0x8c;
      if (flags & 128) {
        g_ram[0xc3] |= 0x40;
        g_ram[0xcf] = 6;
        g_ram[0xf38] = g_ram[0xd40];
      }
      interp_bridge_pre_opcode_redirect(flags && !(flags & 128) ? 0x00992d : 0x009967);
    }
    break;
  case 0x00ba20:
    if (g_ram[0xadf] && effective_rule(FZERO_RULE_RAINBOW)) {
      accum(cpu, 0);
      interp_bridge_pre_opcode_redirect(0x00ba25);
    }
    break;
  case 0x00e70b:
    if (!cpu->X && g_ram[0xadf] && effective_rule(FZERO_RULE_RAINBOW))
      interp_bridge_pre_opcode_redirect(0x00e716);
    break;
  case 0x1ec740:
    if (!cars)
      g_ram[0x52] &= 3;
    break;
  case 0x1ed04f:
    if (!cars && (cpu->A & 255) < 8)
      accum(cpu, cpu->A & 3);
    break;
  case 0x00b890: {
    if (!effective_rule(FZERO_RULE_DMAG) && !FzeroRuleEnabled(FZERO_RULE_TUNING) && !FzeroVehiclesActive()) break;
    bool magnet = effective_rule(FZERO_RULE_DMAG) && !(g_ram[0xd51] & 128) &&
                  (g_ram[0xd50] & 8);
    unsigned car = g_ram[0x52] & 7, speed = r16(0xb20) >> 7;
    unsigned value;
    if (magnet)
      value = 0x38;
    /* BS slots, including the title replay's Blue Thunder, retain native
     * turning data rather than the selected four-slot CGP cohort. */
    else if (FzeroVehiclesActive() && car < 4)
      value = FzeroVehicleTurn(speed);
    else if (FzeroRuleEnabled(FZERO_RULE_TUNING) && car < 4)
      value = turn[car][speed < 30 ? speed : 29];
    else if (FzeroDeluxeActive())
      value = speed < 18 ? g_ram[0x14d63 + speed] : rom8(0x02c9f7 + speed);
    else
      value = rom8(0x02c9ab + g_ram[0xef] + speed);
    cpu_write8(cpu, 0, 0x4203, (uint8_t)value);
    accum(cpu, value);
    interp_bridge_pre_opcode_redirect(0x00b8b1);
    break;
  }
  case 0x00b8e6: {
    if (!effective_rule(FZERO_RULE_DMAG)) break;
    unsigned speed = r16(0xb20) >> 7;
    unsigned value =
        !(g_ram[0xd51] & 128) && (g_ram[0xd50] & 8)
            ? 0x50
            : rom8((FzeroRuleEnabled(FZERO_RULE_TUNING) ? 0x02ca23 : 0x02ca18) +
                   speed);
    accum(cpu, value);
    interp_bridge_pre_opcode_redirect(0x00b8f3);
    break;
  }
  case 0x0094ce:
    if (!FzeroDeluxeActive() && cpu->X && g_ram[0xd71 + (cpu->X & 255)] >= 0x94) {
      unsigned actor = cpu->X & 255, speed = cpu->A & 255;
      /* The author routines change D71 only after the opening Straightaway,
       * at lap/rank updates, and at the finish. Preserve that state machine. */
      unsigned offset = g_ram[0xd71 + actor] - 0x94 + speed;
      cpu->Y = (uint16_t)actor;
      accum(cpu, rom8(0x02cad6 + (offset < 29 ? offset : 28)));
      interp_bridge_pre_opcode_redirect(0x0094d7);
    }
    break;
  case 0x1eacf0: {
    unsigned actor = cpu->X & 255, speed = cpu->A & 255;
    unsigned car = actor ? g_ram[0xd71 + actor] : g_ram[0x52];
    if (actor && car >= 0x94 && (FzeroRuleEnabled(FZERO_RULE_LEGEND) || FzeroVehiclesActive())) {
      unsigned offset = car - 0x94 + speed;
      cpu_write_a_m(cpu, rom8(0x02cad6 + (offset < 29 ? offset : 28)));
    } else if (FzeroVehiclesActive() && car < 4)
      cpu_write_a_m(cpu, FzeroVehicleAcceleration(car,speed));
    else if (FzeroRuleEnabled(FZERO_RULE_TUNING) && car < 4)
      cpu_write_a_m(cpu, acceleration[car][speed < 29 ? speed : 28]);
    else
      break;
    cpu->Y = (uint16_t)actor;
    interp_bridge_pre_opcode_redirect(0x1ead13);
    break;
  }
  case 0x1eadde:
    if (FzeroRuleEnabled(FZERO_RULE_EXHAUST) && g_ram[0x52] < 4) {
      unsigned car = g_ram[0x52], frame = cpu->Y & 255;
      if (frame > 12)
        frame = 12;
      unsigned count = exhaust_counts[settings.exhaust][car], carry = 0;
      const uint8_t *p = exhaust_positions[settings.exhaust][car][frame];
      for (unsigned i = 0; i < count; ++i) {
        unsigned sum =
            p[i * 2] + ((unsigned)p[i * 2 + 1] << 8) + r16(0x20) + carry;
        w16(0x2f0 + i * 4, sum);
        carry = sum > 65535;
      }
      g_ram[0x40f] = (uint8_t)(0xffu << (count * 2));
      interp_bridge_pre_opcode_redirect(0x1eae28);
    }
    break;
  case 0x1eac6d:
    if (effective_rule(FZERO_RULE_DMAG) && !(g_ram[0xd51] & 128) && (g_ram[0xd50] & 8)) {
      w16(0x17, 0x220);
      g_ram[0xad3] |= 0x80;
      interp_bridge_pre_opcode_redirect(0x1eacc6);
    }
    break;
  case 0x038841:
  case 0x1ec835: {
    unsigned level = g_ram[0x57] < 5 ? g_ram[0x57] : 0;
    g_ram[0x59] = (uint8_t)(7 - level);
    g_ram[0xf3d] = 0;
    interp_bridge_pre_opcode_redirect(pc == 0x1ec835 ? 0x1ec84a : 0x03884a);
    break;
  }
  case 0x00d432:
    /* Preserve the native resource loader, but make the source's lap speed
     * mask and bounded opponent-frequency selection independent of class. */
    g_ram[0] = 0x1e;
    unsigned level = g_ram[0x57] < 3 ? g_ram[0x57] : 2;
    const FzeroCourse *c = FzeroTracksCurrentCourse();
    unsigned index =
        (FzeroDeluxeActive() ? g_ram[0x90] : g_ram[0x90] / 2) * 15 + level * 5 +
        g_ram[0x53];
    unsigned frequency = c ? c->opponents[level]
                         : FzeroDeluxeActive()
                             ? rom8(0x1ea839 + index)
                             : native_frequency[index < 45 ? index : 0];
    g_ram[0x1065] = g_ram[0x1066] = (uint8_t)frequency;
    interp_bridge_pre_opcode_redirect(0x00d44f);
    break;
  case 0x00d54c:
    /* Practice opponent IDs include eight BS cars and negative ghost/no-CPU
     * sentinels. Convert them to the source's bounded five-class encoding. */
    accum(cpu, (cpu->X & 255) < (cars ? 8u : 4u) ? rom8(0x02ffda + (cpu->X & 3))
                                                 : 0);
    interp_bridge_pre_opcode_redirect(0x00d54f);
    break;
  }
}
void FzeroGameplayInstallHooks(void) {
  class_last_input = 0;
  class_hold = 0;
  if (FzeroDeluxeActive() && !tracks)
    interp_bridge_set_pre_opcode_hook(0x1eb805, rule_hook);
  if (FzeroDeluxeActive() && !cars) {
    interp_bridge_set_pre_opcode_hook(0x1ec740, rule_hook);
    interp_bridge_set_pre_opcode_hook(0x1ed04f, rule_hook);
  }
  if (available_rule(FZERO_RULE_RAINBOW)) {
    interp_bridge_set_pre_opcode_hook(0x008976, rule_hook);
    const unsigned sites[] = {0x0091e6, 0x0098f5, 0x00ba20, 0x00e70b};
    for (unsigned i = 0; i < sizeof(sites) / sizeof(*sites); ++i)
      interp_bridge_set_pre_opcode_hook(sites[i], rule_hook);
  }
  if (available_rule(FZERO_RULE_UP_MAGNET)) {
    interp_bridge_set_pre_opcode_hook(0x009c55, rule_hook);
    interp_bridge_set_pre_opcode_hook(0x009c6b, rule_hook);
  }
  if (FzeroRuleEnabled(FZERO_RULE_MSU))
    interp_bridge_set_pre_opcode_hook(CGP_MSU_SELECTOR, rule_hook);
  if (available_rule(FZERO_RULE_DMAG)) {
    interp_bridge_set_pre_opcode_hook(0x00b8e6, rule_hook);
    const unsigned sites[] = {0x0098b1, 0x0098bc, 0x0098ce, 0x0098f1, 0x0098f9, 0x009b59};
    for (unsigned i = 0; i < sizeof(sites) / sizeof(*sites); ++i)
      interp_bridge_set_pre_opcode_hook(sites[i], rule_hook);
    if (!FzeroDeluxeActive()) interp_bridge_set_pre_opcode_hook(0x009292, rule_hook);
  }
  if (FzeroRuleEnabled(FZERO_RULE_TUNING) || FzeroVehiclesActive() || available_rule(FZERO_RULE_DMAG))
    interp_bridge_set_pre_opcode_hook(0x00b890, rule_hook);
  if (FzeroDeluxeActive()) {
    if (FzeroRuleEnabled(FZERO_RULE_TUNING) || FzeroVehiclesActive() ||
        FzeroRuleEnabled(FZERO_RULE_LEGEND))
      interp_bridge_set_pre_opcode_hook(0x1eacf0, rule_hook);
    if (FzeroRuleEnabled(FZERO_RULE_EXHAUST) || FzeroVehiclesActive())
      interp_bridge_set_pre_opcode_hook(0x1eadde, rule_hook);
    if (available_rule(FZERO_RULE_DMAG))
      interp_bridge_set_pre_opcode_hook(0x1eac6d, rule_hook);
  }
  if (FzeroRuleEnabled(FZERO_RULE_LEGEND)) {
    interp_bridge_set_pre_opcode_hook(0x038841, rule_hook);
    if (FzeroDeluxeActive())
      interp_bridge_set_pre_opcode_hook(0x1ec835, rule_hook);
    interp_bridge_set_pre_opcode_hook(0x00d432, rule_hook);
    interp_bridge_set_pre_opcode_hook(0x00d54c, rule_hook);
    interp_bridge_set_pre_opcode_hook(0x0094ce, rule_hook);
  }
}
uint16_t FzeroGameplayMenuInput(uint16_t input, const uint8_t *ram) {
  bool class_menu = FzeroDeluxeActive() ? ram[0x54] == 1 && ram[0x55] == 1 &&
                                              ram[0x56] == 2 && ram[0x14c98]
                                        : ram[0x54] == 1 && ram[0x55] == 6;
  if (FzeroRuleEnabled(FZERO_RULE_LEGEND) && class_menu && !ram[0x58]) {
    uint16_t direction = input & 0x34;
    bool step = direction && (direction != (class_last_input & 0x34) ||
                              (++class_hold >= 24 && class_hold % 6 == 0));
    if (direction != (class_last_input & 0x34))
      class_hold = 0;
    unsigned address = FzeroDeluxeActive() ? 0x57 : 0x5a;
    if (step) {
      unsigned level = g_ram[address];
      /* Retail $038817..$03882f: Select cycles, Up/Down stop at the ends.
       * Extend that same behavior to Legend instead of wrapping the D-pad. */
      if (direction & 4) level = (level + 1) % 5;
      else if ((direction & 16) && level) --level;
      else if ((direction & 32) && level < 4) ++level;
      g_ram[address] = (uint8_t)level;
    }
    class_last_input = input;
    return input & ~0x34;
  }
  class_last_input = input;
  class_hold = 0;
  return input;
}
