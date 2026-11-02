# F-Zero Forever 0.16.0: tester notes

Extract the entire ZIP and run `F-Zero Forever.exe`. Select your original
F-Zero (USA) ROM. Both editions have the same game, courses and prebuilt caches.
With MSU includes approved CGP/Astra music; without MSU supports adding it later.
Preserve your existing native saves and settings when updating an installation.

## HD Mode 7

The renderer reconstructs continuous perspective and fixes the brief wrong-track
regions seen during motion at camera wrap points. Native sprites/HUD keep their
pixel artwork. HD defaults to8x; Mods offers1..64x GPU resolution and independent
supersampling1..4 (1=off). Supersampling smooths edges and costs more rendering
work. Local3P/4P continues to use the portable renderer at up to2x.

## Existing content and controls

The original15 courses and the reviewed CGP, Astra Front, Bower and MAX packs
remain available:75 additional course versions in15 cups. Required course
mechanics load automatically. Track Pack Loader and BS Tracks exclude each
other; vehicles remain separate options. Course projects ship with ready caches.

MSU and installed-pack music default on; saved choices are respected and missing
songs fall back to SNES music. Both editions retain the content importer,
credits, profiles, multiplayer and optional lap voice callouts.

Single-player: F7 opens save slots, R opens rewind, D/C are shoulder controls,
Alt+Enter toggles fullscreen. Local Multiplayer defaults on; choose VS MODE for
2..4 players. Multiplayer never pauses. Start opens its live race menu.

For a problem report, quit the game and run Collect diagnostics.cmd. Its output
stays local. Current-build snapshots are disposable and need not load across
versions; native in-game progress is a separate persistent save.
