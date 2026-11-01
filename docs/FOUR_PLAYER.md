# Two to four player VS

Tracking: `beads-8wg.5.120`. Branch: `feat/fzero-four-player`, based on
`origin/main` at `794d602c5f450014e77896a151cadffec572745e`.
The active delivery target is offline play, ready for final human validation.
Online stress qualification remains separate under `beads-8wg.2.190`.

## Behavior

Local VS supports two, three or four humans in one simulation. Two players use
top/bottom views; three/four use a 2-by-2 grid, with the unused fourth cell black.
Press Y on P1's unready machine-selection page to choose the count. Each seat
joins, chooses a machine and readies independently. Devices have one owner;
at most one seat uses the keyboard. Duplicate machines remain legal. League
and course selection retain a shared cursor. Start opens a live menu while the
other racers continue; multiplayer races do not pause.

All humans share four NPC bumpers and terrain mutations. Every human pair has
an independent contact latch. Stable seat order resolves equal live progress;
equal finish times share rank. Finished racers precede survivors and DNFs.
Current-build snapshots, rewind and replays store all active players. Old
build-specific snapshots/replays are rejected; native saves remain separate.

Local 3/4P HD uses the portable CPU renderer, capped at 2x. The shared GPU
packet currently holds only one full-width camera per scanline. GPU rendering
remains available for local 2P and the single-camera online view. Course loading
now reapplies the host viewport after native resets; the CPU HD frame has its
own local-session validity, and loading a different count refreshes the layout
and configured device assignments. Mods calls this mode **Local Multiplayer**;
legacy configuration/package IDs are retained.

Online supports two to four PCs with one player and camera per PC. Mixed couch/
online play, spectators and mid-race joins are outside this change. The shared
launcher still limits LAN/Direct IP room UI to two seats. Original engine/UI pins
are retained; rejected rollback experiments are not included in this branch.

## Build and run

Desktop: `build/F-Zero Forever.exe --players 4 PATH-TO-PRIVATE-ROM`.
Use `--players 2` or `--players 3` for other counts. Machine overrides are
`--p1-machine` through `--p4-machine` (stable IDs 0..11).
Headless: `build/FZeroSNESRecompHeadless.exe ROM FRAMES --versus COURSE P1 P2 [P3 [P4]]`.

The worktree build reuses the private stock/Deluxe generated sources under
`_wt-fzero-track-packs/captures/upstream-073/{stock,deluxe}/gen`, with the verified
embedded BS payload from `FZeroRecomp/captures/bs-deluxe/mods/bs-deluxe.dat`.
These private inputs and generated sources are not committed or distributed.

## Automated validation

Evidence is in ignored `captures/`. Tests use isolated saves/settings, muted
output and actual SDL virtual devices. Prepared, automated-pass and human-pass
are distinct states.

| Gate | Evidence | Result |
|---|---|---|
| CTest, including private-ROM actor parity and embedded BS payload | `build/Testing/Temporary/LastTest.log` | 20/20 pass; mods test now supplies installed-pack fixtures |
| 4P stock HD, actual title/count selector, four independent moving ships, both shoulders, all-view HUD resize checks, prepared NPC impacts, disconnect/reconnect, live Restart/Change/Main Menu and SP return | `captures/offline4-qualified-hd/` | Pass |
| 3P native, same lifecycle and black unused cell | `captures/offline3-qualified-native/` | Pass |
| 4P native imported-course menu/race lifecycle | `captures/offline4-import-native/` | Pass; two real extracted Astra courses, not a complete pack |
| 4P BS profile, requested 8x / actual 2x CPU, extra league, full roster profile, desktop lifecycle and SP return | `captures/offline4-bs-final/` | Pass; content/menu smoke, no prepared NPC-impact assertion |
| 2P HD desktop regression | `captures/offline2-regression/` | Pass |
| 4P BS, machines 4..7: save/load, rewind, fresh-process continuation, changing viewports, exact replay, divergent/corrupt state rejection, native-save isolation | `captures/bs4/results.json` | Pass |
| 4P imported course, machines 8..11: same state/replay lifecycle | `captures/imp4/results.json` | Pass |
| Stock 3/4P state lifecycle | `captures/state3/`, `captures/state4-final/` | Pass |
| 4P five real lap awards, results, rematch, quick slot and rewind | `captures/finish4/results.json` | Pass using explicit pose/health teleport fixture; outcomes/laps were not set |

The actor oracle also covers all six human contact pairs, independent inputs,
ranking and canonical state round trips. Controller tests cover unique seat
ownership and loading a four-seat race from two-seat controls without a new
Start/device-added event. Native/HD four-view images and the three-view black
cell have been inspected. This evidence does not certify natural full races or
physical controllers.

The BS bumper setup can put a human onto an airborne explosive NPC, which
invalidates its contact prerequisite. Its content/menu gate therefore explicitly
omits that prepared encounter. Stock 4P retains the separate all-human NPC
impact/view gate; BS driving and interactions remain part of human validation.

## Runtime cost

The retained matched native headless sample (`captures/native-cost/results.json`)
is 314.4 / 194.8 / 162.9 FPS for 2/3/4P over 1,800 frames including boot, with
25.1 / 27.0 / 28.4 MiB peak working sets. This is not a desktop frame-time or
historical 2P regression comparison.

Two short BS desktop runs exposed the cost of the initial 4x CPU fallback:
after Change Race and capture work, it presented at 30.9?33.4 FPS. The 2x cap
presented at 59.4?59.9 FPS in the corresponding short windows. Raw samples and
binary identities are in `captures/offline-cost.json`; host load was not isolated,
so these are bounded cost observations, not a general performance guarantee.
Stale single-player GPU statistics no longer override CPU-grid diagnostics.

## Human handoff

`captures/offline-human/` is the disposable staged install, with a private
representative two-course Astra pack and no copied owner saves/settings.
Run the selector using:

```powershell
& 'C:/Users/Matthew/.pyenv/pyenv-win/versions/3.12.5/python.exe' 'F:/Projects/snesrecomp/_wt-fzero-four-player/tools/offline_playtest.py' --install 'F:/Projects/snesrecomp/_wt-fzero-four-player/captures/offline-human' --rom 'F:/Projects/snesrecomp/FZeroRecomp/fzero.sfc' --start
```

The selector bundles (1) four gamepads, race/results and live menus;
(2) keyboard plus three gamepads, device ownership and disconnect/reconnect;
(3) 3P/2P presentation, resizing and content/audio. Native/HD and stock/custom/BS
choices are explicit. Four physical gamepads, or three plus a keyboard, and
human natural-driving/visual/audio judgment are prerequisites. Include a current-
build save/load in the presentation check. Close the game before changing setups.
The first setup waits in the machine picker in a normal window with normal audio.

Save Pass/Fail/Deferred observations and individual caveats in the selector.
`human-observations.json` retains separate results per setup/content/rendering
combination. No human acceptance is claimed yet. Read those observations before
follow-up; one bundle passing does not clear another bundle's failure.

Online dense loss plus forced rollback still fails shared baseline checks
(`beads-8wg.2.190`). Passing loss-only 3/4-peer cases do not clear that gap;
historical forced-correction experiments are not current-build qualification.
Linux/cross-platform and WAN/ICE checks are outside this offline handoff.

No merge, public release or delivery-folder replacement was performed. Keep the
current build, useful raw evidence and human profile until validation is resolved;
clean the disposable playtest afterwards without touching owner saves.
