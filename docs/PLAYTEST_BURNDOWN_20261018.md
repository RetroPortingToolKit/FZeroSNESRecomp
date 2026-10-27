# F-Zero Forever feedback burndown — 2026-10-18

Central tracker: `beads-iiw8`. Starting version 0.10.2, branch
`feat/fzero-local-2p`, revision `6e052b3`. This batch supersedes the previous
results-only multiplayer menu requirement: Start must open an in-race menu
while the simulation continues.

| ID | Feedback and acceptance | Status |
| --- | --- | --- |
| N01 | Investigate PS4 offline input locking right after about seven minutes; reproduce disconnect/remap/focus paths and retain diagnostics if physical cause cannot be reproduced. | Remap cache fixed; virtual hold/release/reconnect passes. Physical seven-minute lockup remains unreproduced. |
| N02 | Show appropriate lobby/network mods, following Mega Man X's provider pattern; distinguish shared course/race settings from local presentation and single-player-only changes. | Implemented; provider regression passes. |
| N03 | Reproduce and fix netplay menu audio; test native SPC and MSU without speaker output. | Native selection-program loader fixed. First-entry/Change Race SPC+MSU checks pass with identical canonical state. Real-peer live-menu music checks pass; subjective listening remains. |
| N04 | Measure HD Mode 7 from the sampling/composition pipeline up; implement a performant path with a portable fallback and verify visual/state equivalence. | Done: combined VS composition and filtered palette/texel caches; exact reference pixel hashes, serial regression and controlled measurements pass. See HD_MODE7_PERFORMANCE.md. |
| N05 | Fix one-frame barrier/magnet texture corruption in windowed single-player and local VS; the reporter corrected the earlier fullscreen claim. | 0.11.1 fixes a separate precision defect remaining after 0.10.3: fractional camera/centre subtraction selected an adjacent course tile. Current baseline reproduces with actual Astra artwork; corrected native/HD frames match exactly. See PLAYTEST_FEEDBACK_20261027.md for evidence and remaining reporter-route uncertainty. |
| N06 | Start opens a live-race menu with resume/restart/change/exit, without freezing the race or the other player. | Done: core/device tests and moving two-controller desktop restart/change/main-menu pass. Real peers continue simulation through both players' Start inputs. |
| N07 | Keep VS title row/outline through fades; prevent Records UI appearing before its screen. | Done: title/Records captures and automated fade regressions pass. |
| N08 | Correct league/Practice menus throughout fade-in, fade-out and confirmation; remove donor menu flashes. | Done: published-frame/native-heading detection preserves car/rival screens, replaces actual league panels throughout transitions; GP, Practice and stock regressions pass. |
| N09 | Keep imported course-name glyphs such as Z/Q valid until their sprites disappear. | Done: intro atlas follows the published visible frame; actual Astra U-Zero Z stays intact through its last ten visible frames and disappears correctly. |
| N10 | Show the appropriate machine icon beside GP lives and for the racer currently being viewed. | Done: Red Gazelle GP lives tiles match native art; VS views show each camera's machine beside its boost HUD. |

Automated launches use dummy audio or zero output volume. Owner-facing builds
retain normal audio and MSU defaults. Physical PS4 transport/mapper details and
subjective sound quality are separate from automated evidence. ZIP delivery
and old-archive removal follow the owner preference in global AGENTS.md.

The controller transport/mapper and exact sound signature are unknown. The
owner asked us to proceed without dwelling on those missing details. Diagnostics
now includes raw stick/button samples and device remap/removal events.

The first full-catalog peer run timed out during startup. An empty-catalog
retry reached the existing pre-frame-0 mod-set handshake refusal, while its
guest accepted the host identity. Neither run qualifies the new menu audio.
Those logs are retained under `captures/feedback18`; compatibility checks remain
strict. A subsequent quiet Astra run passed 1,456 matching confirmed ticks,
including both players' live menus, a guest using HD and window resizes.
The new validator reports an empty trace explicitly instead of masking a startup
refusal with an uninitialized Python variable.

Automated acceptance evidence (private captures are intentionally not committed):

- All 18 CTests pass, with the real-ROM Deluxe case enabled. The renderer also
  passes separately with `SNESRECOMP_RENDER_WORKERS=1`.
- `production-01`: production GUI subsystem 2; twelve ROM acceptance/refusal
  cases; fresh MSU/CGP-original-car/VS defaults and saved explicit opt-outs.
- `live-ui-02`: 1,510 simulation frames with two virtual controllers, actual
  driving, 2x HD and resize/aspect changes; live restart, change and main menu.
- `fades-03`, `fades-stock-02`: GP/Practice/Records/stock transitions, intact
  car-information cards and replacement league names during native fades.
- `intro-01`: final visible custom Z glyph frames and Red Gazelle GP lives icon.
- `menu-audio-02`: first entry and live Change Race, each with SPC and synthetic
  MSU; native voices and expected PCM present; final WRAM/ARAM/DSP identical
  across peer-local music choices. This exercises the real native loader and
  online reducer without relying on transport startup.
- `net-quiet-03`: real two-peer live menu/audio acceptance, 1,456 matching ticks.
- `net-change-06`: real two-peer live Change Race, native selection music and
  a second running race, 1,754 matching confirmed ticks with an HD guest.
- Filter reference/optimized CSVs cover four aspects and native/2x/4x with
  identical native/HD hashes. Filtering at 16:9 4x is 14.701 to 2.759 ms on this
  host; these are composition timings, not whole-game or tester-hardware claims.

The first forced-rollback Change Race fixture held acceleration across the
selector's release gate; the fixture now releases it before confirming cars.
The corrected forced run (`net-change-rollback-05`) completed Change Race and
the next countdown/race with identical final-execution trace rows at all 1,727
shared ticks, but its host reported one baseline WRAM fork at tick 439, before
the first course load. The driver later recovered. This run **fails** the strict
zero-desync gate and does not qualify forced rollback; retain it as a separate
shared-driver investigation (`beads-8wg.5.105`), without relaxing the gate or
changing its pin. Startup timeout evidence remains in `beads-8wg.5.104`.

N01 remains open for a physical PS4 reproduction, and N03 retains a human
listening check because the original sound signature is unknown. The virtual
controller test uses eight minutes' worth of reads; it is not a seven-minute
physical-device reproduction.
