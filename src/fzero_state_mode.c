#include "fzero_state_mode.h"

#include <stdio.h>
#include <string.h>

int FzeroStateModeCompatible(FzeroStateMode file, FzeroStateMode current) {
  if (file == kFzeroStateModeUnknown) return current == kFzeroStateModeStock || current == kFzeroStateModeDeluxe;
  return file == current;
}

const char *FzeroStateModeName(FzeroStateMode mode) {
  switch (mode) {
    case kFzeroStateModeStock: return "stock";
    case kFzeroStateModeDeluxe: return "BS F-Zero Deluxe";
    case kFzeroStateModeStockMsu: return "stock + MSU-1";
    case kFzeroStateModeDeluxeMsu: return "BS F-Zero Deluxe + MSU-1";
    case kFzeroStateModeTrackPack: return "track pack";
    case kFzeroStateModeLocalVersus: return "versus";
    default: return "untagged";
  }
}

static uint32_t read_u32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/* The trailer's first eight bytes are its magic and version, at offsets 0 and
 * 4 with no padding in front of them, so a byte read is exact on every target
 * this builds for. Reading them through the struct instead would tie this
 * file to a header it deliberately does not include: the trailer's shape
 * belongs to fzero_runtime.c, and only its size and the tag's offset cross
 * over. */
static int accept(uint32_t magic, uint32_t version, uint8_t tag,
                  const FzeroStateTrailer *layout, FzeroStateMode *out) {
  if (magic != layout->magic || version != layout->version) return 0;
  if (tag < kFzeroStateModeStock || tag > kFzeroStateModeLocalVersus)
    tag = kFzeroStateModeUnknown;
  if (out) *out = (FzeroStateMode)tag;
  return 1;
}

static int layout_ok(const FzeroStateTrailer *layout) {
  return layout && layout->size >= 9 && layout->mode_offset >= 8 &&
         layout->mode_offset < layout->size;
}

int FzeroStateProbeBytes(const uint8_t *data, size_t len,
                         const FzeroStateTrailer *layout, FzeroStateMode *out) {
  if (!data || !layout_ok(layout) || len < layout->size) return 0;
  const uint8_t *trailer = data + (len - layout->size);
  return accept(read_u32le(trailer), read_u32le(trailer + 4),
                trailer[layout->mode_offset], layout, out);
}

int FzeroStateProbeFile(const char *path, const FzeroStateTrailer *layout,
                        FzeroStateMode *out) {
  if (!path || !path[0] || !layout_ok(layout)) return 0;

  FILE *f = fopen(path, "rb");
  if (!f) return 0;

  int ok = 0;
  /* Seek from the end: the trailer is the last `size` bytes of the file and
   * everything ahead of it is the engine's guest blob, whose length varies
   * with the snapshot version. */
  if (fseek(f, 0, SEEK_END) == 0) {
    long end = ftell(f);
    if (end >= 0 && (size_t)end >= layout->size) {
      const long start = end - (long)layout->size;
      uint8_t head[8], tag = 0;
      if (fseek(f, start, SEEK_SET) == 0 &&
          fread(head, 1, sizeof(head), f) == sizeof(head) &&
          fseek(f, start + (long)layout->mode_offset, SEEK_SET) == 0 &&
          fread(&tag, 1, 1, f) == 1)
        ok = accept(read_u32le(head), read_u32le(head + 4), tag, layout, out);
    }
  }
  fclose(f);
  return ok;
}
