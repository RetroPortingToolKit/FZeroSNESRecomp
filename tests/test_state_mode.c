#include "fzero_state_mode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)

/* A stand-in for fzero_runtime.c's FzeroRuntimeState: magic and version at
 * offsets 0 and 4, the mode tag somewhere inside, and the whole thing written
 * last in the snapshot file. The probe is told the shape, so this test does
 * not need the real struct - it needs the real LAYOUT RULE. */
enum { kMagic = 0x4f52465au, kVersion = 1u, kTrailer = 64, kModeAt = 40 };

static const FzeroStateTrailer kLayout = {kTrailer, kModeAt, kMagic, kVersion};

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* `guest` bytes of engine blob, then the trailer. */
static void build(uint8_t *out, size_t guest, uint32_t magic, uint32_t version,
                  uint8_t mode) {
  memset(out, 0xAB, guest + kTrailer);
  uint8_t *t = out + guest;
  memset(t, 0, kTrailer);
  put_u32(t, magic);
  put_u32(t + 4, version);
  t[kModeAt] = mode;
}

static void write_file(const char *path, const uint8_t *data, size_t len) {
  FILE *f = fopen(path, "wb");
  CHECK(f != NULL);
  CHECK(fwrite(data, 1, len, f) == len);
  CHECK(fclose(f) == 0);
}

int main(void) {
  /* ---- the compatibility rule ------------------------------------------ */
  CHECK(FzeroStateModeCompatible(kFzeroStateModeStock, kFzeroStateModeStock));
  CHECK(FzeroStateModeCompatible(kFzeroStateModeDeluxe, kFzeroStateModeDeluxe));
  CHECK(!FzeroStateModeCompatible(kFzeroStateModeDeluxe, kFzeroStateModeStock));
  CHECK(!FzeroStateModeCompatible(kFzeroStateModeStock, kFzeroStateModeDeluxe));
  /* A snapshot written before the tag existed still loads, in either mode:
   * refusing every 1.5.0 state would be silent data loss, and the two modes
   * already keep their slots in different directories. */
  CHECK(FzeroStateModeCompatible(kFzeroStateModeUnknown, kFzeroStateModeStock));
  CHECK(FzeroStateModeCompatible(kFzeroStateModeUnknown, kFzeroStateModeDeluxe));
  for (int current = kFzeroStateModeStock; current <= kFzeroStateModeLocalVersus; ++current)
    for (int saved = kFzeroStateModeStock; saved <= kFzeroStateModeLocalVersus; ++saved)
      CHECK(FzeroStateModeCompatible((FzeroStateMode)saved, (FzeroStateMode)current) == (saved == current));
  CHECK(!FzeroStateModeCompatible(kFzeroStateModeUnknown, kFzeroStateModeStockMsu));
  CHECK(!FzeroStateModeCompatible(kFzeroStateModeUnknown, kFzeroStateModeDeluxeMsu));
  CHECK(!FzeroStateModeCompatible(kFzeroStateModeUnknown, kFzeroStateModeLocalVersus));

  CHECK(!strcmp(FzeroStateModeName(kFzeroStateModeStock), "stock"));
  CHECK(!strcmp(FzeroStateModeName(kFzeroStateModeDeluxe), "BS F-Zero Deluxe"));
  CHECK(!strcmp(FzeroStateModeName(kFzeroStateModeUnknown), "untagged"));

  /* ---- probing bytes ---------------------------------------------------- */
  uint8_t buf[512];
  FzeroStateMode mode = kFzeroStateModeStock;

  build(buf, 128, kMagic, kVersion, kFzeroStateModeDeluxe);
  CHECK(FzeroStateProbeBytes(buf, 128 + kTrailer, &kLayout, &mode));
  CHECK(mode == kFzeroStateModeDeluxe);

  for (int tag = kFzeroStateModeStockMsu; tag <= kFzeroStateModeLocalVersus; ++tag) {
    build(buf, 128, kMagic, kVersion, (uint8_t)tag);
    CHECK(FzeroStateProbeBytes(buf, 128 + kTrailer, &kLayout, &mode));
    CHECK(mode == (FzeroStateMode)tag);
  }

  /* The guest blob's length varies with the snapshot version; the trailer is
   * still found from the END of the file. */
  build(buf, 7, kMagic, kVersion, kFzeroStateModeStock);
  CHECK(FzeroStateProbeBytes(buf, 7 + kTrailer, &kLayout, &mode));
  CHECK(mode == kFzeroStateModeStock);

  /* A 1.5.0 snapshot: tag byte still zero. */
  build(buf, 32, kMagic, kVersion, 0);
  CHECK(FzeroStateProbeBytes(buf, 32 + kTrailer, &kLayout, &mode));
  CHECK(mode == kFzeroStateModeUnknown);

  /* An out-of-range tag is not trusted as a mode; it reads as untagged. */
  build(buf, 32, kMagic, kVersion, 99);
  CHECK(FzeroStateProbeBytes(buf, 32 + kTrailer, &kLayout, &mode));
  CHECK(mode == kFzeroStateModeUnknown);

  /* Foreign or truncated files are refused outright, because the engine
   * applies the guest blob before the game ever sees the trailer. */
  build(buf, 32, 0xdeadbeefu, kVersion, kFzeroStateModeStock);
  CHECK(!FzeroStateProbeBytes(buf, 32 + kTrailer, &kLayout, &mode));
  build(buf, 32, kMagic, kVersion + 1u, kFzeroStateModeStock);
  CHECK(!FzeroStateProbeBytes(buf, 32 + kTrailer, &kLayout, &mode));
  build(buf, 32, kMagic, kVersion, kFzeroStateModeStock);
  CHECK(!FzeroStateProbeBytes(buf, kTrailer - 1, &kLayout, &mode));
  CHECK(!FzeroStateProbeBytes(NULL, 999, &kLayout, &mode));

  /* A trailer exactly the size of the file is legal: no guest blob is a
   * degenerate case, not a malformed one. */
  build(buf, 0, kMagic, kVersion, kFzeroStateModeDeluxe);
  CHECK(FzeroStateProbeBytes(buf, kTrailer, &kLayout, &mode));
  CHECK(mode == kFzeroStateModeDeluxe);

  /* ---- probing files ---------------------------------------------------- */
  const char *path = "test-state-mode.sav";
  remove(path);
  CHECK(!FzeroStateProbeFile(path, &kLayout, &mode)); /* missing */

  build(buf, 200, kMagic, kVersion, kFzeroStateModeDeluxe);
  write_file(path, buf, 200 + kTrailer);
  mode = kFzeroStateModeStock;
  CHECK(FzeroStateProbeFile(path, &kLayout, &mode));
  CHECK(mode == kFzeroStateModeDeluxe);
  CHECK(!FzeroStateModeCompatible(mode, kFzeroStateModeStock));

  build(buf, 200, kMagic, kVersion, kFzeroStateModeStock);
  write_file(path, buf, 200 + kTrailer);
  CHECK(FzeroStateProbeFile(path, &kLayout, &mode));
  CHECK(mode == kFzeroStateModeStock);
  CHECK(FzeroStateModeCompatible(mode, kFzeroStateModeStock));

  /* Truncated mid-trailer. */
  write_file(path, buf, kTrailer - 1);
  CHECK(!FzeroStateProbeFile(path, &kLayout, &mode));

  /* Garbage of the right length. */
  memset(buf, 0x5a, sizeof(buf));
  write_file(path, buf, 200 + kTrailer);
  CHECK(!FzeroStateProbeFile(path, &kLayout, &mode));

  /* A malformed layout is refused rather than read out of bounds. */
  {
    FzeroStateTrailer bad = kLayout;
    bad.mode_offset = kTrailer; /* one past the end */
    build(buf, 8, kMagic, kVersion, kFzeroStateModeStock);
    CHECK(!FzeroStateProbeBytes(buf, 8 + kTrailer, &bad, &mode));
    bad = kLayout;
    bad.size = 4;
    CHECK(!FzeroStateProbeBytes(buf, 8 + kTrailer, &bad, &mode));
    CHECK(!FzeroStateProbeFile(path, NULL, &mode));
  }

  remove(path);
  puts("Save-state cartridge tagging passed");
  return 0;
}
