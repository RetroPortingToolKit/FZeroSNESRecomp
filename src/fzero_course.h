#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  FZERO_COURSE_GRIP_MAGNETS = 1,
  FZERO_COURSE_UP_MAGNETS = 2,
  FZERO_COURSE_RAINBOW = 4,
  FZERO_COURSE_SOFT_HORIZON_FOG = 8,
  FZERO_COURSE_GAMEPLAY_FEATURES = 7,
  FZERO_COURSE_FEATURES = 15
};

/* Decoded resources, never executable donor bytes. One adapter feeds the
 * same retail/Deluxe race engine regardless of the number of installed packs. */
typedef struct FzeroCourseLayout {
  unsigned count;
  uint32_t pools, settings, palettes, maps, graphics, paths, names;
  uint32_t sky_graphics, sky_back, sky_front, minimaps, map_positions;
  uint32_t terrain, gradients, opponents, shortcuts;
  uint32_t palette_cycles; /* Optional same-bank pointer16 table. */
  uint8_t required, course_required[128];
  uint32_t music; /* Optional byte table: native SPC upload-list offset (song * 9). */
  uint8_t spc_override[128]; /* Zero = absent; otherwise canonical song index + 1. */
  /* Optional native intro letters: two donor 2bpp tiles per 8x16 glyph. */
  unsigned intro_glyph_count;
  struct { uint8_t code; uint32_t top, bottom; } intro_glyphs[28];
  char msu_source[96]; /* Stable soundtrack ID, resolved through soundtrack metadata. */
  uint8_t msu_tracks[128]; /* Optional source-slot -> authored PCM number. */
} FzeroCourseLayout;
typedef struct FzeroCourse {
  uint8_t pool[0x2400], blocks[0x2200], grid[0x9000];
  uint8_t graphics[0x4000], palette[0xe0];
  uint8_t sky_graphics[0x2000], sky_back[0x700], sky_front[0x540];
  uint8_t minimap[0x200], terrain[0x400], name[128];
  uint8_t path[0xa00]; /* $11fe..$1bfd, normalized checkpoint arrays. */
  uint8_t setting, gradient;
  uint16_t map_x, map_y;
  uint8_t last_checkpoint, finish_checkpoint, pit_checkpoint, has_pit;
  uint8_t opponents[3], shortcuts[17 * 16 + 2];
  uint16_t block_size, grid_size;
  uint8_t hash[32];
  /* Extensions follow the legacy hash region, preserving existing pack keys. */
  uint8_t has_palette_cycles, palette_cycle_count, palette_cycles[14];
  uint8_t required;
  /* Presentation only: deliberately excluded from the course/record hash. */
  uint8_t has_music, music;
  uint8_t intro_glyph_count;
  struct { uint8_t code, pixels[32]; } intro_glyphs[28];
  char msu_source[96];
  uint8_t msu_track; /* Presentation only; excluded from course/record hashes. */
} FzeroCourse;
bool FzeroCourseLayoutRead(const char *path, FzeroCourseLayout *out, char *error, size_t cap);
bool FzeroCourseExtract(const uint8_t *rom, size_t size, const FzeroCourseLayout *layout,
                        unsigned index, FzeroCourse *out, char *error, size_t cap);
void FzeroCourseCyclePalette(const FzeroCourse *course, uint8_t palette[0xe0]);
/* Authored native HDMA data, independent of HD rendering and texture filtering. */
bool FzeroCourseApplyHorizonFog(const FzeroCourse *course, uint8_t *rom, size_t size);
