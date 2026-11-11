/* Private-ROM semantic probes, linked only into the headless validator.
 * Execute the actual guest instructions and installed adapters at boundaries
 * where effects can be asserted without requiring a particular driving replay.
 */
#include "common_rtl.h"
#include "cpu_state.h"
#include "fzero_deluxe.h"
#include "fzero_gameplay.h"
#include "fzero_course_runtime.h"
#include "fzero_runtime.h"
#include "fzero_gameplay_patches.inc"
#include "snes/cart.h"
#include "sha256.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e)                                                               \
  do {                                                                         \
    if (!(e)) {                                                                \
      fprintf(stderr, "rules-probe:%d: %s\n", __LINE__, #e);                   \
      return false;                                                            \
    }                                                                          \
  } while (0)
static unsigned stopped;
static bool reference_fragment;
static bool probe_rule(FzeroRule rule) {
  const FzeroCourse *course = FzeroTracksCurrentCourse();
  unsigned feature = rule == FZERO_RULE_DMAG ? FZERO_COURSE_GRIP_MAGNETS
                     : rule == FZERO_RULE_UP_MAGNET ? FZERO_COURSE_UP_MAGNETS
                     : rule == FZERO_RULE_RAINBOW ? FZERO_COURSE_RAINBOW : 0;
  if (rule == FZERO_RULE_DMAG || rule == FZERO_RULE_UP_MAGNET)
    return course && (course->required & feature);
  return FzeroRuleEnabled(rule) || (course && (course->required & feature));
}
static void stop(CpuState *cpu, uint32_t pc) {
  (void)cpu;
  stopped = pc;
  interp_bridge_pre_opcode_redirect(0x7e2000);
}
static void word(unsigned a, unsigned v) {
  g_ram[a] = (uint8_t)v;
  g_ram[a + 1] = (uint8_t)(v >> 8);
}
static unsigned read_word(unsigned a) {
  return g_ram[a] | (unsigned)g_ram[a + 1] << 8;
}
static CpuState state(unsigned p) {
  CpuState c = g_cpu;
  c.A = c.X = c.Y = c.D = c.DB = c.PB = 0;
  c.S = 0x1fff;
  c.P = (uint8_t)p;
  c.emulation = 0;
  cpu_p_to_mirrors(&c);
  return c;
}
static bool fragment(CpuState *c, unsigned start, unsigned end,
                     unsigned alternate, bool long_return) {
  stopped = 0;
  if (!reference_fragment) {
    FzeroTracksInstallHooks();
    FzeroGameplayInstallHooks();
  }
  if (end)
    interp_bridge_set_pre_opcode_hook(end, stop);
  if (alternate)
    interp_bridge_set_pre_opcode_hook(alternate, stop);
  g_ram[0x2000] = long_return ? 0x6b : 0x60;
  if (long_return)
    cpu_push_jsl_return_frame(c);
  else
    cpu_push_jsr_return_frame(c);
  CHECK(interp_bridge_run(c, start));
  CHECK(c->S == 0x1fff);
  CHECK(!end || stopped == end || (alternate && stopped == alternate));
  interp_bridge_set_pre_opcode_hook(0, NULL);
  return true;
}
/* Production never installs global terrain patches. For oracle comparison,
 * apply the independently assembled author's bytes only around this fragment,
 * then restore every overwritten byte (including overlapping vehicle tuning). */
static bool author_fragment(CpuState *c, unsigned patch, unsigned start, unsigned end) {
  const RulePatch *p = &rule_patches[FzeroDeluxeActive()][patch];
  size_t count = 0;
  for (unsigned i = 0; i < p->count; ++i) {
    CHECK(p->writes[i].offset + p->writes[i].size <= (unsigned)g_snes->cart->romSize);
    count += p->writes[i].size;
  }
  uint8_t *saved = malloc(count);
  CHECK(saved);
  size_t pos = 0;
  for (unsigned i = 0; i < p->count; ++i) {
    const RuleWrite *w = &p->writes[i];
    memcpy(saved + pos, g_snes->cart->rom + w->offset, w->size);
    memcpy(g_snes->cart->rom + w->offset, w->data, w->size);
    pos += w->size;
  }
  reference_fragment = true;
  bool ok = fragment(c, start, end, 0, false);
  reference_fragment = false;
  pos = 0;
  for (unsigned i = 0; i < p->count; ++i) {
    const RuleWrite *w = &p->writes[i];
    memcpy(g_snes->cart->rom + w->offset, saved + pos, w->size);
    pos += w->size;
  }
  free(saved);
  return ok;
}
static bool magnet_damage_probe(void) {
  bool cgp = probe_rule(FZERO_RULE_DMAG);
  unsigned cases = 0;
  for (unsigned actor = 0; actor <= 2; actor += 2)
    for (unsigned airborne = 0; airborne < 2; ++airborne)
      for (unsigned flags = 0; flags <= 12; flags += 4) {
        CpuState c = state(0x30);
        c.X = (uint16_t)actor;
        g_ram[0xc3] = g_ram[0xc8] = g_ram[0x52] = 0;
        g_ram[0xd50 + actor] = (uint8_t)flags;
        g_ram[0xd51 + actor] = airborne ? 128 : 0;
        word(0xcc0 + actor, 0);
        word(0xc9, 0x800);
        word(0xb20 + actor, 0x800);
        g_ram[0xd90 + actor] = 0;
        CHECK(fragment(&c, 0x0098a4, 0, 0, false));
        bool damage = !airborne && (flags & (cgp ? 4 : 12));
        CHECK(actor ? ((read_word(0xb20 + actor) < 0x800) == damage)
                    : ((read_word(0xc9) < 0x800) == damage));
        ++cases;
      }
  /* Exercise real decoded terrain, not just hand-written surface flags. Only
   * isolate pure magnet surfaces here; pits, walls and barriers have separate
   * damage semantics and are covered by the landing/recovery probes. */
  const FzeroCourse *course = FzeroTracksCurrentCourse();
  unsigned authored = 0;
  if (course)
    for (unsigned tile = 0; tile < 256; ++tile) {
      if (!(course->terrain[0x100 + tile] & 12) || course->terrain[0x300 + tile]) continue;
      CpuState c = state(0x30);
      g_ram[0xcd0] = (uint8_t)tile;
      CHECK(fragment(&c, 0x008e36, 0x008e28, 0, false));
      unsigned surface = g_ram[0xd50];
      CHECK(surface == course->terrain[0x100 + tile]);
      g_ram[0xc3] = g_ram[0xc8] = g_ram[0x52] = g_ram[0xd51] = 0;
      word(0xc9, 0x800);
      word(0xb20, 0x800);
      c = state(0x30);
      CHECK(fragment(&c, 0x0098a4, 0, 0, false));
      CHECK((read_word(0xc9) < 0x800) == ((surface & (cgp ? 4 : 12)) != 0));
      if (tile == 0xa6)
        fprintf(stderr, "rules-probe: tile=A6 surface=%02x energy=%04x PASS\n", surface, read_word(0xc9));
      ++authored;
    }
  fprintf(stderr, "rules-probe: magnet damage=%u authored-tiles=%u semantics=%s PASS\n",
          cases, authored, cgp ? "course-declared" : "native");
  return true;
}
static bool rules_probe(void) {
  static const DispatchEntry empty[1] = {{0}};
  cpu_select_program(empty, 0, NULL, 0);
  interp_bridge_set_pre_opcode_hook(0, NULL);
  CHECK(magnet_damage_probe());
  if (FzeroTracksCurrentCourse()) {
    /* Exercise actual landing/recovery instruction boundaries for every tile
     * and both jump-state variants. Valid custom tiles above D0 must survive;
     * pits below D0 must remain fatal. Check both engine modules. */
    const FzeroCourse *course = FzeroTracksCurrentCourse();
    unsigned custom_safe = 0, custom_pit = 0;
    for (unsigned tile = 0; tile < 256; ++tile) {
      for (unsigned jumping = 0; jumping < 2; ++jumping) {
        CpuState c = state(0x10);
        unsigned flags = course->terrain[0x300 + tile];
        unsigned fatal = (flags & 0x80) || (!jumping && (flags & 0x20));
        g_ram[0xcd0] = (uint8_t)tile;
        g_ram[0xc00] = (uint8_t)flags;
        g_ram[0xd51] = (uint8_t)jumping;
        CHECK(fragment(&c, 0x009c9a, 0x009cc1, 0x009caf, false));
        CHECK(stopped == (fatal ? 0x009caf : 0x009cc1));
        custom_safe += tile >= 0xd0 && !fatal;
        custom_pit += tile < 0xd0 && (flags & 0x80);
      }
      for (unsigned deep = 0; deep < 2; ++deep) {
        CpuState c = state(0x30);
        g_ram[0xcd0] = (uint8_t)tile;
        g_ram[0xc01] = deep ? 0xff : 0;
        CHECK(fragment(&c, 0x00eb90, 0x00eb97, 0x00ebad, false));
        CHECK(stopped == (deep ? 0x00eb97 : 0x00ebad));
      }
    }
    for (unsigned tile = 0; tile < 256; ++tile) {
      unsigned flags = course->terrain[0x300 + tile];
      if (!(flags & 0x40) || (flags & 0x80)) continue;
      CpuState c = state(0x30);
      g_ram[0xcd0] = (uint8_t)tile;
      word(0xb20, 0);
      word(0xcc0, 0);
      CHECK(fragment(&c, 0x008e36, 0x008e28, 0, false));
      CHECK(read_word(0xb20) == 0x80 && read_word(0xc00) == 0x340);
    }
    fprintf(stderr, "rules-probe: course landings/recovery=1024 safe-high=%u low-pits=%u PASS\n",
            custom_safe, custom_pit);
  }
  if (FzeroRuleEnabled(FZERO_RULE_BOOST)) {
    static const unsigned drains[3][4] = {
        {23, 21, 19, 23}, {22, 21, 23, 20}, {22, 17, 22, 21}};
    unsigned profile = FzeroGameplaySettingsCurrent()->boost,
             bank = 0x33 + profile, drain = 0;
    const uint8_t pattern[] = {0xad, 0x51, 0x0d, 0x29, 0x38, 0xc9, 0x08};
    for (unsigned at = 0x8000; at < 0x8400; ++at) {
      unsigned n = 0;
      while (n < sizeof(pattern) &&
             cpu_read8(&g_cpu, (uint8_t)bank, (uint16_t)(at + n)) == pattern[n])
        ++n;
      if (n == sizeof(pattern)) {
        CHECK(!drain);
        drain = (bank << 16) | at;
      }
    }
    CHECK(drain);
    unsigned end = 0;
    for (unsigned at = drain; at < drain + 0x100; ++at)
      if (cpu_read8(&g_cpu, (uint8_t)bank, (uint16_t)at) == 0x22 &&
          cpu_read8(&g_cpu, (uint8_t)bank, (uint16_t)(at + 1)) == 0x91 &&
          cpu_read8(&g_cpu, (uint8_t)bank, (uint16_t)(at + 2)) == 0x87) {
        end = at;
        break;
      }
    CHECK(end);
    for (unsigned car = 0; car < (FzeroBsCars() ? 8u : 4u); ++car) {
      CpuState c = state(0x30);
      g_ram[0x52] = (uint8_t)car;
      g_ram[0x53] = 3;
      g_ram[0xd51] = 8;
      g_ram[0xd40] = 1;
      g_ram[0xee6] = 0;
      g_ram[0xcf4] = 8;
      word(0xc9, 1000);
      CHECK(fragment(&c, drain, end, 0, false));
      CHECK(read_word(0xc9) == 1000 - drains[profile][car & 3]);
      c = state(0x30);
      g_ram[0xcf3] = 3;
      CHECK(fragment(&c, 0x00b82f, 0x00b835, 0, false));
      CHECK(g_ram[0xcf4] == 150 && g_ram[0xcf3] == 2);
    }
  }
  if (FzeroRuleEnabled(FZERO_RULE_TUNING) &&
      !FzeroRuleEnabled(FZERO_RULE_LEGEND)) {
    uint8_t values[4 * 29 * 2], hash[32];
    unsigned n = 0;
    for (unsigned car = 0; car < 4; ++car)
      for (unsigned speed = 0; speed < 29; ++speed)
        for (unsigned actor = 0; actor <= 2; actor += 2) {
          CpuState c = state(0x30);
          c.X = (uint16_t)actor;
          c.A = (uint16_t)speed;
          g_ram[0x52] = (uint8_t)car;
          g_ram[0xd71 + actor] =
              (uint8_t)(FzeroDeluxeActive() ? car : car * 29);
          CHECK(fragment(&c, 0x0094ce, 0x0094d7, 0, false));
          CHECK(c.Y == actor);
          values[n++] = (uint8_t)c.A;
        }
    sha256_compute(values, sizeof(values), hash);
    fputs("rules-probe: acceleration=", stderr);
    for (unsigned i = 0; i < 32; ++i)
      fprintf(stderr, "%02x", hash[i]);
    fputc('\n', stderr);
  }
  if (FzeroRuleEnabled(FZERO_RULE_EXHAUST)) {
    uint8_t sprites[4 * 13 * 17], hash[32];
    unsigned n = 0;
    for (unsigned car = 0; car < 4; ++car)
      for (unsigned frame = 0; frame < 13; ++frame) {
        CpuState c = state(0x30);
        c.Y = (uint16_t)frame;
        g_ram[0x52] = (uint8_t)car;
        word(0x20, 0x2020);
        memset(g_ram + 0x2f0, 0, 16);
        g_ram[0x40f] = 0;
        unsigned entry =
            FzeroDeluxeActive() ? 0x1eadde
            : car ? cpu_read16(&c, 0, (uint16_t)(0xbe98 + 2 * (car - 1)))
                  : 0xc68c;
        CHECK(fragment(&c, entry, 0, 0, FzeroDeluxeActive()));
        memcpy(sprites + n, g_ram + 0x2f0, 16);
        n += 16;
        sprites[n++] = g_ram[0x40f];
      }
    sha256_compute(sprites, sizeof(sprites), hash);
    fputs("rules-probe: exhaust=", stderr);
    for (unsigned i = 0; i < 32; ++i)
      fprintf(stderr, "%02x", hash[i]);
    fputc('\n', stderr);
  }
  if (FzeroRuleEnabled(FZERO_RULE_LAPS)) {
    g_ram[0xad] = 42;
    const unsigned checkpoints[] = {0, 42, 20};
    for (unsigned i = 0; i < 3; ++i) {
      CpuState c = state(0x30);
      g_ram[0xd00] = (uint8_t)checkpoints[i];
      CHECK(fragment(&c, 0x009968, 0x00996d, 0x0099bc, false));
      CHECK(stopped == (i == 2 ? 0x0099bc : 0x00996d));
    }
    CpuState c = state(0x30);
    c.X = 2;
    CHECK(fragment(&c, 0x009968, 0x00996d, 0x0099bc, false));
    CHECK(stopped == 0x00996d);
  }
  if (FzeroRuleEnabled(FZERO_RULE_DASH)) {
    CpuState c = state(0x31);
    g_ram[0xbd1] = 50;
    g_ram[0x109d] = 20;
    g_ram[0xbe1] = 90;
    CHECK(fragment(&c, 0x009829, 0x00982e, 0, false));
    CHECK((c.A & 255) == 30);
    for (unsigned air = 0; air < 2; ++air) {
      c = state(0x30);
      g_ram[0xd51] = (uint8_t)(air * 128);
      g_ram[0xbe1] = 90;
      CHECK(fragment(&c, 0x009842, 0x009847, 0x00986c, false));
      CHECK(g_ram[0xbe1] == (air ? 90 : 20));
    }
  }
  if (FzeroRuleEnabled(FZERO_RULE_CPU_SPIN)) {
    CpuState c = state(0x10);
    c.Y = 2;
    word(0xb22, 0x800);
    word(0x15, 0);
    CHECK(fragment(&c, 0x00bd1a, 0x00bd1f, 0, false));
    CHECK(read_word(0xc22) == 0x8300 && c.m_flag);
  }
  if (FzeroRuleEnabled(FZERO_RULE_BOUNCE)) {
    CpuState c = state(0x30);
    c.A = 99;
    CHECK(fragment(&c, 0x00bc6d, 0x00bc72, 0, false));
    CHECK((c.A & 255) == 10);
  }
  if (probe_rule(FZERO_RULE_DMAG)) {
    /* Every damage-mask input is compared with the independently assembled
     * source. Keeping the damage bit clear must not mask a genuine pit. */
    static const unsigned masks[] = {0x0098b1,0x0098bc,0x0098ce,0x0098f1,0x0098f9};
    if (probe_rule(FZERO_RULE_DMAG))
      for (unsigned site = 0; site < sizeof(masks)/sizeof(*masks); ++site)
        for (unsigned flags = 0; flags < 256; ++flags) {
          CpuState actual = state(0x30), original = state(0x30);
          actual.A = original.A = (uint16_t)flags;
          CHECK(fragment(&actual, masks[site], masks[site]+2, 0, false));
          CHECK(author_fragment(&original, 16, masks[site], masks[site]+2));
          CHECK(actual.A == original.A && actual._flag_Z == original._flag_Z &&
                actual._flag_N == original._flag_N);
        }
    CpuState c = state(0x30);
    g_ram[0xd50] = 8;
    g_ram[0xd51] = 0;
    CHECK(fragment(&c, 0x00b890, 0x00b8b1, 0, false));
    CHECK((c.A & 255) == 0x38);
    c = state(0x30);
    CHECK(fragment(&c, 0x00b8e6, 0x00b8f3, 0, false));
    CHECK((c.A & 255) == 0x50);
    c = state(0x30);
    word(0x17, 0);
    CHECK(fragment(&c, FzeroDeluxeActive() ? 0x1eac6d : 0x009292,
                   FzeroDeluxeActive() ? 0x1eacc6 : 0x0092f9, 0,
                   FzeroDeluxeActive()));
    CHECK(read_word(0x17) == 0x220);
    fprintf(stderr, "rules-probe: grounded grip, turning, strafe and damage masks PASS\n");
  }
  if (probe_rule(FZERO_RULE_UP_MAGNET)) {
    static const unsigned landing_heights[] = {0,0x3ff,0x400,0x800,0x7fff,0xbfff,0xc000,0xffff};
    for (unsigned i = 0; i < sizeof(landing_heights)/sizeof(*landing_heights); ++i) {
      CpuState c = state(0x10);
      c.A = (uint16_t)landing_heights[i];
      word(0x29, 0x400);
      CHECK(fragment(&c, 0x009c55, 0x009c8c, 0x009c5b, false));
      CHECK(stopped == ((landing_heights[i] < 0x400 || landing_heights[i] >= 0xc000)
                            ? 0x009c8c : 0x009c5b));
    }
    static const unsigned tiles[] = {0xb5, 0xb6, 0xcc, 0xcd, 0xcf, 0xd0};
    static const unsigned heights[] = {0, 0x100, 0x6f00, 0x7000, 0x8000};
    static const unsigned velocities[] = {0, 0x200, 0xff00, 0x8000};
    unsigned cases = 0;
    for (unsigned actor = 0; actor <= 2; actor += 2)
      for (unsigned tilt = 0; tilt < 3; ++tilt)
        for (unsigned t = 0; t < sizeof(tiles)/sizeof(*tiles); ++t)
          for (unsigned h = 0; h < sizeof(heights)/sizeof(*heights); ++h)
            for (unsigned v = 0; v < sizeof(velocities)/sizeof(*velocities); ++v) {
              CpuState c = state(0x10);
              c.X = (uint16_t)actor;
              word(0xcd0 + actor, tiles[t]);
              word(0xbc0 + actor, heights[h]);
              word(0xbb0 + actor, velocities[v]);
              word(0x14, 0x18);
              g_ram[0xb10] = (uint8_t)(tilt * 4);
              CHECK(fragment(&c, 0x009c6b, 0x009c71, 0, false));
              if (probe_rule(FZERO_RULE_UP_MAGNET)) {
                /* The independently assembled author ASM is our oracle.
                 * Required-only runs use the same host hook without copying
                 * the global patch over unrelated native courses. */
                CpuState original = state(0x10);
                original.X = (uint16_t)actor;
                CHECK(author_fragment(&original, 22, 0x009c6b, 0x009c71));
                if (original.A != c.A || original._flag_C != c._flag_C) {
                  fprintf(stderr, "magnet mismatch actor=%u tilt=%u tile=%x height=%x velocity=%x host=%x/%u asm=%x/%u\n",
                          actor,tilt,tiles[t],heights[h],velocities[v],c.A,c._flag_C,original.A,original._flag_C);
                  return false;
                }
              }
              ++cases;
            }
    fprintf(stderr, "rules-probe: up-magnet cases=%u%s PASS\n", cases,
            " ASM parity");
  } else {
    /* CGP-specific tile IDs must still pull downward on unrelated courses,
     * even when both magnet switches were enabled by the CGP preset. */
    static const unsigned tiles[] = {0xb5, 0xb6, 0xcc, 0xcd, 0xcf, 0xd0};
    static const unsigned velocities[] = {0, 0x200, 0xff00, 0x8000};
    unsigned cases = 0;
    for (unsigned actor = 0; actor <= 2; actor += 2)
      for (unsigned t = 0; t < sizeof(tiles)/sizeof(*tiles); ++t)
        for (unsigned v = 0; v < sizeof(velocities)/sizeof(*velocities); ++v) {
          CpuState c = state(0x10); /* Native caller clears carry. */
          c.X = (uint16_t)actor;
          word(0xcd0 + actor, tiles[t]);
          word(0xbb0 + actor, velocities[v]);
          CHECK(fragment(&c, 0x009c6b, 0x009c71, 0, false));
          CHECK(c.A == ((velocities[v] - 0x61) & 65535));
          CHECK(!!c._flag_C == (velocities[v] >= 0x61));
          ++cases;
        }
    fprintf(stderr, "rules-probe: native downpull cases=%u PASS\n", cases);
  }
  if (probe_rule(FZERO_RULE_RAINBOW)) {
    for (unsigned rainbow = 0; rainbow < 2; ++rainbow) {
      CpuState c = state(0x30);
      g_ram[0xadf] = rainbow ? 255 : 0;
      g_ram[0x14] = 0x18;
      CHECK(fragment(&c, 0x0091e6, 0x0091eb, 0, false));
      CHECK(g_ram[0x14] == (rainbow ? 0x38 : 0x18));
    }
  }
  {
    const FzeroCourse *course = FzeroTracksCurrentCourse();
    unsigned features = course ? course->required : 0;
    if (features & (FZERO_COURSE_RAINBOW | FZERO_COURSE_FALL_DOWN)) {
      CpuState c = state(0x30);
      g_ram[0xadf] = 255;
      g_ram[0x14] = 0x18;
      CHECK(fragment(&c, 0x0091e6, 0x0091eb, 0, false));
      CHECK(g_ram[0x14] == (features & FZERO_COURSE_RAINBOW ? 0x38 : 0x18));
      for (unsigned flags = 0; flags < 256; ++flags) {
        c = state(0x30);
        g_ram[0] = (uint8_t)flags;
        g_ram[0xc3] = g_ram[0xcf] = g_ram[0xf38] = 0;
        g_ram[0xd40] = 3;
        CHECK(fragment(&c, 0x0098f5, 0x009967, 0x00992d, false));
        CHECK(stopped == ((flags & 0x8c) && !(flags & 128) ? 0x00992d : 0x009967));
        CHECK(g_ram[0xc3] == (flags & 128 ? 0x40 : 0));
        CHECK(g_ram[0xcf] == (flags & 128 ? 6 : 0));
        CHECK(g_ram[0xf38] == (flags & 128 ? 3 : 0));
        c = state(0x30); g_ram[0xcc0] = (uint8_t)flags;
        CHECK(fragment(&c, 0x00ba20, 0x00ba25, 0, false));
        CHECK(!(c.A & 255));
      }
      c = state(0x30); c.X = 0;
      CHECK(fragment(&c, 0x00e70b, 0x00e716, 0, false));
      c = state(0x30); c.X = 2; g_ram[0xcc2] = 0;
      CHECK(fragment(&c, 0x00e70b, 0x00e710, 0, false));
      fprintf(stderr, "rules-probe: fall-down collision flags=256, wall spin/bounce, gravity=%s PASS\n",
              features & FZERO_COURSE_RAINBOW ? "high" : "stock");
    }
  }
  if (FzeroRuleEnabled(FZERO_RULE_LEGEND)) {
    /* Launch acceleration must match that vehicle, even with Legend active.
     * D71 changes to a shared-table index only at the donor's handoff sites. */
    for (unsigned car = 0; car < (FzeroBsCars() ? 8u : 4u); ++car) {
      unsigned stride = FzeroRuleEnabled(FZERO_RULE_TUNING) ? 29 : 19;
      unsigned index = FzeroDeluxeActive() ? car : car * stride;
      for (unsigned speed = 0; speed < 18; ++speed) {
        unsigned values[2];
        for (unsigned actor = 0; actor <= 2; actor += 2) {
          CpuState c = state(0x30);
          c.X = (uint16_t)actor;
          c.A = (uint16_t)speed;
          g_ram[0x52] = (uint8_t)car;
          g_ram[0xd71 + actor] = (uint8_t)index;
          CHECK(fragment(&c, 0x0094ce, 0x0094d7, 0, false));
          CHECK(c.Y == actor);
          values[actor / 2] = c.A & 255;
        }
        CHECK(values[0] == values[1]);
      }
    }
    for (unsigned index = 0x94; index <= 0x9a; ++index)
      for (unsigned speed = 0; speed < 32; ++speed) {
        CpuState c = state(0x30);
        c.X = 2;
        c.A = (uint16_t)speed;
        g_ram[0xd73] = (uint8_t)index;
        unsigned offset = index - 0x94 + speed;
        unsigned expected = cpu_read8(&c, 2, (uint16_t)(0xcad6 + (offset < 29 ? offset : 28)));
        CHECK(fragment(&c, 0x0094ce, 0x0094d7, 0, false));
        CHECK(c.Y == 2 && (c.A & 255) == expected);
      }
    for (unsigned level = 0; level < 5; ++level) {
      CpuState c = state(0x30);
      c.X = 2;
      g_ram[0x57] = (uint8_t)level;
      g_ram[0xd73] = 0;
      CHECK(fragment(&c, 0x00e334, 0x00e339, 0, false));
      CHECK(g_ram[0xd73] == 0x9a - level);
    }
    /* Exercise the actual movement clamp with X=0 and non-boosting state. */
    {
      CpuState c = state(0x30);
      word(0xd51, 0);
      word(8, 12);
      word(12, 0xfff4);
      CHECK(fragment(&c, 0x0096f2, 0x00973c, 0, false));
      CHECK(read_word(8) == 12 && read_word(12) == 0xfff4);
    }
    fprintf(stderr, "rules-probe: Legend per-vehicle starts, handoff, shared indices and movement range PASS\n");
    for (unsigned rival = 0; rival < 256; ++rival) {
      CpuState c = state(0x30);
      c.X = (uint16_t)rival;
      CHECK(fragment(&c, 0x00d54c, 0x00d55d, 0, false));
      CHECK(g_ram[0x57] < 5);
    }
    for (unsigned level = 0; level < 5; ++level) {
      CpuState c = state(0x30);
      g_ram[0x57] = (uint8_t)level;
      g_ram[0xf3d] = 255;
      CHECK(fragment(&c, FzeroDeluxeActive() ? 0x1ec835 : 0x038841,
                     FzeroDeluxeActive() ? 0x1ec84a : 0x03884a, 0, false));
      CHECK(g_ram[0x59] == 7 - level && !g_ram[0xf3d] && g_ram[0x57] == level);
    }
  }
  if (FzeroDeluxeActive() && !FzeroBsTracks()) {
    for (unsigned track = 15; track < 25; ++track) {
      CpuState c = state(0);
      c.A = (uint16_t)track;
      CHECK(fragment(&c, 0x1eb805, 0, 0, false));
      CHECK(c._flag_Z);
    }
  }
  fprintf(stderr, "rules-probe: PASS (guest instructions, native return "
                  "stacks, conditional effects)\n");
  return true;
}

bool FzeroRulesProbe(void) {
  /* Oracle snippets use spare banks even when all optional rules are off.
   * Only this terminal probe gets a mirrored scratch image; real gameplay
   * retains its original cartridge and no global terrain ASM is installed. */
  Cart *cart = g_snes->cart;
  uint8_t *original = cart->rom;
  uint32_t size = cart->romSize;
  if (size >= 0x400000) return rules_probe();
  CHECK(size && cart->type == CART_LOROM);
  uint8_t *scratch = malloc(0x400000);
  CHECK(scratch);
  for (unsigned i = 0; i < 0x400000; ++i) scratch[i] = original[i % size];
  const uint8_t *previous_rom = g_rom;
  cart->rom = scratch;
  cart->romSize = 0x400000;
  g_rom = scratch;
  bool ok = rules_probe();
  cart->rom = original;
  cart->romSize = size;
  g_rom = previous_rom;
  free(scratch);
  return ok;
}

bool FzeroPracticeProbe(const char *path) {
  CHECK(g_ram[0x58] && g_ram[0xcf2] < 4);
  uint8_t values[58];
  for (unsigned who = 0; who < 2; ++who) {
    unsigned actor = who ? 4 : 0;
    g_ram[0xd71 + actor] = who ? g_ram[0xcf2] : g_ram[0x52];
    for (unsigned speed = 0; speed < 29; ++speed) {
      CpuState c = state(0x30);
      c.X = (uint16_t)actor;
      c.A = (uint16_t)speed;
      CHECK(fragment(&c, 0x1eacf0, 0x1ead13, 0, false));
      values[who * 29 + speed] = (uint8_t)c.A;
    }
  }
  FILE *f = fopen(path, "wb");
  CHECK(f);
  bool ok = fwrite(values, 1, sizeof(values), f) == sizeof(values);
  if (fclose(f)) ok = false;
  CHECK(ok);
  return true;
}
