# FZeroSNESRecomp

A native PC build of *F-Zero* for SNES.

The `f-zero-forever` branch includes **Track Pack Loader**, enabled by default
and freely switchable in Mods.
Its bundled CGP, Astra Front, Bower and MAX packs add **75 course versions in
15 cups**, alongside the untouched original 15 courses. Drop a compatible
folder or ZIP into `mods/packs` and restart. All installed packs load together.
BS Satellaview Tracks excludes the loader; vehicle options remain independent.
Optional race music uses the same filename as the course source, in its pack's
`music/` folder: `courses/example.fzc` or `example.fzm` uses `music/example.pcm`.
Restart and enable **Settings > Audio > MSU-1 > Installed pack music**.

See [pack authoring and music](docs/PACK_FORMAT.md) for JSON manifests,
raw FZEdit exports, extracted course resources and separate audio packs.
[Mods and source-game differences](MODS.md) describes scoped mechanics.

The current local tester release is **0.4.0**. Future fixes advance the patch
version; new features advance the minor version. All bundle variants share
that version. See [the release policy](docs/RELEASING_FZERO_FOREVER.md).

You bring your own legally dumped *F-Zero (USA)* ROM. No ROM is included.

<p align="center">
  <img src="docs/screenshots/bs-forest-iii-race-21x9.png" width="96%" alt="BS F-Zero Deluxe Forest III race in 21:9">
  <br>
  <img src="docs/screenshots/widescreen-title.png" width="48%" alt="F-Zero title screen in widescreen">
  <img src="docs/screenshots/bs-blue-thunder.png" width="48%" alt="BS F-Zero Deluxe Blue Thunder machine select">
  <br>
  <img src="docs/screenshots/bs-forest-iii-race.png" width="48%" alt="BS F-Zero Deluxe Forest III race in 16:9">
  <img src="docs/screenshots/bs-forest-iii.png" width="48%" alt="BS F-Zero Deluxe Forest III course select">
</p>

## Features

- Play *F-Zero* as a native app.
- Widescreen modes: 16:9, 21:9, 32:9, and Fit.
- Optional [HD Mode 7](docs/HD_MODE7.md): 2x through 10x track rendering, independent of widescreen and presentation FPS.
- High refresh presentation: 60, 90, 120, 144, 165, 240, or 360 FPS.
- Display shaders: CRT Soft, LCD Grid, Sharp, Warm Composite, or your own GLSL shader.
- Save states with a slot browser and thumbnails, opened with **F7** or **Select + R**.
- Rewind: step back through the last few seconds and drop back in.
- Gamepad support through SDL.
- Optional BS F-Zero Deluxe content.
- Discoverable [track and audio packs](docs/PACK_FORMAT.md), including raw FZEdit course exports. Reviewed ROM/IPS/BPS conversion is an author-side step.
- Optional bundled CGP soundtrack, custom MSU-1 packs and editable content presets.

## Download And Play

On Windows:

1. Download the Windows x64 ZIP from the Releases page.
2. Extract the whole ZIP.
3. Run `FZeroSNESRecomp.exe`.
4. Pick your own *F-Zero (USA)* `.sfc` or `.smc` ROM when asked.

On Linux:

1. Download the Linux AppImage from the Releases page.
2. Put your own *F-Zero (USA)* `.sfc` or `.smc` ROM next to it.
3. Make the AppImage executable.
4. Run it.

The app remembers your ROM path after the first launch.

Enable **Skip launcher on boot** to start directly with that ROM next time.
The choice is saved as `[General] SkipLauncher=1` in `config.ini` beside the
executable. Run `FZeroSNESRecomp.exe --launcher` (on Linux, run your AppImage
with `--launcher`) or set the value to `0` to return to the launcher. The override also works
with a ROM path before or after it. A missing or invalid remembered ROM opens
the launcher so you can select a valid copy.

## Settings

Open **Settings > Display** for:

- **Aspect ratio:** 4:3, 16:9, 21:9, 32:9, or Fit.
- **Shader:** None (default), CRT Soft, LCD Grid, Sharp, Warm Composite, or a custom shader.

Open **Mods** for:

- **Widescreen:** makes races wider.
- **Presentation FPS:** makes motion smoother on high refresh screens.
- **HD Mode 7:** sharper tracks at integer scales from 2x to 10x; off by default.
- **Diagnostics:** optional local performance reports for troubleshooting; off by default. See [how to capture a report](docs/PERFORMANCE_DIAGNOSTICS.md).
  Start at 2x. Above 4x can cause severe slowdown; use at your own risk.
- **BS Satellaview vehicles:** adds four machines independently of courses.
- **BS Satellaview tracks:** adds the ten original BS courses; mutually exclusive with Track Pack Loader, whose CGP pack supplies corrected versions.
- **CGP vehicles:** one opt-in mod for all three authored car groups (eight additional ships plus the original four). The original-car rebalances are one separate opt-in.
- **CGP rules and fixes:** optional gameplay rules and fixes. See [source coverage](mods/cgp-source/README.md).
- **Community Grand Prix (prototype):** a bundled course pack with one enable checkbox. Every enabled track pack adds its cups to the in-game leagues. See [installation and manifest instructions](mods/README.md).

When the Widescreen mod is on, its aspect setting wins over the normal Display aspect setting.

### Importing CRT-Geom or another shader

In **Settings > Display**, use **Browse** beside Shader to select your own
`.glslp` preset or `.glsl` shader. Keep the preset's accompanying files and
subdirectories intact: for example, `crt-geom.glslp` needs
`shaders/crt-geom.glsl` beside it. The app remembers the selected path; it
does not copy the pack, so leave it in a permanent location. RetroArch Slang
(`.slangp`) presets are not supported by this OpenGL path.

[CRT-Geom is available upstream](https://github.com/libretro/glsl-shaders/tree/master/crt).
It carries GPL-2.0-or-later terms. It is **not bundled**: redistribution
compatibility with this app's differently licensed dependencies has not been
established. User-selected CRT-Geom has been tested through the existing shader
loader. An unreadable or invalid preset falls back to unfiltered output.

### Presets and MSU-1 music

**Soundtrack source:** the owner confirms the replacement PC-port archive
already reconciles attribution. Historical exclusions remain enforced.
See [source clearance and evidence](assets/music/CGP_ATTRIBUTION.md).

**Mods > Preset** offers Vanilla, Satellaview and Community Grand Prix.
Each applies an editable recipe. CGP enables Track Pack Loader, which loads
all installed packs; Vanilla and Satellaview disable it. Display, controls
and rewind remain untouched.
CGP enables all its courses, twelve cars, original-car rebalances, rules
including Legend, Community Grand Prix title and bundled music. Individual
options remain editable. The console ending-credits patch is retired. F-Zero 55
title artwork is retained but hidden for a future course pack. No preset is applied automatically on startup.

Windows previews come in **with-msu** and **without-msu** ZIPs. Both support
MSU playback; only with-msu includes the replacement **Community Grand Prix PC-port**
soundtrack (29 attribution-cleared recordings). In **Settings > Audio**, enable **MSU-1 music** and leave its source
on **Installed pack music**, or select the CGP preset in Mods. Music starts
off on a fresh installation. Missing tracks and unrelated track packs use
SNES music. Track selection works in both Grand Prix and Practice.

The smaller without-msu download contains no soundtrack files. Its Audio
settings let you browse for your own music folder. The CGP preset uses that
custom music when configured, or SNES audio until you supply it. All courses,
cars, gameplay options and other assets are identical between the two ZIPs.

**Custom...** opens a picker for your pack's `.msu` file. Existing custom
folder settings are retained. Standard packs require the supported
**Conn/Cubear v11** `f-zero_msu1.ips` beside their numbered PCM files (available
from the [authors' page](https://www.zeldix.net/t2768-bs-f-zero-deluxe-msu-1)).
Keep the pack's original prefix and track numbering. Packs without this
patch use CGP numbering. The v11 patch and third-party custom audio are not
bundled. Unsupported patch versions are refused with an original-audio fallback.

Keep using your **unmodified USA ROM**. Adapters apply in memory, including
all derived vehicle images; no ROM file is rewritten. Save states and rewind
restart the selected song from its beginning rather than its exact playback
position. Existing non-MSU record/save namespaces remain separate.

For command-line use, `SNESRECOMP_MSU1` can select a `.msu` file, folder or
filename prefix; `off` overrides a saved enabled setting. `FZERO_MSU1_PATCH`
can point to the v11 IPS in another folder. Headless CGP music also requires
`FZERO_RULES=cgp-msu` (or adding `cgp-msu` to other chosen rules).

See [preset behavior and validation](docs/CGP_MUSIC_AND_PRESETS.md) and
[soundtrack provenance / source-build import](assets/music/README.md).

## Save States And Rewind

### The save-state menu

Press **F7**, or hold **Select + R** on a gamepad, to open the save-state
browser. The game freezes while it is open, so a state you take is that exact
moment.

- **Up / Down** or the **arrow keys** pick one of 12 numbered slots or the **Quick** slot.
- **A** on a pad, or **X** on the keyboard, loads the selected slot.
- **X** on a pad, or **S** on the keyboard, saves to it.
- **B** on a pad, or **Escape**, closes the menu without doing anything.
- **1**-**9** jump straight to a slot.
- **Q** jumps to the Quick slot.

Each slot shows a thumbnail of the moment it was saved, so you can tell them
apart without loading them. The browser stays centered at 4:3, including in
widescreen windows.

### Quick save and quick load

Open **Controls** in recomp-ui and find **Host Shortcuts**. Assign **Quick Save**
and **Quick Load** to keyboard keys, controller buttons, or controller button
combinations. These start unbound so they don't take over a gameplay button.

Quick Save overwrites the dedicated Quick slot. Quick Load restores it. Each
press performs one action, even if you hold the button. A message confirms the
result. The Quick slot also appears in the save browser and has a thumbnail;
it is separate from your 12 numbered saves.
The Quick slot stays accessible when you return to the title or change courses.

### Rewind

Press **R**, or hold **Select + L** on a gamepad, to open the rewind
filmstrip. It shows the recent past as a strip of frames:

- **Left / Right** scrub back and forward. Hold a direction to keep scrubbing.
- **A** on a pad, or **Enter** / **Space**, jumps to the selected moment.
- **B** on a pad, or **Escape**, leaves without changing anything.

Rewind is **on by default**. You can turn it off in the launcher's **Settings**,
where you can also set:

- **Rewind depth:** how many snapshots to keep (50, 100, 150, or 200).
- **Rewind interval:** how many frames apart they are (1, 4, 8, 12, 15, or 30).

More snapshots make the history longer; a shorter interval makes it finer.
Both cost memory: one snapshot of *F-Zero* is about 330 KB, so 100 of them is
roughly 33 MB. At the default 50 snapshots every 15 frames you can step back
about 12 seconds.

### Changing the keys

Both keys are rebindable in the launcher's **Controls** page, as
**SaveStateMenu** and **Rewind**. They are saved to `config.ini` next to the
executable and take effect the next time you start the game. The rewind
switch, depth and interval are remembered in the same file.

F-Zero's keyboard shoulder defaults are **D = L** and **C = R**. These and
the rewind binding apply only to F-Zero; saved custom bindings remain respected.
**Ctrl+R** still resets the game.

The numbered-slot keys - **F1**-**F12** to load a slot, **Shift + F1**-**F12**
to save one - still work. **F7** is reserved for the save-state browser by
default; its slot is available through that browser. **F8** is now a normal
numbered-slot key. Rebinding the browser elsewhere frees F7 as well. A key
assigned to Quick Save or Quick Load takes priority over its old action.

### Where states are kept

Stock *F-Zero* and BS F-Zero Deluxe keep separate states, because they are
different cartridges and their snapshots are not interchangeable:

| Mode | Slot files |
| --- | --- |
| Stock | `saves/fzero<N>.sav` |
| BS F-Zero Deluxe | `saves/bs-deluxe/fzero-bs-deluxe<N>.sav` |
| Stock + MSU-1 | `saves/msu1/fzero-msu1<N>.sav` |
| BS Deluxe + MSU-1 | `saves/bs-deluxe/msu1/fzero-bs-deluxe-msu1<N>.sav` |

A thumbnail sits beside each as `.sav.thumb`. If a state from the other mode
somehow ends up in a slot, loading it is refused and the game keeps running -
the title bar says so and nothing is disturbed.

## BS F-Zero Deluxe

BS F-Zero Deluxe is included with permission from its authors:
GuyPerfect, Porthor, and PowerPanda.

The release includes two BS Deluxe files:

- `mods/bs-deluxe.dat` is used by this app.
- `patches/bs-deluxe-usa.ips` is the upstream v1.1 USA SNES patch for your own ROM.

No patched ROM is included.

## If The Game Crashes

Send these files from the game folder:

- `crash_report_*.json`
- `crash_minidump_*.dmp`
- `last_run_report.json`

## Build From Source

Clone with submodules:

```bash
git clone --recurse-submodules git@github.com:mstan/FZeroSNESRecomp.git
cd FZeroSNESRecomp
```

Put your own USA ROM at `fzero.sfc`, then generate and build:

```bash
bash tools/regen.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Local ROMs, generated files, saves, captures, and builds are ignored by git.

Run the ROM-free regression suites with `ctest --test-dir build --output-on-failure`.
With your local ROM and the upstream patch ZIP, `tests/test_msu_integration.py`
checks stock/Deluxe playback using generated test tones, missing-track fallback
and invalid-patch rejection. On Windows, `tests/test_desktop_integration.py`
additionally checks real launcher persistence, imported shaders, controller
overlays and save/load. Both scripts accept `--help` and keep their test data
under ignored `captures/` directories; neither bundles music or a shader.

`python tests/test_rom_persistence.py --source build` checks ROM selection
through the actual Windows file dialog, then Play/close and repeated restarts.
It also checks cancellation, an invalid pick, and selecting a moved ROM. It
requires an interactive desktop and never seeds the ROM cache itself.

`python tests/test_launcher_options.py --source build --rom PATH
--output captures/launcher-options` exercises the real Skip Launcher control,
relaunch/recovery, and typed HD resolution persistence. On Linux, prefix it
with `xvfb-run -a`. `--appimage-layout` also checks settings beside an AppImage
instead of inside its executable directory, using the AppImage environment.

On Linux, `xvfb-run -a python3 tests/test_appimage_rom_persistence.py
--appimage PATH --output captures/rom-persistence-linux` exercises the packaged
AppImage through the real zenity picker, then Play/quit and repeated relaunches.
It requires `zenity` and `xdotool`; use a fresh output directory for each run.

## License

MIT License, Copyright (c) 2026 Matthew Stanley. See `LICENSE`. Bundled dependencies keep their own licenses under `licenses/` in each release; BS F-Zero Deluxe content is included with its authors' permission and is not covered by this license.

*F-Zero* belongs to Nintendo. The game ROM is not included.
