# Community Grand Prix soundtrack

The project owner initially approved bundling CGP music on 2026-09-23, then
requested removing all CosmicTailz and TheBlurCafe contributions. Eight exact
CosmicTailz matches remain excluded. On 2026-09-24 the owner confirmed that
the replacement PC-port archive already reconciles the attribution requirement.
See [source clearance and historical evidence](CGP_ATTRIBUTION.md).

Project leads are Worthy MF and
Fennor Virastar; see `../track-packs/cgp-credits.txt` for the project credits.
The included MSU adapter source credits Conn, Khilendel and Catador in
`mods/cgp-source/fzedit-msu.asm`. This permission does not change the licenses
or ownership of the music.

The complete source is now **F-Zero CGP P1 MSU PCPORT.zip**, replacing the
older P1/P2/P3 soundtrack archives. It contains 29 PCM recordings (about 567 MB
uncompressed) and an empty descriptor. The private author import retains a
common `cgp` prefix. Player race songs are staged as `music/<course filename>.pcm`
inside the CGP course pack, without changing audio or loop points. `cgp.json` records their hashes,
the eight author exclusions, and 24 superseded recordings to physically remove
from old staging directories. Missing recordings use the game's SNES music.
These 29 recordings are the owner-confirmed, attribution-cleared bundle.

The canonical extracted set is `music/cgp`. Excluded and superseded recordings
are physically removed after checking their hashes; Git ignores are only a
secondary guard. No ROM or unrelated archive contents are extracted.

Select **Settings > Audio > Enable MSU-1 music**, or apply the CGP preset in
Mods. Music starts off on a fresh installation. Recordings are discovered
from each installed pack's `music/` folder; there is no source picker.
The smaller **without-msu** download omits the soundtrack while retaining MSU
support. Add recordings matching course filenames to each pack's `music/`
folder and restart, then enable MSU-1. Missing songs use SNES audio.

## Building from source

Large PCM files are not stored in Git. Import the complete replacement archive:

```powershell
python tools/import_cgp_music.py 'E:\Downloads\F-Zero CGP P1 MSU PCPORT.zip'
```

This creates the ignored `music/cgp` directory. Reconfigure and build to stage
it beside the executable under `mods/packs/cgp/music`. The release tool validates
the 29 retained files before packaging and rejects excluded or unexpected files.
Do not place arbitrary user
music in this directory. For an old import, run
`python tools/import_cgp_music.py --prune-retired music/cgp` before building.
Previously generated music ZIPs that contained the excluded files have been
physically deleted from this worktree, along with reference music archives and
leftover excluded PCM copies in older capture/staging folders.

The separately supported Conn/Cubear v11 patch and third-party custom packs
are user-supplied and are not part of this soundtrack bundle.

## Astra Front and menu songs

The supplied `astra front msu (1).zip` contains ten course recordings. Import
it with `python tools/import_astra_music.py "path/to/astra front msu (1).zip"`.
The reviewed hashes and loop points are in `astra-front.json`. The source set
stays in ignored `music/astra-front` and can be staged into an independently
installed Astra Front pack.

## Astra Velocity

The MSU edition includes twenty course recordings from the supplied
`F-Zero Astra Velocity (CGP) P3.zip`. Its patch confirms the imported course
names, order and arithmetic music selector. The importer preserves PCM bytes
and loop points, staging them by course filename in `mods/packs/astra-velocity/music`.
The file identities and mapping are recorded in `astra-velocity.json`.

```powershell
python tools/import_astra_velocity_music.py 'path/to/F-Zero Astra Velocity (CGP) P3.zip'
```

This creates ignored `music/astra-velocity`. The release packager verifies it
before including recordings; the without-MSU edition retains the authored SNES
music and supports adding these same files later.

CGP's six non-course recordings are declared in its `courses.json`, using the
reference template in `cgp-menu.json`. **Mods > Menu and event music** displays
them even in a download without audio. Browse selects a replacement; Clear
restores that event's pack default. See [MODS.md](../../MODS.md) for the event
names, exact filenames and installation steps. Astra course music uses its
own filenames and leaves these menu choices intact.
