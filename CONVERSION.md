# Bringing courses into F-Zero Forever

Open **Custom Content**, choose **Import**, and select your ZIP, FZM,
IPS/BPS patch or hacked ROM. You can give the imported pack a display name.
The importer detects its contents, checks usable courses and installs them for
the next time you press Play. For an unpacked editor export, choose its FZM
file with its companions beside it, or ZIP the whole export. There is one
Import button for all of these inputs. The list also shows installed mod files.

CGP, Astra Front, Bower and MAX are included packs. This page cannot remove or
replace them. Huckmine belongs to CGP; it is not an additional default pack.

Patch conversion uses your selected original game ROM. You do not need Python
or a command line. Known packs import directly. For a new supported FZEdit-based
hack, the importer checks the courses, then opens **Review your courses**.
Give the pack and its cups names, check the course assignments, and choose
**Import courses**. Suggested cup names are editable labels, not claims about
the original hack. Cancel leaves the installed collection unchanged.

Course names and music choices are read from the source where possible. Cup
dropdowns let you correct grouping without editing files. Each cup needs one
to five courses. The imported courses use the supported game rules described
in the review; custom vehicles and other game-wide code changes are not copied.
If the actual course format cannot be decoded and validated, the importer
explains what is unsupported instead of asking you for memory addresses.

Keep a spare copy of the files you received. Original F-Zero Edit projects are
the best input: they already contain the editable course. A ROM patch can also
be converted when its course data uses a supported layout.

Conversion adds course data and supported course mechanics. Vehicle changes,
global game rules and other donor ASM need separate adaptation. ZIPs can include
music too: recordings with a verified mapping are copied into the pack's
`music` folder with the matching course filename. Missing recordings use the
course's selected SNES song.

Uncertain music does not block otherwise valid courses. The result tells you
that the courses were imported and which music needs attention. The original
ZIP is unchanged. See the installed pack's `conversion-report.json` for the
unresolved filenames, then put the intended recording in `music` with the same
name as its course ZIP (for example, `coast.zip` uses `music/coast.pcm`). Menu
music uses the pack's `menu_music` entries. Ask the pack author if a recording's
destination is unclear; the importer does not guess from the track's sound.
The installed-content list retains a **songs weren't imported (?)** notice.
Hover it for placement instructions. It describes the original import, even
after you supply songs yourself.

## IPS/BPS

The converter accepts `.ips`, `.bps`, a hacked `.sfc`/`.smc`/`.rom`/`.fig` image,
or a ZIP containing these files, including downloads with MSU recordings and
documentation. It checks the resulting ROM's contents rather than trusting a
filename. Direct-import profiles cover Astra Front, Bower League, the current
CGP P3test revision, and MAX League Classic/Modern. Other supported FZEdit-based
revisions use the interactive review. Older revisions do not inherit a known
pack's identity or compatibility claims.

Older FZEdit layouts are also supported, including the SNES Maximum Velocity
hack, Hybrid and ReFractured. The decoder recognizes relocated data consumers,
older course-name and checkpoint loaders, inline minimaps, and the original
compressed skies where retained. SNES music choices come from the donor's
venue/variant selector or its per-course table. They are never inferred from
course names. These downloads are optional imports, not included packs.

The native terrain rules used by older projects are preserved as tile
properties when the original classifiers can be verified. A missing mine
bitmap does not mean there are no repair strips, jumps or boundaries. If you
imported Maximum Velocity with 0.8.2, remove and reimport that pack with 0.8.3
or later; updating the game does not replace an already imported project.
Maintainers should also run `tests/validate_course_driving.py` alongside
course, music and records validation. The repository's
`docs/IMPORTED_COURSE_DRIVING.md` explains the checks and their limits.

PCM numbering is also read from recognized donor MSU selectors. For example,
the supplied FZero55 patch explicitly selects **11–20** for its ten courses;
the first course does not select 10. Hybrid reuses five course recordings
across its two cups. A matched recording is copied to the filename of each
course that uses it, so the installed convention remains
`music/<course-filename>.pcm`. Unrecognized selectors still produce the manual
music placement notice.

For maintainers, run this from the repository root:

```powershell
python tools/convert_course_content.py path/to/course.ips `
  --stock path/to/original-usa-fzero.sfc `
  --out captures/my-converted-pack `
  --exporter build/FZeroExportCourses.exe `
  --inspector build/FZeroInspectPacks.exe
```

The output directory must be new. A successful conversion contains `pack.json`,
`courses.json`, credits, and editable course ZIPs. Copy that folder into
`mods/packs` to try it. Install one copy of a pack: two copies with the same
stable ID conflict. Conversion preserves the reviewed pack, cup and course IDs,
authored names, race order, SPC selection and declared mechanics.

For a ZIP with equivalent IPS/BPS files, selection is automatic and prefers
BPS, whose checksums are verified. Known Classic/Modern variants can share one
reviewed profile. If a ZIP contains distinct packs or unknown differing targets,
select the intended patch or ROM with `--member "folder/course.bps"`. Import
one pack at a time; the picker does not silently choose between different hacks.

Structurally supported new FZEdit revisions produce a review form after native
extraction and editable-project validation. The command returns **3** while
waiting for the user's details, **2** for an unsupported conversion, **0** for
success, or **1** for a malformed input/tool failure. The report records hashes and
bounded file-difference evidence when a stock ROM is available. No patched ROM
is included in any output.

For automation, pass `--answers path/to/answers.json` when resuming a review.
The file must contain the report's `target_sha256` and `input_sha256`, plus a
`values` object mapping each review field ID to its chosen string. This binds
the answers to the file that was checked; a changed ZIP must be checked again.
The launcher handles this automatically.

If automatic course extraction is unsupported, ask its author for the original
project first. Otherwise a person or agent must review its loader and data tables, establish
the actual cup labels/order, names, SPC music, intro lettering and title layout,
and check every course's gameplay. Follow the concrete examples in
[the import checklist](mods/PARSE_MANIFEST.md). A new mechanic needs an implemented
native adapter; a successful data parse does not prove a full race works.
Extend the supported resource decoder, or add a validated exact revision to
the reviewed registry and exporter profile for unattended conversion.
Arbitrary donor code is never run.

Supported Fuzee layouts can also be imported, including Nebula Highway v0.1.1,
Alternate Strike V2.4 and Parallel Dimension 1.3. Select their IPS or ZIP through
the same Import button, then confirm the pack name and cup assignments. These
remain optional downloads; they are not included in the default course list.

Vintage Velocity I v2.1 is a different case: its included README explicitly
targets F-Zero: Maximum Velocity on Game Boy Advance. Its IPS files must not
be applied to the SNES game. IPS itself has no source-game checksum.

The recovered projects include the road, tile art, terrain, colors, sky, minimap,
AI/checkpoints, shortcuts, intro lettering, opponents and supported mechanics.
Keep each `_Reconstruction.json` companion with its editor files. It preserves
native details and existing record hashes while those editor components are
unchanged; edited components are rebuilt. Author layers, original working
filenames, editing history and ASM source cannot be recovered from a ROM.

Every conversion loads the resulting ZIPs through the native pack inspector and
checks all normalized record hashes. Reconstructed courses must also match the
donor's complete serialized resources, including SPC and intro glyph fields.
CGP Huckmine deliberately retains the supplied original F-Zero Edit project,
matching the shipped pack; its source art/compression and record hash differ from
the older ROM extraction. That exception is stated in the report.

CGP and MAX include their reviewed title artwork. Bower uses Original. Astra
shares CGP's artwork and reuses the CGP title choice; it does not add a duplicate
screen. Titles, music and display names do not change course record hashes.

## Older Fuzee projects and patches

Fuzee is a different, older editor. Its projects use `regionN.txt` and
`globalsetting.txt`, rather than F-Zero Edit's FZM and companion files.
Keep that complete project and the author's credits if you have them.

For a supported Fuzee-based hack, import its patch or ROM. The importer converts
all fifteen GP courses to editable F-Zero Edit ZIPs. The seven original Practice
entries are aliases of those courses, so they share their records. Native song
choices come from the actual patched music selector, including relocated tables.
The native road rules retain repair strips, barriers, jumps and boost plates.

Conversion also preserves tile art, palettes, skies, minimaps, intro lettering,
checkpoint/AI data and explosive-opponent frequencies. Native palette cycling
is represented as course data. Cup labels remain editable suggestions. Donor
vehicles, menus and global code changes are excluded. Unrecognized resource
consumers, separate Practice routes and active hard-coded shortcut penalties
are rejected rather than silently omitted. Raw `regionN.txt` projects are not
yet a direct import format; use the author's patch or ROM for this adapter.

Maintainers can use the inspection tool and source references in
[FUZEE_FORMAT.md](docs/FUZEE_FORMAT.md). Its raw audit output belongs in
`captures`, not `mods/packs`. The final converted folder is the installable pack.

## F-Zero Edit

Keep the `.fzm` file and all its companion files together. The companions contain
the track, computer drivers' route, artwork, sky, colors and minimap. Export or
ZIP the complete set, preserving relative paths and the author's credits.
Select that ZIP or FZM through **Custom Content > Import**. The game supplies the
pack description for an ordinary single-course project.

A bare editor export uses standard F-Zero course rules. FZEdit files do not
declare a hack's ASM changes. If the course relies on special rules, use the
author's pack with those rules declared in its manifest. For example, the
packaged CGP Huckmine example declares its magnet rules; the original bare
`HM.zip` cannot supply that declaration by itself. The importer does not guess
those rules from the course name or artwork.

For an original single-course F-Zero Edit ZIP, the maintainer helper can add the
pack descriptions without changing the original files:

```powershell
python tools/package_fzedit_course.py path/to/original-project.zip `
  path/to/my-course-pack.zip --id my-author-my-course --author "Course Author"
```

Choose a stable lowercase ID that belongs to this course. Add `--credits
path/to/CREDITS.txt` to preserve separate attribution. Compatible course mechanics
can be declared explicitly, for example `--requires grip-magnets up-magnets`;
those names mean the reviewed native behaviors described in
[the pack format](docs/PACK_FORMAT.md), not arbitrary ASM from the source.

The new ZIP keeps the original project members byte-for-byte. Copy it into
`mods/packs`, restart, and find its one-course cup. For a multi-course league,
use the cup/course examples in [the pack format](docs/PACK_FORMAT.md). A source
ZIP from an existing pack already has its league position in the enclosing
`courses.json`; keep that relationship when editing bundled courses.

For optional MSU music, match the course source filename: `courses/coast.zip`
uses `music/coast.pcm` in that pack. The names inside the ZIP do not select the
recording. The `.fzm` source supplies its SPC selection unless the enclosing
pack explicitly overrides it.

## Standalone Windows helper

The file picker can invoke `FZeroConvertContent.exe`, so testers do not need
Python. The helper uses the same arguments and exit codes as the Python command.
Ship `FZeroExportCourses.exe` and `FZeroInspectPacks.exe` beside it, including
their usual runtime dependencies. Pass their paths explicitly when staging an
import. The helper contains only the converter, Pillow and reviewed descriptors,
credits, title patches and the preserved Huckmine example; it contains no ROM
or audio recordings.

Build it with Python, Pillow and PyInstaller installed:

```powershell
./tools/build_convert_course_content.ps1 -Python path/to/python.exe `
  -EngineRoot path/to/populated/snesrecomp
```

The result is `build-content-converter/FZeroConvertContent.exe`. Checkout scripts
also use `SNESRECOMP_ROOT` when the shared-engine submodule is elsewhere. Native
conversion can later call `FzeroCourseLayoutRead`, `FzeroCourseExtract` and
`FzeroCourseFileWrite` directly; the standalone helper currently supplies the
editable-project reconstruction and native round-trip validation.
