# Mods and differences from the source games

## Online VS

For online VS, the rules are automatic and do not have a switch in Lobby Mods.
Everyone gets all twelve CGP cars with their matching artwork, handling and
energy boost. Your single-player car settings stay separate. The host can
adjust input delay in the lobby; F-Zero starts at two frames. Increase it if a
less reliable connection causes frequent corrections.

## Reporting a crash

**Crash diagnostics and replay** is on by default in Mods for these test builds.
Just play normally. If the game crashes, open the `diagnostics` folder beside
the game and ZIP the newest folder whose name starts with `replay-`. Send that
ZIP with a short description of what happened. Keep it before starting another
test. Existing recordings survive later launches.

Each launch keeps roughly the last minute to minute and a half of play: recent
inputs, game checkpoints, settings and records. Loading a state or rewinding
starts a new checkpoint. A Windows crash also produces a crash report and,
when possible, a minidump in that same folder. This lets us replay your inputs
with the matching build and check where the run differs.

Files stay on your computer; nothing is uploaded automatically. They include
game memory, records and local paths. They do not include your ROM file. If
you were using an imported pack or custom music, include that pack separately
so we can reproduce the same setup. Old diagnostic folders can be deleted
when you no longer need them.

The separate **Diagnostics** option records performance timings and remains
off by default. It does not control crash recording. Turning off **Crash
diagnostics and replay** takes effect after restarting the game.

## Adding custom courses

Open **Custom Content** in the launcher and choose **Import**. Pick the ZIP you
downloaded, a F-Zero Edit `.fzm` project, an IPS/BPS patch, or a ROM hack. You can
change its display name before importing. For an unpacked editor project,
select its `.fzm` file and keep the companion files beside it, or ZIP the whole
project. The same picker handles every supported format.

The importer checks the files first. If they work, it installs the pack and
you can press Play. Track Pack Loader must be enabled in Mods. A different
display name does not overwrite an existing pack or change saved records.

Original editor projects work best. For a new supported ROM hack, **Review your
courses** lets you name the cups and check the suggested course assignments.
Choose **Import courses** to finish, or Cancel to leave everything unchanged.
This imports course data using supported game rules; custom vehicles and other
game-wide code changes are not included. Formats that cannot be decoded get an
explanation. See [CONVERSION.md](CONVERSION.md) for editor and patch workflows.
The same guide is included inside the build's `mods` folder.

Community Grand Prix, Astra Front, Bower League and MAX League appear as
**Included course pack**. They cannot be removed or replaced through this page;
their gameplay switches remain in Mods. Huckmine is one of CGP's courses, not a
separate default pack. New imports are separate from the included collection.

If you imported the SNES **Maximum Velocity** hack with 0.8.2, remove that
imported pack and import its patch again with 0.8.3 or later. The earlier
conversion lost its repair, jump and boundary rules. Updating the game alone
does not rebuild an already imported project. Hybrid and ReFractured do not
need reimporting for this fix.

Course music still lives in each pack's `music` folder and matches the course
source filename. ZIPs containing a supported patch or ROM plus recordings can
import both together. If a song's destination is uncertain, valid courses still
import and the result tells you which music needs attention. Your original ZIP
stays unchanged. See the pack's `conversion-report.json` for unresolved files;
place each intended recording at `music/<course-filename>.pcm`. Importing
content does not change that convention. The installed pack keeps a **songs
weren't imported (?)** notice. Hover it for placement instructions. This is a
record of the import, so adding songs later does not change that notice.

When the hack contains a recognized music selector, the importer uses its
course-to-song mapping. This includes FZero55's ten course songs and its title,
menu and ending recordings. Choose that pack under **Mods > Menu and event
music** to use its menu songs. Missing or ambiguous recordings still need
manual placement; file numbering alone is not enough to identify a song.
The FZero55 download we checked uses song **11** for its first course and
**12–20** for the rest. Those numbers come from the patch's music table.
The importer renames the matched songs for you; once installed, they still use
`music/<course-filename>.pcm`.

You can rename an imported course ZIP inside its `courses` folder while the
game is closed. On the next launch, the loader finds its internal identity and
updates the source path. Records and the displayed course name stay the same.
It also renames the matching PCM when there isn't already one under the new
name. For example, `course-1.zip` → `canvas.zip` changes the music path to
`music/canvas.pcm`. Keep the ZIP's internal pack files intact. Missing or
duplicate identities produce an error instead of selecting another course.

### Import limits

ROM-hack imports bring over courses and supported course rules. They do not
bring over custom vehicles, menus, or other game-wide code changes. A successful
import checks the course files; it does not guarantee every custom mechanic
will behave like the original hack. Original F-Zero Edit exports are preferred.
Unmatched songs need manual placement, and missing songs use SNES music.
Older stock-format hacks, including Nebula Highway v0.1.1, need a separate
course decoder and are not supported yet. Vintage Velocity I v2.1 targets
**F-Zero: Maximum Velocity for Game Boy Advance**, so it cannot be imported
into this SNES game. The separate **SNES Maximum Velocity** hack can be imported,
along with Hybrid and ReFractured. They use the same course review form.
For a download supplied as RAR, extract it first and pick
the IPS/BPS file; the importer accepts ZIP archives directly.

## Opening Records for testing

**Mods > Always show Records** is on by default. It keeps **RECORDS** on the
title menu, including on a new save, so you can check courses and vehicles
without finishing a race first. Empty courses stay empty; no scores are added.

Turn it off to restore the original unlock behavior. Your choice is saved,
and changing a preset leaves it alone. This option does not change when the
lap/results screen appears after a race.

## Choosing menu music

Open **Settings > Audio** and enable **MSU-1**. There is no source to browse for.
Then open **Mods > Menu and event music**. The existing CGP songs are already
filled in; the music bundle is ready to play them. In the smaller download,
the same paths show where to put the recordings.

| What plays | Supplied file inside `mods/packs/cgp/` |
| --- | --- |
| Countdown / race start | `music/cgp-1.pcm` |
| Racers ready / zoom | `music/cgp-2.pcm` |
| Lost life | `music/cgp-3.pcm` |
| Title screen | `music/cgp-4.pcm` |
| Menus and records | `music/cgp-5.pcm` |
| Ending / victory | `music/cgp-7.pcm` |

Use **Change file** beside any song to choose a replacement `.pcm`. Its name can
be anything; you are selecting the file itself. **Clear selection** restores that song's
pack default. These choices are saved when you press Play. Turning this mod
off uses SNES music for these events and leaves course music alone. A missing
recording also falls back to SNES music.

The soundtrack dropdown lists installed packs that supply menu songs. CGP is
the supplied default. A pack can provide some events and leave the rest as
SNES audio. Changing the default soundtrack keeps your individual replacements.

For **course music**, place the recording in that pack's `music` folder and
match the course filename: `courses/moon.zip` uses `music/moon.pcm`. The filenames inside a ZIP do not
change this rule.
The music bundle includes the ten supplied Astra recordings in
`mods/packs/astra-front/music`. You can add those same files to the smaller
download without changing any JSON. Astra's course music does not replace
CGP's menu songs.

Pack authors can copy the `menu_music` object from CGP's `courses.json` as a
template. Its six keys are `countdown`, `ready`, `lost-life`, `title`, `select`
and `ending`; each value is a PCM filename relative to the pack folder. These
defaults appear in Mods automatically. See [the pack format](docs/PACK_FORMAT.md)
for the full example and how to update the pack description after editing it.

## Trying the Huckmine source example

Huckmine now runs from its FZEdit project. In Grand Prix, choose **Zenith**;
Huckmine is the first course. It is also available in Practice.

The example is `mods/packs/cgp/courses/huckmine.zip`. Inside are the author's
`hm.fzm` and its companion files, plus the small pack descriptions and credits
we added. The original twelve files have not been renamed or converted.

To edit and try this example:

1. Make a spare copy of `huckmine.zip`, then extract it into a folder.
2. Open `HM/hm.fzm` in FZEdit. Keep its companion files beside it.
3. Make your changes and save them in FZEdit.
4. Zip the `HM` folder, `pack.json`, `courses.json` and `CREDITS.txt` together.
   Replace the installed `mods/packs/cgp/courses/huckmine.zip` with that ZIP.
5. Restart F-Zero Forever and choose Huckmine again.

You do **not** need to make an `.fzc` file. Bundled courses come with a
ready-to-use cache in `mods/packs/.cache/`, so they don't need to be converted
on your first launch. Editing a project or adding a new course makes a new
cache automatically. You can delete the cache; it rebuilds on launch.
Keep backups outside `mods/packs`, and do not install this same example a
second time as a separate pack.

For optional music, use `mods/packs/cgp/music/huckmine.pcm`: it matches
`huckmine.zip`, regardless of the filenames inside. Enable MSU-1
in Audio settings. Without that recording, Huckmine uses its SNES music.

The `.fzm` file is the starting point; the other files hold the track, sky,
colors, minimap and computer drivers' route. Copy the whole set together.
All 75 bundled courses now have a ZIP in their pack's `courses` folder.
Their names are unchanged apart from replacing `.fzc` with `.zip`.
Huckmine contains the author's original files. The other 74 contain editor
projects reconstructed from the ROM data we already imported.

To edit another course, follow the same steps: extract its ZIP, open its `.fzm`
in FZEdit, keep all the companion files together, then zip them back up and
replace the course ZIP. Keep the file ending in `_Reconstruction.json` too.
It preserves details that the editor cannot store. The game rebuilds the
parts you change and keeps the other parts intact.

Unedited courses keep their existing records. A changed course can get a
separate records list; your previous records remain saved. Reconstructed
projects recover the playable course, not the author's original working
layers, filenames or editing history. They are labeled inside each ZIP.

This example's league placement and CGP magnet behavior are already set up.
For a **new** course or league, a pack description is still required; dropping
a bare editor ZIP into the game is not yet supported. The example demonstrates
source loading, not automatic league creation. Advanced authoring details are
in [the pack format](docs/PACK_FORMAT.md).

Track packs add courses to the original 15. Their required terrain mechanics
follow the **course you are racing**. Vehicle rebalances, difficulty and other
general gameplay options follow your **Mods settings**, including when you
race a different pack. A preset selects options once; you can edit them afterward.
Presets leave unrelated presentation options as you set them. The Track Pack
Loader now enables all installed course packs together; add/remove folders or
ZIPs under `mods/packs`. See [the pack format](docs/PACK_FORMAT.md).

## Course behavior

| Content | Behavior in this port |
| --- | --- |
| Original Knight, Queen and King | Original courses remain available. CGP's revisions are additional cups. |
| CGP courses | Use their authored terrain properties, including required grip/up magnets. Mandatory mechanics come from the pack; there are no global terrain switches. |
| MAX and Bower | Use their own imported course data and native magnet rules. Enabling the CGP preset does not change the meaning of their magnet tiles. |
| Astra Front | Adds ten courses in Astra and Front. Uses the same author's CGP grip/up-magnet mechanics, scoped to these courses. Unused CGP resources and global car/rule changes in its donor are excluded. See [the import audit](docs/ASTRA_FRONT_IMPORT.md). |
| Original BS tracks / CGP BS revisions | The loader and original BS track provider exclude each other. CGP supplies its corrected versions. |
| Marine City I / Lightning | CGP trampoline behavior belongs to Marine City I; the railroad landing behavior belongs to Lightning. |
| Rainbow Road | Its required gravity and illusion collision rules follow this CGP course. |

**CGP magnets:** `DOWNPULL_MAGNET` identifies the ground magnet/grip surface.
It does not, by itself, make the surface damaging in CGP. The separate
`MAGNET` property must be enabled for magnet damage. Red Canyon III's tile
`$A6` has both properties and **does damage**. Tiles with downpull only retain
their grip/pull without that damage. Ordinary native downpull damage remains
unchanged outside courses declaring CGP's magnet mechanics.

CGP up magnets additionally depend on the author's tile IDs (`$B6`, `$CCâ€“$CF`)
and airborne/tilt logic. Those IDs are not treated as up magnets in unrelated
packs. Pack mechanics cannot force CGP tile semantics onto stock, original BS, MAX
or Bower. New packs must explicitly declare compatible mechanics in their
manifest or reference their own bundled mechanics modules. See the [technical audit](docs/CGP_COURSE_CAPABILITIES.md).

## Music, cars and presentation

| Feature | Scope and intentional differences |
| --- | --- |
| SNES music | Imported courses use the donor's original song selection, independently of their scenery or display order. MAX and Bower retain their patched soundtrack mappings. |
| CGP music | With MSU-1 off or a recording absent, the course uses its SNES mapping. The one explicit donor override is Mute City III CGP: Mute City replaces the donor's Big Blue selection, interpreting the author's original-league fallback guidance. All mappings are [listed here](docs/CGP_SNES_MUSIC.md). |
| MSU-1 | Optional. The music bundle uses the cleared PC-port archive; the smaller download still supports custom music. Unmapped packs fall back to their SNES music. See [soundtrack notes](assets/music/README.md). |
| CGP vehicles | One mod enables all three authored groups together. Each car retains its own artwork and parameters. In GP, its other three group members are its rivals; Practice permits any enabled rival. |
| Original-car rebalances | Enabled by default for fresh settings; changes all four original identities together on any course. Explicit saved choices are respected. |
| Stock BS vehicles | Preserve their native behavior; this mode excludes CGP vehicles and original-car rebalances. Track choice is independent. |
| Legend and general gameplay fixes | Legend is enabled by default for fresh settings; explicit saved choices are respected. General gameplay fixes remain editable options, and the CGP preset enables them. Legend's CPU changes apply after vehicle tuning. They can affect races outside CGP. |
| Title screen override | One Presentation mod offers Original, Community Grand Prix and MAX League independently of enabled track packs. Astra uses the same title artwork as CGP; Bower and BS use Original. F-Zero 55 remains retained but hidden. The CGP preset selects its title; Vanilla and Satellaview restore Original. |
| Ending credits ASM | Retained as attributed source only; the PC port does not install it. |
| Engine and menus | Course imports use the shared engine, renderer, HUD/culling fixes and expanded native-style menus. They do not replace the game with the donor ROM or import every donor code patch. |

Same-named courses in different packs can be different revisions. They retain
separate identities and records; names alone are not used to deduplicate them.
The records browser supports enabled cups and individual vehicle records.
Grand Prix and Practice share one set of times for each course and car.
There is no mode switch in Records. Better Practice times saved by earlier
test builds are included automatically; existing GP times remain available.
Imported courses show their own scenery, and individual records use that car's
icon. Newly saved CGP Practice ghosts retain their selected car. Existing ghosts
saved by older builds may retain the old donor identity; record a new ghost to
replace one. Old vehicle save states are incompatible with the corrected ghost
workspace; battery records remain available.

The Huckmine editor project differs slightly from the older ROM extraction.
Zenith therefore starts a new set of records; its previous records stay on disk.
Other cups keep their record identities.

## Keeping these notes accurate

For every imported pack, record its source revision, authored course and cup
names, race order, SNES music, required mechanics, intentional deviations and
remaining limitations. Preserve attribution. Do not infer music from venue
art, assume tile IDs have universal meanings, or copy course-specific ASM
over the whole game.

The [import checklist](mods/PARSE_MANIFEST.md) covers machine checks and manual
review. The metadata audit records donor versus selected songs, original
course names and omitted courses in partial packs. Cup labels still require
review against the donor menu or author's documentation. An unknown loader
needs a reviewed adapter, and a structural pass alone does not establish
full-race playability.

Imported intro lettering must be checked against the donor's actual font lookup,
not just decoded name bytes. Astra remaps its font; its authored R and Z graphics
are imported as scoped letter resources. Course and record IDs are unchanged.
