#include "fzero_course_file.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *e, size_t n, const char *s) {
  snprintf(e, n, "%s", s);
  return false;
}
static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put(uint8_t *p, unsigned n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
}
void FzeroCourseHash(FzeroCourse *c) {
  /* Existing record keys are deliberately preserved. All callers zero-init. */
  sha256_compute((const uint8_t *)c, offsetof(FzeroCourse, hash), c->hash);
  if (c->has_palette_cycles) {
    uint8_t key[48];
    memcpy(key, c->hash, 32);
    key[32] = 1;
    key[33] = c->palette_cycle_count;
    memcpy(key + 34, c->palette_cycles, 14);
    sha256_compute(key, sizeof(key), c->hash);
  }
  if (c->required & FZERO_COURSE_GAMEPLAY_FEATURES) {
    uint8_t key[34];
    memcpy(key, c->hash, 32);
    key[32] = 0xc1;
    key[33] = c->required & FZERO_COURSE_GAMEPLAY_FEATURES;
    sha256_compute(key, sizeof(key), c->hash);
  }
}
bool FzeroCourseValidate(const FzeroCourse *c, char *e, size_t cap) {
  if (c->block_size < 544 || c->block_size > sizeof(c->blocks) ||
      c->block_size % 16 || !c->grid_size || c->grid_size > sizeof(c->grid) ||
      c->grid_size % 18 || c->last_checkpoint > 253 ||
      c->finish_checkpoint > 254 || c->has_pit > 1 ||
      (c->has_pit && c->pit_checkpoint > 254) ||
      c->required & ~FZERO_COURSE_FEATURES || c->has_palette_cycles > 1 ||
      c->palette_cycle_count > 14 ||
      (!c->has_palette_cycles && c->palette_cycle_count) ||
      c->intro_glyph_count > 28 || c->has_music > 1 ||
      (c->has_music && (c->music > 81 || c->music % 9)) ||
      !memchr(c->name, 0, sizeof(c->name)) ||
      !memchr(c->msu_source, 0, sizeof(c->msu_source)))
    return fail(e, cap, "Invalid course resource bounds or metadata");
  bool shortcut_end = false;
  for (unsigned i = 0; i < 17; ++i)
    if (word(c->shortcuts + i * 17) & 0x8000) {
      shortcut_end = true;
      break;
    }
  if (!shortcut_end)
    return fail(e, cap, "Unterminated shortcuts");
  for (unsigned i = 0; i < c->intro_glyph_count; ++i) {
    unsigned code = c->intro_glyphs[i].code;
    if (!((code >= 0x64 && code <= 0x6f) || (code >= 0x8a && code <= 0x8f) ||
          (code >= 0xa0 && code <= 0xa9)))
      return fail(e, cap, "Unsupported intro glyph");
    for (unsigned j = 0; j < i; ++j)
      if (c->intro_glyphs[j].code == code)
        return fail(e, cap, "Duplicate intro glyph");
  }
  unsigned seen = 0;
  for (unsigned chunk = 0; chunk < 512; ++chunk) {
    unsigned block = 512 + (unsigned)c->blocks[chunk] * 32;
    if (block + 32 > c->block_size)
      return fail(e, cap, "Chunk references missing row pointers");
    for (unsigned row = 0; row < 16; ++row) {
      unsigned address = word(c->blocks + block + row * 2);
      if (address < 0x7000 || address - 0x7000 + 32 > c->grid_size)
        return fail(e, cap, "Row pointer leaves course layout");
      for (unsigned x = 0; x < 16; ++x) {
        unsigned tile = word(c->grid + address - 0x7000 + x * 2);
        if (tile + 4 > sizeof(c->pool) && !(tile >= 0x3ff0 && tile <= 0x3ffc))
          return fail(e, cap, "Map tile leaves course tile pool");
      }
    }
  }
  for (unsigned i = 0; i < c->palette_cycle_count; ++i) {
    unsigned p = c->palette_cycles[i];
    if (p > 0xd0 || p % 16 || (seen & (1u << (p / 16))))
      return fail(e, cap, "Invalid palette cycle");
    seen |= 1u << (p / 16);
  }
  return true;
}
/* Section IDs are append-only. Scalars are explicitly little endian. */
typedef struct Section {
  uint16_t id;
  size_t offset, size;
} Section;
#define FIELD(id, f)                                                           \
  {id, offsetof(FzeroCourse, f), sizeof(((FzeroCourse *)0)->f)}
static const Section sections[] = {
    FIELD(1, pool),       FIELD(2, blocks),     FIELD(3, grid),
    FIELD(4, graphics),   FIELD(5, palette),    FIELD(6, sky_graphics),
    FIELD(7, sky_back),   FIELD(8, sky_front),  FIELD(9, minimap),
    FIELD(10, terrain),   FIELD(11, name),      FIELD(12, path),
    FIELD(13, opponents), FIELD(14, shortcuts), FIELD(15, palette_cycles),
    FIELD(16, msu_source)};
#undef FIELD
static bool write_section(FILE *f, unsigned id, const void *p, size_t size) {
  uint8_t h[4];
  put(h, id);
  put(h + 2, (unsigned)size);
  return fwrite(h, 1, 4, f) == 4 && fwrite(p, 1, size, f) == size;
}
bool FzeroCourseFileWrite(const char *path, const FzeroCourse *c, char *e,
                          size_t cap) {
  if (!FzeroCourseValidate(c, e, cap))
    return false;
  FILE *f = fopen(path, "wb");
  if (!f)
    return fail(e, cap, "Cannot create course resource");
  bool ok = fwrite("FZCOURSE\1", 1, 9, f) == 9;
  for (unsigned i = 0; ok && i < sizeof(sections) / sizeof(*sections); ++i)
    ok = write_section(f, sections[i].id,
                       (const uint8_t *)c + sections[i].offset,
                       sections[i].size);
  uint8_t m[20] = {c->setting,
                   c->gradient,
                   0,
                   0,
                   0,
                   0,
                   c->last_checkpoint,
                   c->finish_checkpoint,
                   c->pit_checkpoint,
                   c->has_pit,
                   0,
                   0,
                   0,
                   0,
                   c->has_palette_cycles,
                   c->palette_cycle_count,
                   c->required,
                   c->has_music,
                   c->music,
                   c->msu_track};
  put(m + 2, c->map_x);
  put(m + 4, c->map_y);
  put(m + 10, c->block_size);
  put(m + 12, c->grid_size);
  ok = ok && write_section(f, 17, m, sizeof(m));
  uint8_t glyphs[1 + 28 * 33] = {0};
  glyphs[0] = (uint8_t)c->intro_glyph_count;
  for (unsigned i = 0; i < c->intro_glyph_count; ++i) {
    glyphs[1 + i * 33] = c->intro_glyphs[i].code;
    memcpy(glyphs + 2 + i * 33, c->intro_glyphs[i].pixels, 32);
  }
  ok = ok && write_section(f, 18, glyphs, sizeof(glyphs));
  if (fclose(f))
    ok = false;
  return ok ? true : fail(e, cap, "Cannot write course resource");
}
bool FzeroCourseFileRead(const char *path, FzeroCourse *out, char *e,
                         size_t cap) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return fail(e, cap, "Cannot open course resource");
  FzeroCourse *c = calloc(1, sizeof(*c));
  if (!c) {
    fclose(f);
    return fail(e, cap, "Out of memory");
  }
  uint8_t magic[9], m[20] = {0}, glyphs[925] = {0};
  unsigned seen = 0;
  bool ok = fread(magic, 1, 9, f) == 9 && !memcmp(magic, "FZCOURSE\1", 9);
  while (ok) {
    uint8_t h[4];
    size_t n = fread(h, 1, 4, f);
    if (!n)
      break;
    if (n != 4) {
      ok = false;
      break;
    }
    unsigned id = word(h), size = word(h + 2);
    void *dst = NULL;
    size_t expected = 0;
    if (!id || id > 18 || (seen & (1u << id))) {
      ok = false;
      break;
    }
    seen |= 1u << id;
    if (id <= 16) {
      dst = (uint8_t *)c + sections[id - 1].offset;
      expected = sections[id - 1].size;
    }
    if (id == 17) {
      dst = m;
      expected = sizeof(m);
    }
    if (id == 18) {
      dst = glyphs;
      expected = sizeof(glyphs);
    }
    ok = size == expected && fread(dst, 1, size, f) == size;
  }
  ok = ok && !ferror(f) && seen == 0x7fffe;
  fclose(f);
  if (ok) {
    c->setting = m[0];
    c->gradient = m[1];
    c->map_x = (uint16_t)word(m + 2);
    c->map_y = (uint16_t)word(m + 4);
    c->last_checkpoint = m[6];
    c->finish_checkpoint = m[7];
    c->pit_checkpoint = m[8];
    c->has_pit = m[9];
    c->block_size = (uint16_t)word(m + 10);
    c->grid_size = (uint16_t)word(m + 12);
    c->has_palette_cycles = m[14];
    c->palette_cycle_count = m[15];
    c->required = m[16];
    c->has_music = m[17];
    c->music = m[18];
    c->msu_track = m[19];
    c->intro_glyph_count = glyphs[0];
    ok = c->intro_glyph_count <= 28;
    for (unsigned i = 0; ok && i < c->intro_glyph_count; ++i) {
      c->intro_glyphs[i].code = glyphs[1 + i * 33];
      memcpy(c->intro_glyphs[i].pixels, glyphs + 2 + i * 33, 32);
    }
    ok = ok && FzeroCourseValidate(c, e, cap);
  }
  if (ok) {
    FzeroCourseHash(c);
    *out = *c;
  } else
    fail(e, cap, "Invalid or unsupported course resource");
  free(c);
  return ok;
}
