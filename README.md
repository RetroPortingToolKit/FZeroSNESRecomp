# F-Zero Forever

A native PC build of *F-Zero* for SNES, with **75 course versions in 15
cups**, **twelve CGP machines**, local and online multiplayer, widescreen and
HD Mode 7.

You bring your own legally dumped *F-Zero (USA)* ROM. No ROM is included.

<img src="docs/screenshots/bs-forest-iii-race-21x9.png" width="96%" alt="BS F-Zero Deluxe Forest III race in 21:9">

The current local tester release is **0.18.1**. See
[the release policy](docs/RELEASING_FZERO_FOREVER.md).

- [Download and play](#download-and-play)
- [Player guide](PLAYER_GUIDE.md): settings, presets, music, save states, rewind and diagnostics
- [Building from source](BUILDING.md)

## Download and play

**Windows:** download the Windows x64 ZIP from the Releases page, extract the
whole ZIP, run `F-Zero Forever.exe` and pick your own *F-Zero (USA)* `.sfc` or
`.smc` ROM.

**Linux:** download the AppImage, put your ROM next to it, make it executable
and run it.

The app remembers your ROM. See the [player guide](PLAYER_GUIDE.md#download-and-play)
for skipping the launcher on boot.

## Track packs: 75 course versions

**Track Pack Loader** is on by default and freely switchable in Mods. Its
bundled CGP, Astra Front, Bower and MAX packs bring the game to **75 course
versions in 15 cups**. The original courses are untouched.

Drop a compatible folder or ZIP into `mods/packs` and restart. All installed
packs load together and add their cups to the in-game leagues. Raw FZEdit
course exports are supported.

<img src="docs/screenshots/bs-forest-iii.png" width="48%" alt="BS F-Zero Deluxe Forest III course select">

See [pack authoring and music](docs/PACK_FORMAT.md) and
[installation and manifest instructions](mods/README.md).

## Community Grand Prix machines

**CGP vehicles** adds all three authored car groups: eight additional ships
plus the original four, for **twelve machines** with matching artwork,
handling and boost behavior. The separate **CGP original-car rebalances**
default on. VS mode always offers the full twelve-machine selector.

<img src="docs/screenshots/bs-blue-thunder.png" width="48%" alt="BS F-Zero Deluxe Blue Thunder machine select">

[Mods and source-game differences](MODS.md) describes scoped mechanics.

## Local multiplayer and netplay

**Local Multiplayer** defaults on. Choose 2, 3 or 4 players in the mod,
configure their launcher controls, then choose **VS MODE** on the title menu.
Two players use top/bottom views; three or four use a 2x2 grid. Keyboard
layouts may be separate or shared.

[Versus netplay](docs/NETPLAY.md) gives two to four participants each a full
viewport. The host selects 4:3, 16:9 (default) or 21:9.

See [local controls and mod scopes](docs/LOCAL_CONTROLS_AND_MODS.md).

## Widescreen, HD Mode 7 and high refresh

- **Widescreen:** 16:9, 21:9, 32:9 and Fit.
- **[HD Mode 7](docs/HD_MODE7.md):** 2x, 4x and 8x track detail, optional edge
  smoothing and GPU rendering for every local player.
- **Presentation FPS:** 60, 90, 120, 144, 165, 240 or 360 FPS.

<img src="docs/screenshots/widescreen-title.png" width="48%" alt="F-Zero title screen in widescreen">
<img src="docs/screenshots/bs-forest-iii-race.png" width="48%" alt="BS F-Zero Deluxe Forest III race in 16:9">

## Music

MSU-1 and **Installed pack music** default on for fresh settings. The
**with-msu** Windows ZIP includes the replacement **Community Grand Prix
PC-port** soundtrack (29 attribution-cleared recordings); **without-msu** lets
you browse for your own music. Missing songs use the SNES soundtrack.

See [presets and MSU-1 music](PLAYER_GUIDE.md#presets-and-msu-1-music).

## Save states and rewind

Press **F7** or **Select + R** for a save-state browser with 12 numbered slots,
a Quick slot and thumbnails. Press **R** or **Select + L** for a rewind
filmstrip of the last few seconds.

<img src="docs/screenshots/attract-race.png" width="48%" alt="F-Zero attract-mode race">

See [save states and rewind](PLAYER_GUIDE.md#save-states-and-rewind).

## BS F-Zero Deluxe

Optional BS Satellaview vehicles and tracks, included with permission from
GuyPerfect, Porthor and PowerPanda. BS tracks exclude Track Pack Loader, whose
CGP pack supplies corrected versions. See
[BS F-Zero Deluxe](PLAYER_GUIDE.md#bs-f-zero-deluxe).

## And even more

- Display shaders: CRT Soft, LCD Grid, Sharp, Warm Composite or your own
  [GLSL shader](PLAYER_GUIDE.md#importing-crt-geom-or-another-shader).
- Presets: Vanilla, Satellaview and Community Grand Prix.
- Lap voice callouts with your own recordings.
- Gamepad support through SDL.
- Optional [performance diagnostics](docs/PERFORMANCE_DIAGNOSTICS.md) and
  [playtest reports](PLAYER_GUIDE.md#reporting-playtest-problems).

## License

MIT License, Copyright (c) 2026 Matthew Stanley. See `LICENSE`. Bundled dependencies keep their own licenses under `licenses/` in each release; BS F-Zero Deluxe content is included with its authors' permission and is not covered by this license.

*F-Zero* belongs to Nintendo. The game ROM is not included.
