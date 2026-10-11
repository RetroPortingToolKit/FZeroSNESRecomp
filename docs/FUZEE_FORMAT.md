# Fuzee format investigation

Fuzee is Puresabe's older SNES F-Zero course editor. It is separate from
F-Zero Edit. Its output cannot be treated as an FZEdit `.fzm` project.

## Original source

Puresabe's [official site](https://borokobo.web.fc2.com/#FZ) links to the
[Fuzee 0.04 download](https://www.dropbox.com/s/i25ihyuav2rab0n/fuzee004.zip?dl=1).
Retrieved October 9, 2026; archive SHA-256:

```text
baee17ae35f122490f1c662f4166a8d8b567009e7f1d2ffbcecb2db82daa6597
```

The archive includes `src`, the executable, sample brushes and a `working`
project. The original readme permits source modification and publication of
modified versions, and asks that software using the source publish its own
source. Preserve that notice if incorporating the original code. This project's
decoder is a separate Python implementation of the data layout; the downloaded
source and executable stay in ignored `captures/fuzee-reference/fuzee004`.

Useful source references:

| File | What it explains |
| --- | --- |
| `src/_task_main.cpp`, lines 769–814 | Exact loader changes written to the ROM |
| `src/_task_main.cpp`, lines 1006–1076 | Placement and GP/practice order |
| `src/fzcd.cpp`, `FZCD::Write2ROM` | Road dictionaries, alternate layouts, checkpoints and AI |
| `src/fzcd.cpp`, `FZCD::Save` / `Load` | Editable `working/regionN.txt` text format |
| `src/fzcd.h` | Dimensions, capacities and project version |

## What we verified

Nebula Highway v0.1.1 matches all fourteen independent loader signatures checked
by `tools/fuzee_course_format.py`, as well as the classic course selector. These
identify the **Fuzee 0.04-compatible layout**. They do not establish which editor
version the author actually ran, or qualify other executable changes in the hack.

The supplied IPS has SHA-256
`bec8ec7300f289fc13f77bdc2e3b299169f7f542034f5842a03ea0e7f0c8c533`.
Applied to the original USA ROM, its headerless target is 1 MiB with SHA-256
`168817dc602b7372042d7398fe6cd04e02785887628b97102bf802c31956ef85`.
The private audit decodes 15 GP entries, 7 practice entries and 15 distinct road
variants, with checkpoint positions and their original flags/AI bytes. Practice
references the same roads and routes as the corresponding GP courses.

## Decoder interface

Run against a private, already patched, headerless donor ROM:

```powershell
python tools/fuzee_course_format.py path/to/donor.sfc `
  --out captures/my-fuzee-audit
```

The output must be a new directory. It contains `fuzee-audit.json` and raw road
definition maps. It is an inspection artifact, **not an installable pack**. The
decoder only reads bytes; it does not run the editor or donor instructions.

The format can be decoded without matching a hack's name or digest:

* `$03:9F00` contains nine eight-byte road resource entries. Each has two packed
  pointer/length pairs. Pointers encode a bank-table index and a sixteen-byte
  address; lengths are sixteen-byte units. Reads can cross LoROM bank boundaries.
* A road has 512 one-byte block indices, a dictionary of 32-byte blocks and a
  dictionary of 32-byte rows. Each block selects sixteen rows using pointers
  based at `$7000`. Each row holds sixteen two-byte chip-definition offsets.
  Expanding them yields a 512 by 256 grid of 16-pixel chips.
* `$11:8000` selects replacement lists in `$11:8100`. Each six-byte record replaces
  four blocks in a 2 by 2 square; `$FFFF` terminates the list. Course selector
  high nibbles `$E`, `$C`, `$D` select the base, first and second variant. Only
  region 7 has the second variant in this layout.
* `$02:E129` lists fifteen GP entries followed by seven practice entries. The low
  nibble selects the region. Checkpoint pointers are at `$10:8000`, with practice
  beginning at `$10:801E`.
* Checkpoint headers identify a main route and optional branch. Each points to
  six byte arrays: signed X/Y deltas in eight-pixel units, flags and three AI
  parameters. The decoder retains branch origins and raw parameter bytes.

All dictionary indices, replacement coordinates, pointers, counts and
terminators are bounded. Malformed data is rejected; it is never wrapped with
the editor's modulo-ROM access macros.

## Playable course conversion

`tools/fuzee_course_import.py` adapts verified Fuzee dictionaries and native
resource consumers to the typed exporter. Custom Content uses it automatically
for supported patches and ROMs, including Nebula Highway v0.1.1, Alternate
Strike V2.4 and Parallel Dimension 1.3. The original audit command above remains
an inspection tool, not a pack installer.

The adapter reconstructs tile art, palette cycles, compressed horizons,
minimaps, native terrain, checkpoint/AI arrays and opponent frequencies. Used
chip definitions are reindexed without changing their pixel quads; animated
mine pointers and row sharing are preserved. Native name suffixes and SPC
selection are decoded from verified consumers. Both music-table operands must
agree, so a relocated table remains supported without a hack-name whitelist.

The fifteen GP courses are retained in order. Seven Practice entries must
match GP maps and routes; separate Practice-only data needs another adapter.
User-confirmed cup labels are not presented as decoded author menu labels.
Changed classifiers, unknown presentation consumers and active hard-coded
shortcut penalties fail before publishing a pack. Donor vehicles, menus and
global executable changes are excluded.

Each course passes native extraction, editable ZIP reconstruction and byte-exact
round-trip checks. Private validation on these three patches includes driving
courses from each cup through repairs and barriers, present jump/boost surfaces,
and input fuzz. Nebula's first course was compared against the patched donor
running in Snes9x: all 131,072 map chips matched. These are spot checks, not
certification of every complete race. Raw Fuzee text projects remain unsupported
as direct inputs.

Reconstructing a ROM cannot recover the author's editing history or original
working layers. Even the initial checkpoint's editor-only parameters are not
all serialized. Avoid claiming a recovered project is the original source.

Tracking: central Beads `beads-8wg.5.91`.
