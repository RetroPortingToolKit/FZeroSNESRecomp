# F-Zero Forever packs (shared envelope, course index format 1)

Put a pack folder or ZIP in `mods/packs`, then restart the game. **Track Pack
Loader** is enabled by default and adds every valid installed course pack.
An explicitly saved off setting stays off. There are no individual
pack switches. Remove a folder/ZIP to uninstall it. The original 15 courses
remain available. BS Satellaview Tracks and the loader exclude each other;
vehicle options remain separate. Invalid packs appear in the Mods diagnostics.

The launcher also offers **Custom Content > Import** for ZIPs, FZEdit projects,
patches and ROM hacks. It validates and installs supported inputs. Included
packs are labeled separately and cannot be removed or replaced through that
page. Build staging records their ownership in `mods/.bundled-packs.json`;
user pack manifests cannot declare themselves included. This index is not a
course or music manifest and does not change record identities.

ZIPs contain `pack.json` at their root, or inside one enclosing directory.
Use relative paths with forward slashes. Keep each FZEdit export's referenced
files together; filenames do not have to match course IDs. Stable IDs identify
content, while names are the labels players see. Never assign another pack's ID
to an unrelated pack. Duplicate installed IDs exclude both copies.

`pack.json` is the shared snesrecomp envelope:

```json
{
  "format": "snesrecomp.data-pack", "version": 1, "game": "f-zero",
  "id": "my-project", "title": "My Project",
  "base_rom_sha256": "bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2",
  "requires": ["fzero-course-v1"],
  "payload": {"format": "fzero.course-index", "file": "courses.json",
              "sha256": "<SHA-256 of courses.json bytes>"}
}
```

The game-owned index below goes in `courses.json`. Its `id` and `name` must
match the envelope. `tools/pack_manifest.py:write_index(folder, index)` writes
both files and computes the digest; it uses the pinned engine's shared writer.
Set `SNESRECOMP_ROOT` when developing against a separate engine worktree.
Old pre-release single-file manifests must be re-exported; course resources,
record identities and music filenames stay unchanged.

```json
{
  "format": 1,
  "id": "my-project",
  "name": "My Project",
  "author": "Course Author",
  "order": 100,
  "cups": [{"id": "first", "name": "First League", "courses": ["coast"]}],
  "courses": [{
    "id": "coast", "name": "Coastal Run", "source": "courses/coast/Coast.fzm",
    "requires": [],
    "music": {"spc": 1, "soundtrack": "my-project", "track": 10}
  }],
  "soundtracks": [{"id": "my-project", "prefix": "My Project", "directory": "music"}]
}
```

Cup order and each cup's course order are authoritative. Cups contain 1–5
entries. Reusing a course in multiple cups does not require duplicating its
files. Pack order sorts by `order`, then installation entry name. Current
capacity is 64 catalog packs, 32 cups per pack and 128 course entries per pack.
These bounds produce diagnostics; they are not silently truncated.

## Course sources

`source` supports `.fzm` exported by FZEdit 1.2.0 (its internal file version is
`FZEdit Version 0.9`) and portable `.fzc` extracted course resources. FZM input
uses its AIP checkpoint file, CSV TMX layers, TSX properties, indexed tilesets,
palette, horizon and minimap. Image signatures determine the format: FZEdit
can save PNG data with a BMP/GIF filename. Cache files are disposable.

A course may also reference a single-course pack ZIP, for example
`"source": "courses/huckmine.zip"`. The ZIP uses the same shared `pack.json` and
`courses.json` format and must contain exactly one course. Its source must be
FZM or FZC; nested ZIP chains are rejected. Its required mechanics are retained
and combined with the enclosing pack. The enclosing pack supplies the league
position, course identity and optional music overrides. Audio filenames follow
the ZIP filename (`huckmine.zip` means `music/huckmine.pcm` in the enclosing pack).

For a ZIP source, optional `source_id` records the nested archive's stable
`pack.json` ID. Import/export tools populate this field. If the file is renamed
within its course directory, the folder-pack loader can locate that identity,
validate the course, and update `source` and the envelope checksum. Older
reconstructed imports use the existing `<pack-id>-<course-id>` identity.
Duplicate or missing identities are rejected; display names are never used to
guess a replacement. Matching music follows the new filename without replacing
an existing destination PCM. This repair applies to installed folder packs,
not to source paths inside an outer pack ZIP. Stable IDs and record keys do not
change, and renaming the ZIP does not change the displayed course name.

The Huckmine example preserves the supplied twelve files byte-for-byte. The
maintainer helper `tools/package_fzedit_course.py` adds descriptors and credits
to a new ZIP without changing the original archive. Its map layout, checkpoints,
palette, horizon and minimap match the ROM extraction; some tile artwork differs.
Source conversion also uses different layout compression, so its compiled
record signature differs. Previous Zenith records are preserved in their old
namespace rather than reassigned to a different source build.

The source supplies SPC music unless `music.spc` overrides it. SPC indices are:
0 Mute City, 1 Big Blue, 2 Sand Ocean, 3 Silence, 4 Port Town, 5 Red Canyon,
6 White Land I, 7 White Land II, 8 Fire Field, 9 Death Wind.

`requires` declares native engine capabilities: `grip-magnets`, `up-magnets`,
`rainbow-road`, `soft-horizon-fog`, and `fall-down`. These apply to this course, not every installed track.
`fall-down` provides Illusion's off-road fall and wall bypass with normal gravity;
`rainbow-road` also applies CGP Rainbow Road's increased gravity.
Unknown capabilities reject the pack. This is not an ASM execution interface.
New data can load without a game update; a genuinely new mechanic requires
an engine implementation first. See MODS.md for the existing mechanic details.

### Pack-owned mechanics modules

Required terrain behavior is part of the pack, with no global Mods checkbox.
Put reusable module files inside the pack and reference them with `mechanics`:

```json
"mechanics": ["mechanics/terrain.json"]
```

At the top level this applies to every course in that pack. The same field on
one course adds behavior only to that course. Paths are relative to the pack
root, even when the course's FZM lives in a subdirectory. A module looks like:

```json
{
  "format": 1,
  "id": "cgp-magnets",
  "engine": "fzero-course-v1",
  "requires": ["grip-magnets", "up-magnets"]
}
```

CGP and Astra exports include a shared terrain module. CGP Rainbow Road adds
its own `rainbow-road` module on that course only. Multiple modules combine;
a course's `requires` cannot turn off its pack's mandatory mechanics. Required
modules are validated before the pack is registered; missing files, unknown
formats/engines, and unsupported requirements reject the pack with a diagnostic.

These files bundle declarations of reviewed ASM behavior. The native adapters
that implement that behavior remain in the F-Zero adapter; raw `.asm` is not assembled
or executed at launch. New packs may reuse supported mechanics without an EXE
update. New ASM behavior still needs a reviewed adapter. Developer distributions
retain the original ASM sources for that work.

For reviewed ROM hacks without editor source, `tools/export_runtime_packs.py`
applies the known patches privately, extracts resources and uses
`tools/reconstruct_fzedit_course.py` to recover a single-course editor ZIP for
each resource. All 95 shipped entries reference ZIPs. Releases include compiled
`.fzc` files in `mods/packs/.cache/courses/` and extracted projects in
`mods/packs/.cache/sources/`, generated from those exact ZIPs during packaging.
These content-keyed caches remain valid when the install moves. Edited or
new projects rebuild automatically; source ZIPs remain the editable originals.
The result
contains no ROM or executable donor code. `extraction.json` identifies the
source revision and record hashes. It is reconstructed data, not the original
author's FZEdit project or editing history. Unchanged projects are checked
byte-for-byte against the previously decoded courses, including presentation
fields excluded from record hashes. Unknown patches still need format
and mechanics qualification; their filenames cannot establish compatibility.

The default bundle contains CGP's 55 course versions (15 revised originals,
10 revised BS courses, 30 additional courses), Astra Velocity's 20, MAX's 5,
and Bower's 5.
Extraction recovers layout, tile/palette/horizon/minimap resources,
checkpoints, opponents/shortcuts, intro text, native music and reviewed terrain
requirements. It does not recover author layers, filenames, editing history,
ASM source, or absent MSU recordings. These examples preserve the 0.2.0 decoded
resources and record keys; they are not newly reinterpreted course designs.

### Portable resource encoding

`.fzc` starts with the nine bytes `FZCOURSE` followed by byte `1`. Each section
has a little-endian uint16 ID and uint16 byte length. All 18 sections are
required exactly once; unknown sections are rejected in version 1. IDs 1–16
hold pool, blocks, grid, tile graphics, palette, horizon graphics, back/front
horizon maps, minimap, terrain, intro name, AI path, opponents, shortcuts,
palette cycles, and soundtrack ID respectively. Section 17 stores explicit
little-endian scalar metadata; section 18 stores the count and 28 reserved
intro glyph slots (one code plus 32 pixel bytes each). The authoritative sizes
and scalar offsets are in `src/fzero_course_file.c`; no compiler struct layout
or donor pointers are serialized. Authors normally use FZEdit exports instead
of constructing this engine-oriented resource by hand. It remains accepted as
an explicit source for externally authored packs; our shipped packs use ZIPs.

### Reconstructed editor projects

Recovered projects contain FZM, AIP, TMX, TSX, indexed tilesets, palette,
horizon preview and minimap files in FZEdit's conventions. Checkpoint files
use CRLF, as required by FZEdit's own reader. The original author-supplied
Huckmine project is kept unmodified instead of being reconstructed.

An optional `ReconstructionFile` property in FZM points to a JSON companion
with `format: "fzero.reconstruction"`, `version: 1` and `groups`. This is data,
never ASM. Each named group contains `files` (FZM component property to SHA-256),
`properties` (FZM property to original value), and `fields` (bounded native
scalars or hex byte arrays). Supported groups cover layout compression,
shortcuts, AI checkpoints, tile behavior, horizon tiles/maps, intro lettering,
settings, shading and palette cycles. Unknown groups/fields or wrong bounds
reject the project.

A group retains its native fields only while all its declared editor inputs
match. Editing those inputs makes the regular FZEdit compiler authoritative
for that group. Other groups retain their native details. This preserves
compression, custom glyphs and original record keys without concealing a
compiled FZC inside the ZIP. The companion is part of the cache fingerprint;
missing or malformed companions are diagnosed, never silently ignored.

Keep it beside the editor files when repacking. It does not recover original
author layers, names of working files or history. It is not required for a
new project exported directly from FZEdit.

Building the importer requires C++20, RapidJSON, libarchive, libxml2, libpng,
and giflib in addition to the existing game dependencies. Runtime packages
include the Windows DLL dependencies; end users do not install FZEdit or Python.

## Music

Music is optional and independently enabled in Audio settings. The game
automatically discovers recordings in the installed packs; no source picker is needed.
Use one naming rule: **the PCM matches the course source filename**, in that
pack's `music/` folder. Drop it in, restart, and enable MSU-1.
Create `music/` if it is absent.

```text
mods/packs/astra-velocity/
    courses/big-blue.zip
    music/big-blue.pcm
```

For raw FZEdit input, `courses/coast/Coast.fzm` uses `music/Coast.pcm`.
Match the filename exactly, including case, without the `.zip`/`.fzc`/`.fzm` extension.
For ZIPs, use the archive name, not the filenames inside.
No MSU number, soundtrack prefix, JSON music mapping, or `.msu` file is needed.
The course display name and ID do not select the recording. Two source files
with the same basename in one pack share its PCM; different packs stay separate.
Missing recordings use SNES music. Adding music does not change course records.

CGP recordings now ship in `mods/packs/cgp/music`, following the same rule for
race songs. There is no separate `cgp-audio` folder in new bundles. Shared menu
cues retain the existing adapter filenames in that folder. Original numbered
MSU packs can be renamed to match the course ZIPs. Astra Velocity uses its authored
SNES music unless matching course recordings are added.

This convention requires 0.4.0 or newer. The older 0.3.0 tester ZIPs still need
their original prefixed recordings.

## Optional title artwork

`titles` is an array of `{ "id": "my-project", "name": "My Project",
"source": "title.fzt" }`. Artwork is selectable in the existing independent
screen-override dropdown; installing a pack does not select it automatically.
The reviewed extractor produces FZTITLE version-1 resources for the current
native title layout. Arbitrary donor title code is not supported.

## Maintainers and automated contributors

Preserve author names, cup/course order, native music, and capability requirements.
Do not infer music from venue graphics. Do not combine vehicle families while
extracting courses. Keep local ROMs, editor inspection files and recordings out
of Git. Qualify all course resources, then test GP, Practice and records before
publishing a pack. Compare extracted packs with the original resource hashes;
for raw sources, compare decoded layout and gameplay rather than compressor
byte ordering. Updating music alone must not reset course records.
## Menu and event recordings

The course index can supply optional menu defaults, including in an audio-only
pack. No executable change is needed to add another soundtrack to the Mods
dropdown. Paths are relative to that pack and remain displayed when recordings
are absent from the no-audio download:

```json
"menu_music": {
  "countdown": "music/cgp-1.pcm",
  "ready": "music/cgp-2.pcm",
  "lost-life": "music/cgp-3.pcm",
  "title": "music/cgp-4.pcm",
  "select": "music/cgp-5.pcm",
  "ending": "music/cgp-7.pcm"
}
```

Any subset is valid. Unknown event names and paths outside the pack are rejected.
Unavailable songs use SNES audio. User-selected files override one event and are
stored in `loader.cfg`; clearing a selection restores its pack value. An explicit
selection of a temporarily absent pack is preserved rather than silently switching
soundtracks. With no explicit selection, the primary soundtrack pack supplies the
defaults (CGP in the supplied installation), otherwise the first declared pack.
The first three cues play once; the others loop. These command assignments follow
the [authors' MSU patch map](https://www.zeldix.net/t2768-bs-f-zero-deluxe-msu-1).

After editing `courses.json`, regenerate `pack.json` using
`tools/pack_manifest.py`'s `write_index`; its payload digest must match.
