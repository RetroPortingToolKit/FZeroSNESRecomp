#include "fzero_course.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                   \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)
static uint8_t rom[0x40000];
static unsigned next = 0x1000;
static unsigned addr(unsigned off) {
  return (off / 0x8000) * 0x10000 + 0x8000 + off % 0x8000;
}
static unsigned off(unsigned a) {
  return ((a & 0x7f0000) >> 1) | (a & 0x7fff);
}
static void word(unsigned o, unsigned n) {
  rom[o] = (uint8_t)n;
  rom[o + 1] = (uint8_t)(n >> 8);
}
static void pointer(unsigned o, unsigned n) {
  word(o, n);
  rom[o + 2] = (uint8_t)(n >> 16);
}
static unsigned allocate(unsigned n) {
  unsigned result = next;
  next += n;
  CHECK(next < sizeof(rom));
  return result;
}
static unsigned resource(unsigned size) {
  unsigned t = allocate(3), data = allocate(size);
  pointer(t, addr(data));
  return addr(t);
}
static void requirements(void) {
  const char *path = "course-requirements-test.layout";
  const char *cases[] = {
      "require=all|grip-magnets\nrequire=0|fall-down\nrequire=all|soft-horizon-fog\nrequire=1|up-magnets\nrequire=1|rainbow-road\nmusic=108000\nspc=1|mute-city\nintro_glyph=a7|0f8da2|0f8db2\nmsu_source=astra-front\nmsu=1|10\n",
      "require=all|unknown\n", "require=2|up-magnets\n",
      "require=128|up-magnets\n", "require=-1|up-magnets\n",
      "require=+1|up-magnets\n", "require=|up-magnets\n",
      "require=all|up-magnets\nrequire=all|up-magnets\n",
      "require=1|up-magnets\nrequire=1|up-magnets\n",
      "music=0\n", "music=108000\nmusic=108001\n",
      "spc=2|mute-city\n", "spc=-1|mute-city\n", "spc=|mute-city\n",
      "spc=128|mute-city\n", "spc=0|unknown\n", "spc=0|mute-city|big-blue\n",
      "spc=0|mute-city\nspc=0|big-blue\n",
      "intro_glyph=a7|7e8000|0f8db2\n", "intro_glyph=a7|0f8da2\n",
      "intro_glyph=81|0f8da2|0f8db2\n", "intro_glyph=a7|0f8da2|0f8db2|junk\n",
      "intro_glyph=a7|0f8da2|0f8db2\nintro_glyph=a7|0f8da2|0f8db2\n",
      "msu=1|10\n", "msu_source=Astra\n", "msu_source=../Astra\nmsu=1|10\n",
      "msu_source=C:\\Astra\nmsu=1|10\n", "msu_source=Astra.\nmsu=1|10\n",
      "msu_source=Astra\nmsu=1|0\n", "msu_source=Astra\nmsu=1|256\n",
      "msu_source=Astra\nmsu=2|10\n", "msu_source=Astra\nmsu=128|10\n",
      "msu_source=Astra\nmsu=1|-1\n", "msu_source=Astra\nmsu=1|10|x\n",
      "msu_source=Astra\nmsu=1|10\nmsu=1|11\n"};
  const char *fields[] = {"pools", "settings", "palettes", "maps", "graphics", "paths",
                         "names", "sky_graphics", "sky_back", "sky_front", "minimaps",
                         "map_positions", "terrain", "gradients", "opponents", "shortcuts"};
  for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
    FILE *f = fopen(path, "wb");
    CHECK(f);
    fprintf(f, "format=fzero-course-1\ncount=2\n%s", cases[i]);
    for (unsigned j = 0; j < sizeof(fields) / sizeof(*fields); ++j)
      fprintf(f, "%s=108000\n", fields[j]);
    CHECK(!fclose(f));
    FzeroCourseLayout layout = {0}, before = layout;
    char error[256];
    bool valid = FzeroCourseLayoutRead(path, &layout, error, sizeof(error));
    CHECK(valid == (i == 0));
    if (valid) {
      CHECK(layout.required == (FZERO_COURSE_GRIP_MAGNETS | FZERO_COURSE_SOFT_HORIZON_FOG));
      CHECK(layout.course_required[0] == FZERO_COURSE_FALL_DOWN);
      CHECK(layout.course_required[1] == (FZERO_COURSE_UP_MAGNETS | FZERO_COURSE_RAINBOW));
      CHECK(layout.music == 0x108000 && !layout.spc_override[0] && layout.spc_override[1] == 1);
      CHECK(layout.intro_glyph_count == 1 && layout.intro_glyphs[0].code == 0xa7);
      CHECK(layout.intro_glyphs[0].top == 0x0f8da2 && layout.intro_glyphs[0].bottom == 0x0f8db2);
      CHECK(!strcmp(layout.msu_source, "astra-front") && layout.msu_tracks[1] == 10 && !layout.msu_tracks[0]);
    } else {
      CHECK(!memcmp(&layout, &before, sizeof(layout)));
      if (i == 1) CHECK(strstr(error, "Unsupported required"));
    }
  }
  CHECK(!remove(path));
}
int main(void) {
  requirements();
  FzeroCourseLayout l = {0};
  l.count = 1;
  l.pools = resource(0x2400);
  l.palettes = resource(0xe0);
  l.sky_graphics = resource(0x2000);
  l.sky_back = resource(0x700);
  l.sky_front = resource(0x540);
  l.terrain = resource(0x400);
  l.minimaps = resource(0x200); /* This format uses a bank-relative offset. */
  rom[off(l.minimaps) + 1] &= 0x7f;
  l.settings = addr(allocate(1));
  rom[off(l.settings)] = 0xc7;
  l.gradients = addr(allocate(1));
  l.map_positions = addr(allocate(4));
  l.opponents = addr(allocate(3));
  unsigned maps = allocate(10), blocks = allocate(16), grid = allocate(16);
  l.maps = addr(maps);
  pointer(maps, addr(blocks));
  word(maps + 3, 1);
  pointer(maps + 5, addr(grid));
  word(maps + 8, 1);
  word(grid, 0x123);
  l.graphics = resource(256 * 33);
  l.names = resource(128);
  l.shortcuts = resource(2);
  unsigned shortcut = off(l.shortcuts);
  unsigned sd = rom[shortcut] | rom[shortcut + 1] << 8 | rom[shortcut + 2] << 16;
  word(off(sd), 0xffff);
  /* Keep same-bank path records, pointers and arrays together. */
  next = 0x10000;
  l.paths = resource(18);
  unsigned t = off(l.paths), segments = off(rom[t] | rom[t + 1] << 8 | rom[t + 2] << 16);
  unsigned ptrs = allocate(12), arrays = allocate(6);
  rom[segments] = 1;
  word(segments + 1, addr(ptrs) & 65535);
  word(segments + 5, 100);
  word(segments + 7, 200);
  for (unsigned i = 0; i < 6; ++i)
    word(ptrs + i * 2, addr(arrays + i) & 65535);
  rom[arrays] = 1;
  rom[arrays + 1] = 255;
  rom[arrays + 2] = 32;
  FzeroCourse *c = malloc(sizeof(*c)), *previous = malloc(sizeof(*c));
  CHECK(c && previous);
  char error[256];
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(c->grid[0] == 0x48 && c->grid[1] == 0 && c->grid[16] == 3);
  CHECK(c->path[0] == 100 && c->path[2] == 108 && c->path[0x202] == 192);
  CHECK(c->has_pit && c->pit_checkpoint == 0); /* Checkpoint zero is a valid pit. */
  *previous = *c;
  /* Presentation overrides must preserve records and only load used letters. */
  unsigned nt = off(l.names), name_data = off(rom[nt] | rom[nt+1] << 8 | rom[nt+2] << 16);
  memset(rom + name_data, 1, 6);
  rom[name_data + 6] = 0xa7;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  *previous = *c;
  unsigned pixels = allocate(32);
  for (unsigned j=0; j<32; ++j) rom[pixels+j] = (uint8_t)(j+1);
  l.intro_glyph_count = 1;
  l.intro_glyphs[0].code = 0xa7;
  l.intro_glyphs[0].top = addr(pixels);
  l.intro_glyphs[0].bottom = addr(pixels + 16);
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(c->intro_glyph_count == 1 && c->intro_glyphs[0].code == 0xa7);
  CHECK(!memcmp(c->intro_glyphs[0].pixels, rom+pixels, 32));
  CHECK(!memcmp(c->hash, previous->hash, 32));
  l.intro_glyphs[0].code = 0xa6;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(!c->intro_glyph_count && !memcmp(c->hash, previous->hash, 32));
  l.intro_glyphs[0].bottom = 0x7e8000;
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  l.intro_glyph_count = 0;
  unsigned cycles = allocate(2), entries = allocate(8);
  l.palette_cycles = addr(cycles);
  word(cycles, addr(entries) & 65535);
  word(entries, 0x20);
  word(entries + 2, 0xf0);
  word(entries + 4, 0xffff);
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(c->has_palette_cycles && c->palette_cycle_count == 2);
  CHECK(memcmp(c->hash, previous->hash, 32));
  uint8_t colors[0xe0], before[0xe0];
  for (unsigned i = 0; i < sizeof(colors); ++i)
    before[i] = colors[i] = (uint8_t)i;
  FzeroCourseCyclePalette(c, colors);
  CHECK(colors[0] == 14 && colors[1] == 15 && colors[2] == 0);
  CHECK(colors[0xd0] == 0xde && colors[0xd2] == 0xd0);
  CHECK(!memcmp(colors + 0x10, before + 0x10, 0xc0));
  *previous = *c;
  l.required = FZERO_COURSE_GRIP_MAGNETS;
  l.course_required[0] = FZERO_COURSE_UP_MAGNETS;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(c->required == (FZERO_COURSE_GRIP_MAGNETS | FZERO_COURSE_UP_MAGNETS));
  CHECK(memcmp(c->hash, previous->hash, 32));
  l.course_required[0] = 128;
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  l.required = l.course_required[0] = 0;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(!memcmp(c->hash, previous->hash, 32));
  /* Presentation requirements retain record identity and restore stock data
   * when leaving the owning pack. Only the six authored bytes may change. */
  l.required = FZERO_COURSE_SOFT_HORIZON_FOG;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(!memcmp(c->hash, previous->hash, 32));
  uint8_t *fog_rom = malloc(0x80000);
  CHECK(fog_rom != NULL);
  memset(fog_rom, 0xaa, 0x80000);
  CHECK(!FzeroCourseApplyHorizonFog(c, NULL, 0x80000));
  CHECK(!FzeroCourseApplyHorizonFog(c, fog_rom, 0x46fce));
  CHECK(fog_rom[0x46f9f] == 0xaa);
  CHECK(FzeroCourseApplyHorizonFog(c, fog_rom, 0x80000));
  CHECK(!memcmp(fog_rom + 0x46f9f, "\x61\x63\x68", 3));
  CHECK(!memcmp(fog_rom + 0x46fcc, "\xe1\xe4\xe8", 3));
  CHECK(fog_rom[0x46f9e] == 0xaa && fog_rom[0x46fa2] == 0xaa);
  CHECK(fog_rom[0x46fcb] == 0xaa && fog_rom[0x46fcf] == 0xaa);
  CHECK(FzeroCourseApplyHorizonFog(NULL, fog_rom, 0x80000));
  CHECK(!memcmp(fog_rom + 0x46f9f, "\x6a\x6c\x6e", 3));
  CHECK(!memcmp(fog_rom + 0x46fcc, "\xe6\xe7\xe8", 3));
  free(fog_rom);
  l.required = 0;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  /* Audio must be bounded, can explicitly select song zero, and must never
   * strand existing course records by changing their gameplay key. */
  CHECK(!c->has_music);
  unsigned music = allocate(1);
  l.music = addr(music);
  for (unsigned song = 0; song < 10; ++song) {
    rom[music] = (uint8_t)(song * 9);
    CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
    CHECK(c->has_music && c->music == song * 9);
    CHECK(!memcmp(c->hash, previous->hash, 32));
  }
  rom[music] = 82;
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  rom[music] = 1;
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  l.music = 0x7e8000;
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  l.music = addr(music);
  rom[music] = 9;
  l.spc_override[0] = 1;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(c->has_music && !c->music && !memcmp(c->hash, previous->hash, 32));
  l.music = 0; /* Explicit mapping also works without a donor music table. */
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(c->has_music && !c->music);
  l.spc_override[0] = 11;
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  l.spc_override[0] = 0;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  l.msu_tracks[0] = 10;
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  strcpy(l.msu_source, "astra-front");
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(c->msu_track == 10 && !strcmp(c->msu_source, l.msu_source));
  CHECK(!memcmp(c->hash, previous->hash, 32));
  l.msu_tracks[0] = 0;
  CHECK(FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(!c->msu_track && !c->msu_source[0]);
  *previous = *c;
  word(entries, 0x10); /* Shared HUD/car palette is forbidden. */
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  word(entries, 0x21);
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  word(entries, 0xf0); /* Duplicates would rotate twice. */
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  word(entries, 0x20);
  word(entries + 4, 0x100);
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  word(entries + 4, 0xffff);
  word(cycles, 0x1234); /* RAM/MMIO pointer. */
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  word(cycles, addr(entries) & 65535);
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 1, c, error, sizeof(error)));
  word(maps + 3, 0xffff);
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  word(maps + 3, 1);
  pointer(off(l.pools), 0x7e8000);
  CHECK(!FzeroCourseExtract(rom, sizeof(rom), &l, 0, c, error, sizeof(error)));
  CHECK(
      !memcmp(c, previous, sizeof(*c))); /* Failed extraction never publishes partial resources. */
  free(c);
  free(previous);
  puts("Typed extraction, LoROM bounds, row expansion and checkpoint-zero pit passed");
  return 0;
}
