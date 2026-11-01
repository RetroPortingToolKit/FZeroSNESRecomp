# Two to four player VS

Tracking: `beads-8wg.5.120`; shared online stress gap: `beads-8wg.2.190`.
Branch: `feat/fzero-four-player`, based on `origin/main` at
`794d602c5f450014e77896a151cadffec572745e`.

## Behavior

VS supports two, three or four humans. Local two-player retains top/bottom
views; three/four players use a 2-by-2 grid, with the unused fourth cell black.
Press Y on P1's machine-selection page to select the player count. Each active
seat joins, selects a machine and readies independently. A device has one owner;
at most one seat uses the keyboard. The existing shared league/course cursor is
retained. Duplicate machines remain legal.

Online supports one player per PC, with two to four participants and one full
camera per PC. The participant count is fixed before simulation begins. Mixed
couch/online sessions, mid-race joins and spectators are outside this change.
Rollback remains mandatory. Start opens a live menu; races never pause.
The shared launcher still limits its LAN/Direct IP room UI to two seats; 3/4P
regular online rooms and private direct hub qualification are separate paths.

All humans share one simulation, four NPC bumpers and terrain mutations. Every
human pair has an independent contact latch. Stable seat order determines
movement/contact ordering and equal live progress; equal finish times share
rank. Finished racers precede survivors and DNFs. Old build-specific states and
replays are rejected; native saves are preserved.

Local 3/4P currently uses portable CPU rendering for HD because the shared GPU
job packet has one full-width camera per scanline. Its CPU fallback uses up to
4x when the GPU path is selected. Online single-camera and local 2P GPU rendering
remain available. This is a functional implementation, not a claim of 4P 64x GPU
support.

## Running and building

Desktop: `build/F-Zero Forever.exe --players 4 PATH-TO-PRIVATE-ROM`.
`--players 2` and `--players 3` select the other counts. Machine overrides are
`--p1-machine` through `--p4-machine` (stable IDs 0..11).
Headless: `build/FZeroSNESRecompHeadless.exe ROM FRAMES --versus COURSE P1 P2 [P3 [P4]]`.

Build uses the checked-out engine/UI worktrees and the existing supported
`FZERO_GEN_DIR` override pointing at the private generated stock sources in
`_wt-fzero-track-packs/captures/upstream-073/stock/gen`. Those private generated
sources are reused; they are not committed or distributed. UI pin is unchanged.
All speculative shared input/rollback experiments were rejected and reverted;
the original engine and UI pins are retained.

## Validation record

Raw logs and artifacts are under the ignored `captures/` directory. Prepared,
tested and owner-approved are distinct states.

- Actor oracle: 3/4P readiness/countdown, independent inputs, deterministic
  continuation, all six contact pairs, canonical state round trips and ranking
  pass, alongside native actor parity checks.
- Actual SDL virtual devices: four-seat isolation and disconnect neutrality
  pass; existing two-player controller/menu lifecycle passes.
- Online reducer: 2/3/4 setup, independent readiness, state validation/restore
  pass.
- `captures/state3/results.json` and `captures/state4-final/results.json`: full three/four-player host lifecycle passes,
  including save/load, rewind, fresh-process continuation, in-race viewport
  changes, diagnostic replay/divergence and invalid-state rejection.
- `captures/finish4/results.json`: four real five-lap awards/results, rematch
  countdown, quick slot, save/load and rewind pass using the explicitly enabled
  teleport fixture. Poses/health were forced; laps/outcomes were not.
- `captures/local4.png`: actual four-camera native race capture, visually
  inspected. This is rendering evidence, not natural full-course driving.
- `captures/net-3p-final/report.json`: three real peers, loss/jitter, 1,442
  matching confirmed ticks, zero desyncs.
- `captures/net-4p-final-loss/report.json`: current dependency pins, four real
  peers, loss/jitter, 1,426 matching confirmed ticks, zero desyncs.
- `captures/net-4p-loss-only/report.json`: four real peers, loss/jitter, 1,430
  matching confirmed ticks, zero desyncs.
- `captures/net-4p-forced/report.json`: four real peers, forced corrections
  every 37 ticks, 1,402 matching confirmed ticks, zero desyncs and actual replay.
- `captures/net-4p-loss-forced101/report.json`: loss/jitter plus forced
  corrections every 101 ticks, 1,351 matching confirmed ticks, zero desyncs.
- `captures/net-2p-forced/report.json`: two-player regression control passes
  with 1,438 matching confirmed ticks and actual rollback.
- Denser combined loss/forced-correction runs have failed baseline digest
  checks. These remain qualification failures; passing cases do not erase them.
  This is an open shared-library qualification gap under `beads-8wg.2.190`.
  Input deferral, shorter prediction, common-baseline and history-retention
  experiments were rejected and reverted.
- The pre-existing `fzero_mods` CTest expects three global title-art choices;
  current main has moved visible art into installed packs. This unrelated
  assertion fails. The stale actor-oracle V-glyph assertion was updated to
  inspect the current nine-column glyph, preserving silhouette checks.
- Initial widescreen native bootstrap produces a different captured initial
  race identity (historical `beads-8wg.5.111`). The viewport lifecycle check now
  compares identical native bootstrap followed by in-race view changes.

## Built native cost sample

`captures/native-cost/results.json` records one matched 1,800-frame run per
count on the owner PC, native rendering every tick with muted output, including
boot and course setup. Throughput was 314.4 FPS (2P), 194.8 FPS (3P) and 162.9 FPS
(4P). Peak working sets were 25.1, 27.0 and 28.4 MiB respectively. This is a
bounded headless throughput sample, not a desktop frame-time distribution or a
historical 2P regression comparison. The capacity reservation and binary hash
are retained alongside it.

## Owner acceptance still required

Use a normal window with normal audio and disposable profiles/saves. Test a few
bundled setups: (1) four gamepads and duplicate machine choices, (2) keyboard plus
three gamepads with disconnect/reconnect, (3) a 3P race with window/aspect changes,
and (4) four actual PCs with live Start menus, rematch and change race. Record
Pass/Fail/deferred and notes for each setup; a rendering capture or teleport
fixture does not establish handling or natural course completion.

No merge, public release or delivery-folder replacement is authorized here.
Retain the current build, raw qualification evidence and disposable QA profiles
until handoff is resolved; clean those profiles afterwards without touching owner
saves. The native-title 4P UI fixture also needs follow-up: one attempt exposed the
fixture's incorrect cold-boot return prerequisite, and a later ordinary-title
attempt timed out during setup. Four-device input/count-selection unit checks
and the direct-mode four-camera capture do not substitute for this UI gate.

Physical controllers, WAN/ICE, Linux/cross-platform qualification and
sustained desktop performance are not established by the current tests.
