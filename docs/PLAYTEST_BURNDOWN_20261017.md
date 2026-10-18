# F-Zero Forever playtest burndown

Tracking: central Beads `beads-iiw8`. Starting revision `2206083` on
`feat/fzero-local-2p`. Owner reports dated 2026-10-17, including the Discord
image showing horizontal track shading and the GIVE UP prompt.

| ID | Report and required outcome | Status |
| --- | --- | --- |
| B01 | Brand future executable, launcher, window and packages F-Zero Forever. | Done |
| B02 | Enable MSU-1 for fresh settings; preserve explicit user choice and stock fallback. | Done |
| B03 | Production Windows builds use the GUI subsystem and show errors without a console. | Done |
| B04 | Validate exact USA ROM, including copier-header normalization, at all entry paths. | Done |
| B05 | Remove multiplayer pause and resume input lag. The results-only menu requirement was superseded by N06 on 2026-10-18: Start opens a menu while the race continues. | Superseded; see the 2026-10-18 burndown |
| B06 | Respect separate launcher keyboard/controller seats, including Steam virtual-controller cases. | Fixed; physical Steam QA remains |
| B07 | Explain which mods apply to single-player, versus, courses, and machines. | Done |
| B08 | Maximum correct full-course draw distance in single-player and both versus views; investigate border glitches and banding. | Done |
| B09 | Landing in void must cause death rather than bouncing/glitching back onto track. | Done |
| B10 | Diagnose startup/high-pitched audio and verify race music/effects transitions. | Fixed; listening QA remains |
| B11 | Restore S-jet black outline and a proper oval vehicle shadow. | Done |

Automated/background launches use dummy audio or zero output volume. Interactive
owner QA builds retain normal audio. Physical controller/Steam and subjective
audio quality checks must be reported separately from automated evidence.

## Changes and findings

- **Production:** Windows desktop uses PE GUI subsystem 2; headless remains a
  console tool. The desktop executable, launcher, window, Windows package slug,
  Linux executable and AppImage metadata use F-Zero Forever. The internal CMake
  target remains `FZeroSNESRecomp`. Fresh launcher settings enable MSU-1 before
  loading saved choices. Direct/headless entry requires the exact USA ROM hash
  after optional 512-byte copier-header removal; incorrect sizes are rejected
  before running. Desktop rejection also has a message box.
- **Multiplayer:** Start, P, Escape, focus loss, minimized windows and missing
  controllers cannot pause the race. Save/rewind browsers and quick actions
  cannot interrupt an active race. Disconnected seats send neutral input and
  can reconnect. Menus contain rematch/change/main only after results. Versus
  rules and compatibility identity advance to 7; paused/older-rule snapshots
  are incompatible. Single-player retains its native pause.
- **Controls:** local seat assignment now reads the launcher's input sources
  and selected controller GUIDs. Join order cannot steal the keyboard seat.
  Two selected devices cannot drive the same local seat. Steam keyboard
  emulation generates ordinary OS keyboard events, so physical Steam QA still
  needs its desktop keyboard bindings disabled for a separate keyboard player.
- **Mods:** launcher descriptions/status labels distinguish single-player
  rules/vehicles from presentation and shared course content. The packaged
  [controls and mods guide](LOCAL_CONTROLS_AND_MODS.md) explains the actual
  twelve-machine versus selector and Steam setup.
- **Rendering:** every race view samples the complete course rather than
  treating the guest's streamed 1024-unit VRAM square as the world boundary.
  This now includes single-player and 4:3. Uninitialized course tables retain
  native sampling. VS clears the original shadow-window math as well as the
  stepped distance haze, avoiding the dark seam when those windows expand.
  Single-player retains localized shadow/HUD math. Optional **Track texture
  smoothing**, formerly Smooth fog, filters the road in both modes independently
  of HD Mode 7. It softens texture dithering; authored broad road-color strips
  remain visible. It does not supply higher-color replacement road artwork.
- **Void landing:** imported terrain properties were being read from temporary
  ground-response flags that airborne ticks clear. Landing now reads the actual
  immutable terrain property table, so lethal void cannot become safe road.
  Native-course altitude remains covered by the CPU parity oracle.
- **Audio:** the rev program was starting during the 68-tick arrival animation
  with an engine command of zero. It now starts at the native arrival/rev
  boundary, with a valid minimum engine command; course music still starts at
  GO. Automated SPC checks pass, but perceived sound quality needs listening.
- **Art:** S-jet retains nontransparent black palette entries for its outline.
  Vehicle shadows are bounded, alpha-blended ovals with distance scaling.

## Validation evidence

Private captures/builds stay in ignored workspace folders. These checks use
dummy audio in child environments only; QA settings retain normal output.

| Check | Result | Local evidence |
| --- | --- | --- |
| Windows build and all CTests | 18/18 pass, 27.93 s; includes 1.2 million scalar comparisons and targeted regressions | `captures/fb17-final-build.log`, `captures/fb17-final-ctest.log` |
| Production ROM/MSU contract | GUI subsystem; raw/headered accepted; corrupt/truncated/oversized/empty refused by both hosts; default on and explicit off persist | `captures/fb17-prod-final/`, `tests/validate_production_contract.py` |
| Local lifecycle, replay, state validation and course setups | Pass; all 15 stock setups, tamper refusal, fresh-process reload and viewport-independent state | `captures/fb17-vs2/`, `captures/fb17-vs2.log` |
| Actual SDL desktop local controls/results | Pass; two virtual devices, native title entry, five resizes, uninterrupted Start, rematch/change/main and return to single-player | `captures/fb17-ui2/run.log` |
| Actual desktop texture smoothing | Pass; 1,646 simulation frames; both views/HUD/shadows inspected | `captures/fb17-ui-smooth/run.log`, `captures/fb17-ui-smooth/drive-0.png` |
| SPC/music/effects | All 13 cases pass, including stock/imported courses, Start ignored in VS and native SP pause/resume | `captures/fb17-audio2/report.json` |
| Actual peers, Start ignored with different native/HD presentation | Pass; 1,458 matching confirmed ticks | `captures/fb17-net3/report.json` |
| Healthy void landing on imported Astra and stock Mute City | Both pass; each 1,158 matching ticks and actual DNF/survivor results | `captures/fb17-crash/report.json`, `captures/fb17-crash-stock/report.json` |
| Linux desktop/headless and representative regressions | Compile/link pass; actor parity and renderer tests pass | `captures/fb17-linux-final.log`, `captures/fb17-linux-actor.log`, `captures/fb17-linux-renderer.log` |

The crash fixture now starts at positive health `0x800` and chooses an actual
lethal surface. It sets only a falling pose; the core awards death and results.
The prior fixture's negative starting health was insufficient evidence for this
report. Desktop results-menu tests use `EndRace` as a UI fixture, not proof of a
natural five-lap drive. The all-course lifecycle test similarly uses checkpoint
teleports and does not certify human driving through every course.

A separate loss-injected netplay run correctly failed qualification because
startup continued UNVERIFIED after a timeout (existing `beads-8wg.5.104`). It had
no simulation desync, but is **not** a passing compatibility test. Normal peer
startup passed. This burndown does not claim to resolve that separate issue.

## Owner QA and handoff

Run `build/F-Zero-Forever-QA/F-Zero Forever.exe`. This isolated installation
uses normal audio at volume 100, MSU-1 on, keyboard P1/controller P2 and Local
2 Player on. Choose your USA ROM and controller in the launcher, then select
VS MODE. Its `mods/packs` is a local junction to the existing reviewed catalog;
the QA folder is a workspace installation, not a portable release ZIP.

- [ ] With the physical Switch Pro controller and keyboard, confirm each car
  page and racer responds only to its configured device. Repeat with Steam
  closed, then with Steam Input configured without desktop keyboard emulation.
- [ ] Listen to first-race arrival/rev/GO and a rematch with both native music
  and an installed MSU soundtrack. Record any remaining high-pitched sound.
- [ ] Play tight borders and jumps on the originally reported course. Compare
  Track texture smoothing on/off and confirm the desired texture appearance.

All eleven code items are implemented; B06/B10 remain open in the central
tracker for the physical-device/listening acceptance checks above. No release
or public upload was performed.
