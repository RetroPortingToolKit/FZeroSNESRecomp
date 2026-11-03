# Two to four player VS

Tracking: `beads-8wg.5.120`. Branch: `feat/fzero-four-player`, based on
`origin/main` at `794d602c5f450014e77896a151cadffec572745e`.
The active delivery target is offline play, ready for final human validation.
Online stress qualification remains separate under `beads-8wg.2.190`.

## Behavior

Local VS supports two, three or four humans in one simulation. Two players use
top/bottom views; three/four use a 2-by-2 grid, with the unused fourth cell black.
Choose the saved default in Mods > Local Multiplayer > Players. Player 1 through
Player 4 can be configured in launcher Controls. Press Y on P1's unready
machine-selection page to change the count in-game. Each seat
joins, chooses a machine and readies independently. Gamepads have one owner.
Explicit keyboard assignments can use separate
`keybinds.ini` player1..player4 layouts, or repeat a layout to control multiple
racers with the same keys. Automatic keyboard joining still claims only one seat.
Floating labels show only P1/P2/P3/P4; HUD and results rankings remain available.
Duplicate machines remain legal. League
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
launcher offers two to four seats for online, LAN and Direct IP rooms. F-Zero
opts into the shared revisioned four-seat LAN adapter; legacy consumers keep
their two-seat default. All occupied seats must accept the current room settings
and be ready before launch. Rejected rollback experiments are not included.

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
| Four keyboard layouts through actual config/keybind loading, host input conversion, four moving ships, Ready and live menus | `captures/keyboard4-config-final/` | Pass; synthetic key matrix; physical key rollover remains a human check |
| 2P HD desktop regression | `captures/offline2-regression/` | Pass |
| 4P BS, machines 4..7: save/load, rewind, fresh-process continuation, changing viewports, exact replay, divergent/corrupt state rejection, native-save isolation | `captures/bs4/results.json` | Pass |
| 4P imported course, machines 8..11: same state/replay lifecycle | `captures/imp4/results.json` | Pass |
| Four-seat LAN/Direct IP room, real readiness/launch record, common execution profile before frame 0, four independent players driving | `captures/followup-final/lobbies/lan4-fixed/` | Pass; 1,453 matching trace ticks, all four reach tick 1,460 with zero desyncs |
| Two-seat lobby regression through the same opted-in adapter | `captures/followup-final/lobbies/lan2-fixed/` | Pass; 1,459 matching trace ticks |
| Shared LAN room/lobby/rematch/beacon tests and launcher backend/seat selector | `captures/net-lobby/build-retry.log` | Pass; four UDP sockets, readiness, seat moves and launch handoff; opt-in LAN ceiling four |
| GPU affine/Mode 7 pixel oracle, including large non-periodic maps and intermediate scales above 32x | `captures/high-scale/validate-game.log` | 576 ROI comparisons pass through 64x on RTX 3080 Ti |
| Real single-camera native/32x/48x/64x and two-camera 32x/48x/64x across the three stock leagues, resize/live-menu flow, plus native 4P | `captures/followup-final/summary.json` and `graphics/` | Pass on GTX 760; 61,149 sampled GPU pixels match the portable consumer; actual presenter captures visually reviewed |
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
The keyboard config/menu gate also omits this unrelated pose fixture, retaining
the existing stock SDL-device contact coverage.

## High-scale graphics follow-up

32x/48x/64x select the HD Mode 7 spatial sampling scale. The game keeps native
simulation and sprite/HUD artwork; the default nearest presenter evaluates a
bounded window-sized surface rather than allocating the entire requested raster.
Local 3/4P still uses the separate 2x CPU fallback described above.

No corruption was reproduced in the representative real-game scenes above.
The suspected Q16 overflow was rejected by measurement: the existing affine
fast-path guard already bounds its intermediates. The production shader remains
unchanged. New coverage exercises both signs near that guard, steeper fallback
rows, large maps, non-power-of-two scales and actual game-frame pixel samples.
The original 64x two-camera run on this PC is retained in
`captures/high-scale/baseline64/`; SERVER supplied the additional GTX 760 checks
with its smaller 16K texture limit. These were default nearest/unfiltered checks,
not qualification of every course, custom shader, filtering preset or GPU driver.

Four-seat lobby qualification also found and fixed a real shared bridge defect:
`snes_host_app_apply_launch` now copies the settled player count, rather than
leaving the two-seat default. The game test removes direct-connect environment
overrides and consumes the actual room session ID, endpoints and slot count.
It qualifies loopback waiting rooms and normal driving; WAN/ICE and dense forced
rollback remain separate acceptance gaps.

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

The selector defaults to **Keyboard controls everyone**, requiring no gamepads.
Enter readies/confirms, Z accelerates every racer, arrows steer, X boosts, D/C
lean and S opens P1's count selector before readying. Other input modes are two
keyboard groups (P1/P3 use those keys; P2/P4 use I/J/K/L, F/G, U/O and Space),
one gamepad plus keyboard for the other racers, two pads plus keyboard, or four
pads. Every keyboard seat has an explicit player layout in the disposable
`keybinds.ini`. Existing owner settings/saves are not edited.

The three bundles cover race/results and live menus; input groups and optional
pad recovery; and 3P/2P presentation, resizing, content, audio and current-build
save/load. Native/HD and stock/custom/BS choices are explicit. Mirrored controls
are useful for four-view/race checks but do not certify four independent physical
pads. Human natural-driving/visual/audio judgment and any chosen hardware checks
remain pending. Close the game before changing setups. The first setup waits in
the machine picker in a normal window with normal audio.

The selector also exposes **Boost voice only at lap 2**, and **Configure audio /
mods...** opens the existing launcher to select/enable the callout recordings.
Changing input setups preserves `LapCallouts`, `LapBoostWav` and `FinalLapWav`.
The timing checkbox writes `LapBoostSecondLapOnly`; final-lap timing is separate.

Save Pass/Fail/Deferred observations and individual caveats in the selector.
`human-observations.json` retains separate results per setup/input mode/content/rendering/boost-timing
combination. No human acceptance is claimed yet. Read those observations before
follow-up; one bundle passing does not clear another bundle's failure.

Online dense loss plus forced rollback still fails shared baseline checks
(`beads-8wg.2.190`). Passing loss-only 3/4-peer cases do not clear that gap;
historical forced-correction experiments are not current-build qualification.
Linux/cross-platform and WAN/ICE checks are outside this offline handoff.

The owner authorized main integration and Nextcloud delivery on 2026-11-01.
Version 0.15.0 carries this implementation in both Windows music editions.
Keep useful raw evidence and the human profile; the release authorization does
not turn unrecorded human observations into passed checks. Delivery and source
identities are recorded in the owning issue, `beads-8wg.5.120`.
