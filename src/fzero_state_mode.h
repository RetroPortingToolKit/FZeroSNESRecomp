#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Which cartridge a save state was taken from.
 *
 * Stock F-Zero and BS F-Zero Deluxe are two different ROMs sharing one
 * executable, and their snapshots are not interchangeable: the Deluxe module
 * rewrites code and data the stock cartridge does not have. The two already
 * keep their slots apart by path (saves/fzero<N>.sav against
 * saves/bs-deluxe/fzero-bs-deluxe<N>.sav, see FzeroDeluxeSelectSaveRoot), so
 * a file can only cross by being copied and renamed by hand. That is exactly
 * the case this tag exists for: a snapshot says which mode wrote it, and a
 * mismatched one is refused rather than resumed into a machine whose ROM does
 * not match the register and RAM state being restored.
 *
 * The tag lives in one byte of the game's fixed-size snapshot trailer that
 * was previously reserved padding, so a 1.5.0 snapshot reads back as Unknown
 * and still loads in non-MSU sessions. MSU-patched cartridges use their own
 * tags and save directories. The trailer's size and field order are unchanged.
 */
typedef enum FzeroStateMode {
  kFzeroStateModeUnknown = 0, /* pre-1.6.0 snapshot: carries no tag */
  kFzeroStateModeStock = 1,
  kFzeroStateModeDeluxe = 2,
  kFzeroStateModeStockMsu = 3,
  kFzeroStateModeDeluxeMsu = 4,
  kFzeroStateModeTrackPack = 5,
  kFzeroStateModeLocalVersus = 6
} FzeroStateMode;

/* May a snapshot tagged `file` be resumed by a process running `current`?
 * An untagged snapshot is accepted in either non-MSU mode: refusing every state
 * written before the tag existed would be a silent data loss, and those
 * files are already separated by path. */
int FzeroStateModeCompatible(FzeroStateMode file, FzeroStateMode current);

/* Cartridge + optional MSU-1 description, for operator-facing messages. */
const char *FzeroStateModeName(FzeroStateMode mode);

/*
 * Where the mode tag sits. The game's extra chunk is one fixed-size struct
 * written last by RtlSaveSnapshot, so its first byte is at
 * (file_size - size) and the tag at (file_size - size + mode_offset). The
 * layout is passed in rather than duplicated here so the struct stays owned
 * by fzero_runtime.c, which is the only file that knows its shape.
 */
typedef struct FzeroStateTrailer {
  size_t size;        /* sizeof the game's trailer struct */
  size_t mode_offset; /* byte offset of the mode tag inside it */
  uint32_t magic;     /* expected magic at trailer offset 0 */
  uint32_t version;   /* expected version at trailer offset 4 */
} FzeroStateTrailer;

/*
 * Read the tag out of a snapshot without loading any of it.
 *
 * Returns 1 and writes *out when a well-formed trailer was found. Returns 0
 * when the file is missing, shorter than a trailer, or does not carry this
 * game's magic/version — all of which mean "do not hand this to the engine",
 * because the engine applies the guest blob before the game ever sees the
 * trailer and a half-applied load is the failure mode this avoids.
 */
int FzeroStateProbeFile(const char *path, const FzeroStateTrailer *layout,
                        FzeroStateMode *out);
int FzeroStateProbeBytes(const uint8_t *data, size_t len,
                         const FzeroStateTrailer *layout, FzeroStateMode *out);
