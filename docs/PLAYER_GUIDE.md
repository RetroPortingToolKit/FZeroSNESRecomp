# Player guide

[Back to the README](../README.md) · [Local controls and mod scopes](LOCAL_CONTROLS_AND_MODS.md) · [Netplay](NETPLAY.md)

## Launching

Enable **Skip launcher on boot** to start directly with your remembered ROM next time.
The choice is saved as `[General] SkipLauncher=1` in `config.ini` beside the
executable. Run `F-Zero Forever.exe --launcher` (on Linux, run your AppImage
with `--launcher`) or set the value to `0` to return to the launcher. The override also works
with a ROM path before or after it. A missing or invalid remembered ROM opens
the launcher so you can select a valid copy.

## Settings

Open **Settings > Display** for:

- **Aspect ratio:** 4:3, 16:9, 21:9, 32:9, or Fit.
- **Shader:** None (default), CRT Soft, LCD Grid, Sharp, Warm Composite, or a custom shader.

Open **Mods** for:

- **Lap voice callouts:** supply your boost-power and final-lap WAV recordings.
  **Boost-power timing > Lap 2 only (once per race)** announces boost only when
  the first racer starts lap 2; the final-lap callout is unchanged.
  **Every new lap** retains the existing default behavior.
- **Widescreen:** makes races wider.
- **Presentation FPS:** makes motion smoother on high refresh screens.
- **HD Mode 7:** reconstructed perspective and finer track sampling, with a 4x default and optional edge smoothing. Local 2P/3P/4P uses the GPU; the portable grid fallback reports its 2x limit. Start at 2x when tuning performance; settings above 4x can cause severe slowdown.
- **Diagnostics:** optional local performance reports for troubleshooting; off by default. See [how to capture a report](PERFORMANCE_DIAGNOSTICS.md).
- **BS Satellaview vehicles:** adds four machines independently of courses.
- **BS Satellaview tracks:** adds the ten original BS courses; mutually exclusive with Track Pack Loader, whose CGP pack supplies corrected versions.
- **CGP vehicles:** one opt-in mod for all three authored car groups (eight additional ships plus the original four). The separate original-car rebalances default on.
- **CGP rules and fixes:** optional gameplay rules and fixes. See [source coverage](../mods/cgp-source/README.md).
- **Community Grand Prix (prototype):** a bundled course pack with one enable checkbox. Every enabled track pack adds its cups to the in-game leagues. See [installation and manifest instructions](../mods/README.md).

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
See [source clearance and evidence](../assets/music/CGP_ATTRIBUTION.md).

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
soundtrack (29 attribution-cleared recordings). In **Settings > Audio**, **MSU-1 music** defaults on for fresh settings. Leave its source
on **Installed pack music**, or select the CGP preset in Mods. Missing tracks and unrelated track packs use
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

See [preset behavior and validation](CGP_MUSIC_AND_PRESETS.md) and
[soundtrack provenance / source-build import](../assets/music/README.md).

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

## Reporting playtest problems

Recent inputs and checkpoints are recorded by default, including local and
online VS. Reproduce the problem, quit, then double-click **Collect diagnostics.cmd**
beside the game and send the ZIP it selects with an approximate time and a
description. A crash is not required. Nothing is uploaded automatically.

The ZIP includes recent replay sessions, crash reports when present and the
latest exit, performance and transport logs. It contains game memory, saves
and local paths; share it privately when appropriate. See
[recording and replay instructions](DIAGNOSTIC_REPLAY.md).
