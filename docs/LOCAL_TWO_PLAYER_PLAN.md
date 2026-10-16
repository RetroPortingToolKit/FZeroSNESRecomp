# F-Zero local two player implementation plan

Implement local two-player versus racing in F-Zero Forever with one shared
simulation and two independent cameras, always displayed top/bottom. The owner
selected versus races without CPU opponents for the first release and approved
the implementation plan. The latest instruction is to prepare this handoff and
leave gameplay implementation to a new session.

Tracking: central Beads `beads-8uc1`, beneath F-Zero epic `beads-8wg.5`.
Prepared 2026-10-15. No multiplayer prototype or new gameplay code exists yet.

## Repository and checkpoint

- Worktree: `F:/Projects/snesrecomp/_wt-fzero-track-packs`.
- Implementation branch: `feat/fzero-local-2p`, created from `f-zero-forever` at
  `c33e2e1d053a344377110f8a97d68bcaa31af02a` (version 0.10.0).
- The game, actual engine build checkout, and actual UI build checkout had no
  tracked pending edits when preparing the branch. Existing commits preserve
  the baseline; an empty checkpoint commit is unnecessary.
- The older `F:/Projects/snesrecomp/FZeroRecomp` checkout is on `main` at
  `1686df4`. Do not use it as the implementation base or merge it blindly.
- Engine build checkout: `F:/Projects/snesrecomp/_wt-fzero-diagnostics-engine`,
  detached at `e54427bad93fb75729ce8214fdd7a46e197e6223`.
- UI build checkout: `F:/Projects/snesrecomp/_wt-recomp-ui-fzero-quick`, at
  `b520c9cdbced26d47f95b9117074c011bc90c550`.
- The game index pins those exact dependency commits. Its local `snesrecomp`
  directory is absent because the build uses the separate engine checkout.
  Do not stage that absence as a submodule deletion. Stage exact files rather
  than using `git add -A`.

Keep implementation game-owned where possible. If a shared dependency must
change, create its own branch from the pinned commit before editing, commit
there, and update the game pin only after validation. Do not commit unrelated
work or modify the old baseline branch.

## Evidence and private references

The available evidence supports feasibility, not a claim of working multiplayer.
The difficult work is separating player-only state, resolving shared collisions,
and generating the second complete view.

| Resource | Location and significance |
| --- | --- |
| Original F-Zero VS source | `E:/Downloads/source release 12.06.09.rar`, 5,010,791 bytes; SHA-256 `05573e4fcbcf8aa2d18bce0e2dae3df408641f26dc7871213fad8aed67986dcd`. Contains modified Snes9x 1.42 and the C# FZVSServer. |
| Emulator modifications | Archive member `source release/snes9x-1.42-src/snes9x-1.42-src/wsnes9x.cpp`; game setup, hook addresses, participant states, racer exchange, and collision modifications were inspected. |
| Server racer model | Archive member `source release/win_server/FZVSServer/FZVSServer/Player.cs`; exchanges position/orientation and race status. |
| Annotated disassembly | Worktree-relative `references/diz-export/export/`, especially `bank_00.asm`, `bank_03.asm`, and `labels.asm`; intake provenance is in `references/diz-export/README.md`, tracked by historical `beads-8wg.5.58`. |
| Renderer groundwork | `docs/ADAPTIVE_RENDERER.md` and `src/fzero_renderer.c`; full-course lookup already resolves locations beyond the guest's streamed tilemap. |
| Presentation precedent | `F:/Projects/segagenesisrecomp/SonicTheHedgehog2Recomp/README.md`; local versus stays split, whereas online peers receive individual views. Sonic 2 already had native two-player simulation; F-Zero does not. |

The original author describes the design at <https://fzerovs.blogspot.com/>.
F-Zero VS advances separate game instances and exchanges racer locations. It
suppresses AI and modifies collisions to avoid competing updates. Use those
findings as reference knowledge; do not import its server, socket code, separate
game instances, or collision compromises into the new local mode.

The DIZ export has 28 recorded diagnostics and uncertain labels. Verify routine
boundaries, M/X widths, branch targets, arithmetic, and writes against the ROM
and runtime. `tools/disassemble_range.py` only performs a linear decode with
caller-supplied M/X flags. It cannot establish correct call boundaries alone.

Seven USA-ROM locations were spot-checked successfully:

| CPU address | Observed bytes | Historical use |
| --- | --- | --- |
| `$00:BDB2`, `$00:BDD5`, `$00:BDEF`, `$00:BDFE` | `99 20 0B` | Stores to the other racer's rebound speed. |
| `$00:DDDA` | `20 57 DE` | Calls opponent AI. |
| `$00:DDFC` | `20 D0 DE` | Calls opponent population/catch-up behavior. |
| `$00:D2EF` | `A5 52 0A` | Reads player machine selection. |

These are verified landmarks, not instructions to apply the historical NOPs.
Relevant annotated landmarks include the physics hub `$00:9144`, actor loop
`$00:915D`, finish-line routine `$00:9968`, and player input/drift near
`$00:B8CE..B928`. Racer X/Y coordinates are at `$0B70/$0B90` with two-byte
slot strides; fractional coordinates, momentum, facing, altitude, and action
state must also be retained. Player-only health at `$C9`, selection at `$52`
and `$D1`, input history, boosts, recovery, and checkpoint state must become
per-player state rather than being shared accidentally.

Keep the archive, any bundled ROM/binaries, ROM-derived generated output, and
the entire private DIZ directory out of commits and release bundles. Use the
existing ROM verification and local generated sources.

## Finished behavior

- Add a discoverable **Local 2 Player** entry. Preserve ordinary single-player
  selection and existing content settings outside versus sessions.
- First release supports two humans, all 15 stock courses, the four stock
  machines, and five laps. No CPU field, multi-race cups, netplay, or qualification
  of BS/CGP/imported courses and altered gameplay profiles is included.
- Both players assign devices, independently choose machines, and confirm ready.
  Player 1 chooses the course. Duplicate machine choices are allowed with
  distinct player colors/labels. Use distinct legal starting-grid positions.
- Support two gamepads and keyboard plus gamepad, with separate bindings and
  stable SDL device identity. One device must not drive both racers accidentally.
- Start from one shared countdown. Keep the original simulation cadence
  `FZERO_SIMULATION_HZ`, currently approximately 60.0988 Hz.
- Player 1 is always the top pane and player 2 the bottom. Each pane has its own
  camera, machine effects, lap/time/speed/power/boost HUD, and player identification.
- Derive each pane's projection from its drawable rectangle using the existing
  supported aspect range. Fit may clamp at 32:9 and center the image in an
  exceptionally wide pane. Preserve field of view and pixel aspect; do not crop
  or vertically squeeze a completed single-player image to create the split.
- Either player pauses the entire session. Focus loss or an assigned-controller
  disconnect also freezes the session, clears held inputs, and presents recovery
  controls. Explicit resume is required after reassignment.
- Track checkpoint order, laps, health, finish, and DNF independently. Remove
  rank-out/lives behavior from versus. Backward finish crossings do not award laps.
  Race timing uses the common tick; finishes on the same tick tie.
- A finish or DNF does not stop the other racer. Hold the finished player's view
  at the finish and show status; retain the crashed player's terminal view.
  Display combined results when both are terminal. The pause menu can end a
  stalled race. Rematch retains course/machines; changing either returns to setup.
- Play one soundtrack and mix effects from both racers without duplicate music.
  Audio emulation remains active in validation when needed, with output muted.
- Save/load, quick saves, rewind, replay, and crash capture include the complete
  session. Versus saves and records use their own namespace and never populate
  single-player records.

## Architecture and interfaces

### Shared simulation

Add a game-owned versus subsystem with an opaque session, two actor states, a
shared clock, race phase, course identity, deterministic state, and results.
Use portable fixed-width integer arithmetic matching verified guest semantics.
Only elective presentation optimizations may vary by host.

Expose initialization/reset from race configuration, one tick accepting two
latched SNES-format input words, immutable completed snapshots, and versioned
save/restore. The frame loop latches both inputs before advancing either racer.
Keep session and presentation interfaces separate so neither camera determines
simulation ownership. This is architecture preparation only; implement no online
transport, lobbies, capability negotiation, or network protocol.

Derive portable C adapters for input, vehicle physics, surface interaction,
health, boosts, airborne/recovery behavior, checkpoint progression, and finishing
from verified routines and ROM tables. Integrate at controlled runtime hooks;
do not edit generated C. Keep ordinary guest execution for asset/race setup and
existing single-player behavior. Own versus gameplay state explicitly and make
any required guest mirrors one-way projections at documented boundaries.

First map every player-only read/write and its dependencies, including scratch
RAM, RNG, hardware multiply/divide, sound effects, counters, and transitions.
Do not run the entire guest frame twice or repeatedly copy an unverified
player-zero RAM range as a substitute for actor separation. Reuse scalar guest
math and tables through verified adapters, without inheriting player-zero-only
or camera-dependent conditions.

For each tick, compute the two actors' uncoupled movement from the same initial
world snapshot, then resolve their contact once in stable actor-ID order and
apply both responses. Use stock collision geometry and responses as the reference,
explicitly checking altitude and world-coordinate wrapping. Verify the update
order against stock observations before recording parity. Neither sprite
allocation nor camera visibility may disable collisions or other physics.
Advance shared events, race clock, and transition state once per tick.

Stock versus uses stock physics tables, without silently applying saved CGP,
Legend, or other gameplay modifications. Unsupported content is unavailable in
the versus setup with a clear qualification message. Leaving versus restores
the user's normal settings. Identify the versus rules/version in state identity.

### Rendering

Replace renderer-owned singleton frame/interpolation/scratch state with explicit
view contexts while retaining the existing single-player interface as a wrapper.
Share immutable course/assets; keep camera transforms, previous/current frames,
projection output, effect identity, HUD state, and worker scratch per view.

Generate both cameras' transforms, skyline, machine sprites, effects, and HUDs
from one completed race snapshot. The current renderer consumes guest-latched
matrices and OAM: allocating another output buffer alone cannot produce camera
2. Implement camera/display adapters from verified render routines and ROM tables.
Rendering must not call simulation updates or mutate the live guest state.

Reuse the full-course WRAM lookup for both cameras, including widely separated
racers. Extend vehicle identity handling to stable versus actor IDs. Suppress
interpolation across reset, load, teleport/recovery discontinuity, and course
changes. Apply existing HD/FPS options consistently to both panes without
changing simulation frequency. Keep shared worker resources safe for two views.

### Host and persistence

Integrate the session in desktop and headless hosts. Extend input scripts/replay
to carry both players and add headless versus selection and race configuration.
Retain existing single-player command behavior. Keep the session entry point
discoverable through the existing launcher/game menu instead of requiring an
environment variable for normal users.

Extend the existing snapshot extras, pre-load compatibility checks, rewind,
replay logs, and crash captures with both actor states, shared events/RNG/tick,
configuration, and results. Use a dedicated versus state identity and namespace.
Reject incompatible or corrupt states before applying any live state. Loading
restores the whole session and resets view histories. Preserve existing stock,
Deluxe, MSU, and track-pack snapshot compatibility behavior.

Primary integration areas are `src/fzero_runtime.c`, `src/fzero_renderer.c`,
`src/sdl_main.c`, and the existing input, video/settings, headless, state, and
capture modules. Add bounded new versus modules rather than embedding all
gameplay in the host loop. Start with game-owned changes; shared engine/UI
changes require their own reviewable commits.

## Implementation order and acceptance gates

1. **Baseline and state inventory.** Read the issue and this plan; recheck Git
   state/dependencies. Run the existing relevant tests once and record failures
   already present. Inventory verified actor state and routine read/write sets,
   identify exact hook boundaries, and capture stock physics fixtures. Record
   implementation findings in the issue without committing private traces.
2. **Actor adapters and parity.** Implement input/physics/progress adapters with
   two explicit actor states. Compare one active actor with stock execution for
   every machine, acceleration/braking, turning, L/R drift, launch/false start,
   boosts, walls, jumps, pits, mines, death and recovery. Check velocity,
   fractional position, angles, health/boosts, and checkpoints each tick. Include
   varied initial states and long input sequences, not just steady straight runs.
3. **First playable Mute City.** Integrate both controllers, shared clock,
   independently simulated racers, one collision result, two complete cameras,
   and both HUDs. Gate success on independent steering/braking, mutual visibility,
   collisions with consistent response in both views, and one racer driving far
   away without corrupting the other's world or simulation.
4. **Complete versus flow.** Finish ready/countdown, stock course/machine setup,
   laps, finish/DNF/tie handling, pause/disconnect, combined results/rematch, and
   soundtrack/effects. Validate all stock courses and duplicate machine choices.
5. **Session lifecycle.** Integrate save/load/quick saves, rewind, two-player
   input replay, state identity, isolated records, and crash capture. Test
   restoring into countdown, racing, contact, one-finished, and results phases;
   corrupt or incompatible loads must leave the live session intact.
6. **Qualification and delivery.** Run affected existing tests plus meaningful
   versus integration scenarios; build Windows and Linux portable paths; measure
   two-view baseline performance; finish a real two-controller playtest. Record
   actual results and commits in Beads. Close only when every required gate passes.

Collision scenarios cover rear/side/head-on hits, overlapping starts, opposing
motion, airborne separation, simultaneous wall contact, and coordinate wraps.
Race scenarios cover reverse crossings, skipped checkpoints, lap differences,
simultaneous finishes, double DNF, one racer finishing/crashing while the other
continues, and many consecutive rematches.

Verify input replay produces identical simulation hashes across render rates,
window/pane sizes, HD on/off, and worker configurations. Save/load and rewind
must reproduce both players' continuation. Compare fresh-process restoration
as well as same-process loads. For rendering, inspect both HUDs, shadows,
sparks, explosions, pits, skyline, fades, and distant-track sampling at supported
aspects. Rendering-only changes must not affect gameplay hashes.

Baseline graphics must sustain the original simulation cadence on the validation
host with two views. Measure HD modes separately; do not skip simulation ticks
to meet presentation FPS. Compare supported platform builds and representative
runtime replay results; compilation alone does not establish numerical parity.

## Build and tool starting points

Use PowerShell and absolute executables to avoid MSYS path reinterpretation.
The existing `build/CMakeCache.txt` records these private local prerequisites:

| Setting | Existing value |
| --- | --- |
| `SNESRECOMP_ROOT` | `F:/Projects/snesrecomp/_wt-fzero-diagnostics-engine` |
| `RECOMP_UI_ROOT` | `F:/Projects/snesrecomp/_wt-recomp-ui-fzero-quick` |
| `FZERO_GEN_DIR` | `F:/Projects/snesrecomp/_wt-fzero-track-packs/captures/upstream-073/stock/gen` |
| `FZERO_DELUXE_GEN_DIR` | `F:/Projects/snesrecomp/_wt-fzero-track-packs/captures/upstream-073/deluxe/gen` |
| `FZERO_DELUXE_DATA_FILE` | `F:/Projects/snesrecomp/FZeroRecomp/captures/bs-deluxe/mods/bs-deluxe.dat` |
| `FZERO_PACK_SOURCE` | `F:/Projects/snesrecomp/_wt-fzero-track-packs/captures/reconstructed-projects-03/packs` |

Verify those files still exist. Reuse the configured build for the baseline;
do not point it at another branch's generated sources. The cache's CMake is
`C:/Program Files/CMake/bin/cmake.exe`; MinGW C compiler and Ninja are under
`C:/msys64/mingw64/bin`. Both desktop and headless game targets are configured.

From the game worktree:

```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' --build build --parallel 4
& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build --output-on-failure
```

Use the existing `tests/validate_course_driving.py`, headless input scripts,
renderer capture tests, and state/replay validation as patterns. Read each
script's arguments before running. `tools/run_capture.py` clears several
environment prefixes and only explicitly selects dummy audio in desktop mode;
ensure new or reused launch paths honor muting. Never manufacture physics,
collision, or lap results by writing the expected outcome into guest RAM.

`tools/build-linux.sh` is a starting point for Linux, but currently expects
local submodule directories and default generated-source paths. Supply the
correct pinned checkouts/resources in the Linux workspace; do not assume this
Windows worktree can run the script unchanged. Use WSL or the established Linux
build environment and retain the portable implementation.

7-Zip is `C:/Program Files/7-Zip/7z.exe`. Archive members can be inspected with
`x -so` without extraction. Git is `C:/Program Files/Git/cmd/git.exe`. Do not
launch Ghidra's GUI; if further analysis needs Ghidra, use the headless MCP
registry. Initial discovery found no registered SNES F-Zero program.

## Operating constraints and next session

Work solo. The owner has not authorized subagents for this task. Mute every
agent-initiated game/test launch; keep audio emulation when required and select
dummy audio or zero output volume. Do not unmute without an explicit request.
Keep Windows filesystem operations in PowerShell/.NET and do not use `cmd /c`
fallbacks. Preserve portable fallbacks for optional CPU/OS-specific optimization;
no assembly is required by this plan.

Use only the central Beads workspace `F:/Software/beads/issues`. The issue is
`beads-8uc1`; do not create a duplicate. Record milestones, changed assumptions,
failing gates, and landed commits there. Sync durable updates with
`C:/Users/Matthew/.local/bin/Sync-CentralBeads.ps1 -Action push`, yielding and
resuming the same healthy process until it finishes. Do not impose short kill
timeouts or restart the shared database server.

The next session should implement the whole approved feature and acceptance
gates, rather than stopping at scaffolding or the first playable milestone.
Known routine mapping and physics parity work are part of implementation, not
a reason to restart this feasibility investigation. Report any evidence that
invalidates the chosen architecture candidly and resolve it before claiming
completion. A true external prerequisite must remain explicit; never mark an
unperformed human playtest or platform validation as passed.

No publication, merge, release, or outbound message to another person is
authorized by this handoff. Local reviewable commits are authorized.

## Implementation record (2026-10-15)

The feature remains **incomplete**. The following
are implementation milestones, not completion of gates 1, 2, or 3. In
particular, a second renderer context is not a second camera, and scalar
routine parity is not full-frame or race parity.

Commit `b784ad8` replaces the renderer's singleton capture/interpolation/worker
scratch with opaque `FzeroRenderView` objects. The existing interface wraps a
single-player view. Alternating native/HD rendering of two different immutable
views, resetting/destroying one view, and preserving the other view are tested.
Capture file layout and the single-player capture interface remain unchanged.

`src/fzero_actor.{c,h}` implements portable human-actor scalar adapters. The
state is named fixed-width fields, not a RAM-window swap. Scratch belongs to
one uncoupled evaluation. ROM tables are read through a caller-supplied byte
reader; no ROM or generated source is embedded in these files. Normal gameplay
still executes the stock guest. Only the opt-in headless observation test calls
the adapters, computes an expected actor without guest writes, and compares it
with the real guest at `$00:919B` before finish-line handling.

| Adapter group | Verified stock reference entries |
| --- | --- |
| Countdown acceleration, launch, failed-start control | `$00:FA1C`, `$00:8D12`, `$00:B5EC` |
| Input/boost action, fractional steering stage, turn/drift performance | `$00:B7CB`, `$00:B94E`, `$00:B881` |
| Slide, top speed/gravity, grip, spin state, acceleration/braking | `$00:9377`, `$00:91BC`, `$00:9264`, `$00:930D`, `$00:938D` |
| Spin, facing, grip application, velocity, wind, limits, position | `$00:953A`, `$00:9503`, `$00:9589`, `$00:95E7`, `$00:9672`, `$00:96F2`, `$00:9753` |
| Dash, altitude/jump/landing, surface damage, mine response | `$00:981C`, `$00:9B4F`, `$00:98A4`, `$00:9D9E` |
| Surface classification and whole-course sampling | `$00:8E36` / `$03:9187`, `$00:A1E5` |
| Spin trigger and animation state affecting subsequent steering | `$00:BA13`, `$00:BA70` |
| Recovery movement and pit/repair progression | `$00:B6CC`, `$00:B61B` |

Actor ownership for these routines includes position/fractions/velocity,
facing/momentum/turn/spin, altitude/vertical velocity, action/state/surface
flags, boundary momentum/timer, no-control timer, health/cooldowns, boost
count/timer/award, held/pressed/released input, fractional steering stage and
acceleration, machine table indices, exhaust/hover and animation history,
death/lap mirrors, recovery target/phase, and sound/visual event flags. `$DD`
is essential simulation state: the next steering update reads it, although the
stock animation routine writes it. Recovery movement also retains arithmetic
carry that makes its positive approach step seven units rather than six.

The differential test audits exercised RAM reads/writes against those fields,
`$00..$2F` scratch, `$0100..$01FF` stack, and documented shared/presentation
accesses. Shared fixture inputs are tick `$51`, doubled machine index `$D1`,
rank eligibility `$DC8`, and disabled attract flags `$F50/$F51`. Presentation
accesses include OAM/HUD buffers, health edge `$F0/$E23`, and the mine routine's
screen-Y guard. M16 reads of `$B11/$D52` are masked high bytes. The CPU speed
writes in the stock launch routine are discarded. Mine coordinates become an
explicit event; committing events to the shared course is still outstanding.
No currently adapted routine uses RNG. This inventory does not yet cover the
remaining collision, checkpoint, finish, camera, or host integration routines.

The adapters assume four stock machines, an unmodified verified USA cartridge,
human rank eligibility, and no attract-demo actor. The mine adapter intentionally
has no opponent sprite-visibility/Y guard. Its response is compared against the
stock human branch with an eligible sprite; world-event deduplication and
collision qualification remain to be implemented.

`fzero_actor_oracle` executes original routines on the pinned engine's separate
callback-bus 65816 core and compares actor outputs. It validates ROM SHA-256
before execution. Multiply/divide results are modeled by the fixture bus;
beam/APU timing is outside this scalar oracle. It cannot play audio or mutate
the running game. The test performs 40,000 varied fixtures per scalar entry,
40,000 independent whole-course lookups, and 48,000 chained routine ticks across
the four machines. Chained ticks supply terrain as input and omit still-unported
guest work; they are not a whole-frame parity claim. Private input/output
artifacts remain ignored under `captures/` or `build*/`.

Enable the private-ROM CTest explicitly:

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' -S . -B build '-DFZERO_TEST_ROM=<private verified USA ROM>'
& 'C:/Program Files/CMake/bin/cmake.exe' --build build --target fzero_actor_oracle
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build -R fzero_actor_stock_parity --output-on-failure
```

The ROM path is a local cache value only. `tools/disassemble_range.py` now
accepts `--engine-root` so inspection works with the separately pinned checkout
without creating the absent submodule directory. Its linear-decoding caveats
still apply, especially after calls, PLP, and divergent REP/SEP paths.

Validation of the current implementation:

- Full desktop and headless builds passed on Windows/MinGW and Linux/WSL with
  the pinned engine/UI checkouts. Linux needed `libgif-dev`, `rapidjson-dev`,
  and `libarchive-dev` installed; source dependency pins did not change.
- CTest passed 16/16 on each platform, including the private-ROM oracle.
- The oracle passed 1,080,000 scalar fixtures, 40,000 world lookups, and 48,000
  chained routine ticks on both platforms. Continuation hash:
  `636769d59ff80807`. The final Linux oracle also passed with undefined-behavior
  sanitization enabled. This hash belongs to these fixtures, not session
  compatibility identity.
- `tests/validate_actor_live.py` drove stock selection and 3,000 frames per
  machine using ordinary input. All machine IDs were checked in actual WRAM;
  the stock menu advances Down through `0,2,1,3`, after its initial input lock.
  Comparisons were Blue Falcon 969, Wild Goose 1,102, Golden Fox 1,019, and
  Fire Stingray 1,008: 4,098 per platform, zero mismatches. Each machine's final
  128 KiB WRAM dump matched byte-for-byte between Windows and Linux. These
  are stock movement observations, not versus-session determinism tests.
- A separate muted 1,800-frame single-player route passed with active audio
  emulation and zero underruns. Every automated launch used dummy audio;
  headless/oracle programs have no speaker output.

The live oracle is headless-only and enabled by `FZERO_TEST_ACTOR_PARITY`.
Use its dedicated driver; reset/load/rewind observation is not qualified by
this test. The driver isolates settings/saves/output under `--out`, disables
content/physics modifications, and does not write approach states or expected
outcomes into guest RAM. `src/fzero_actor_fields.inc` is the explicit field
manifest shared by the scalar and live observation tests. For example:

```powershell
& 'C:/msys64/mingw64/bin/python3.13.exe' tests/validate_actor_live.py --build build --stock '<private verified USA ROM>' --out captures/local-2p-validation/live-windows
```

### Shared simulation implementation checkpoint (2026-10-15)

The following now exists in portable game-owned C, below the host integration
boundary. It does not yet make Local 2 Player available in either host.

- `fzero_actor_progress.c` ports human checkpoint lookup, ordered forward and
  backward gates, previous/next gate pairs, authored branches, wrong-way
  hysteresis and wall rebound (`$E309`, `$DE07`, `$E5CF`, `$E67B`, `$E7A8`,
  `$E85E`, and the human prefix `$DD25..$DDA3`). It also ports finish-strip
  detection without stock rank/lives/record writes (`$9968..$99BF`) and authored
  shortcut-recovery triggers (`$D9C8..$DAB7`). The actor owns takeoff/landing
  history and the full branch word, including the high byte cleared by recovery.
- `fzero_actor_contact.c` ports the exact toroidal world/body projection
  (`$DBC4..$DD1F`), one speed/momentum exchange (`$BD51`), direct spin (`$BE02`)
  and lateral rebound (`$BC4A`). Damage adapters retain `$87A3`'s low-byte
  momentum reads and carry set by `SEP #$21`. Both human participants receive
  independent damage and their selected machine's player response table.
  Visibility, OAM slots, cameras and viewport settings never gate contact.
- `FzeroActorTick` evaluates one human against an immutable whole-course image.
  Both humans latch inputs before either runs. A shared session resolves the
  pair once afterwards, then commits mine events in stable actor-ID order.
  `FzeroActorCommitMine` implements the four crater-pointer writes through
  `$A23D/$A3F5..$A443`, so simultaneous evaluations see the same initial mine.
- `fzero_versus.c/h` owns an opaque session, two actors, one clock, ready mask,
  shared 180-tick countdown, pause reasons, race/results phases, five-lap
  progress, boosts, per-lap/finish ticks, DNF, ties and rematch. Finished/crashed
  actors freeze while the other actor continues. Backwards checkpoint travel
  repays gate credit; the finish strip alone cannot farm laps or boosts.
  Final-gate confirmation retains the actual finish-crossing tick and pose
  when the before-movement checkpoint stage confirms on the following tick.
- The versioned session image is canonical little-endian field data rather
  than a C-struct dump. It includes both actors, spawn seeds, current and initial
  course images, contact, mine events, phase/clock/configuration, lap times and
  pending crossings. An FNV-64 integrity check detects accidental corruption;
  rules/version and semantic/configuration validation run on a temporary
  session before applying it. Rematch restores the initial terrain, including
  mines. There is no session RNG dependency in the implemented adapters.

This is simulation profile/rules 1: stock human physics, movement followed by
one stable-ID two-human contact. Stock resolves player/CPU contact before
movement; component parity does not assert whole-frame update-order parity.
The second human deliberately uses player spin tables where stock's other
slot uses CPU tables. Both adaptations require versus integration qualification.

Expanded private-ROM oracle results on Windows and Linux/UBSan:

- 28 scalar entries x 40,000 = 1,120,000 comparisons, including shortcut rescue.
- 40,000 whole-course surface samples; 48,000 chained actor ticks. Current
  continuation hash: `450ebd35dddd6e0d` (the named-field schema expanded).
- Four checkpoint entries x 40,000 = 160,000 comparisons, with RAM ownership
  checks, branch changes, exact gate/axis points and boundary response.
- 80,000 physical projections; five contact entries x 40,000 = 200,000
  comparisons, with RAM ownership checks. Shared-pair scenarios cover all
  16 machine pairs, direct/lateral response, contact latching, toroidal seams
  and airborne exclusion.
- 40,000 finish-strip comparisons, stopping before native lives/record effects;
  10,000 whole-bank mine-mutation comparisons.
- Synthetic-course session tests cover independent controls, ready/countdown,
  pause/frozen clock/cleared input, canonical round trips, corrupt/incompatible
  rejection without live mutation, deterministic continuation, contact/mine
  restores, simultaneous mine evaluation, DNF from actual surface damage with
  the other racer continuing, end/rematch and initial-terrain restoration.
  Ordinary integer-generated pad inputs also complete two five-lap synthetic
  races, checking same-tick ties, unequal finish times, frozen terminal pose,
  one-finished saves and capped lap boosts. These are not stock-course races.
  The session fixture hash is `09e2a5a898890af4`.
- The live driver now checks progress as well as movement on both platforms. Over its
  3,000-frame ordinary-input routes, checkpoint comparisons are Blue Falcon
  970, Wild Goose 1,103, Golden Fox 1,020 and Fire Stingray 1,009: 4,102, zero
  mismatches per platform. Movement remains 4,098 comparisons, zero mismatches
  per platform. Each machine's final 128 KiB WRAM remains byte-identical
  between Windows and Linux. The driver
  uses the existing target exclusion `SNESRECOMP_LLE_INTERP_TARGET=00dd20`
  to observe the real opcodes inside the otherwise compiled checkpoint call.
  It never writes guest state. The refreshed Windows desktop/headless build
  and CTest 16/16 passed (366.93 seconds, including 323.37 seconds of catalog
  IO). The Linux full host build is still progressing through asset staging;
  do not infer a full-build/CTest result from the standalone Linux oracle.

Shared-simulation commit: `5ffedab`. A follow-up fixes terminal status immediately
after shared contact, before finish awards: a repeated-collision route using
ordinary throttle/brake/steering reaches DNF naturally. The new check fails at
the old commit's still-active status on that damage tick and passes with the
fix on Windows and Linux/UBSan.

### Independent camera display adapters (2026-10-15)

`fzero_camera.c/h` now derives the race perspective coefficients, periodic
center/scroll representatives, background scroll and skyline scroll separately
from each completed actor pose. It reads immutable ROM tables and has no guest
pointers or simulation writes. The adapter preserves the stock integer divide,
table quantization and 47/112/64-line HDMA boundaries. It explicitly normalizes
inverse facing rather than inheriting a caller's CPU carry; the reference for
`$980E` is invoked with that explicit carry contract. This qualifies the display
adapters, not a complete native call chain or composed second view.

Windows and Linux/UBSan pass 40,000 camera poses against `$97BC`, `$980E`,
`$A60C`, `$97E3` and `$03:94CF`, including all 176 perspective rows and their
signed bank selections for every fixture. These results do not establish
sprite/HUD generation, complete split-view pixels or presentation performance.

Outstanding implementation: connect the session to stock guest asset/race
setup and both hosts; generate true independent cameras, complete sprites,
effects, skyline and HUDs; implement discoverable setup/device binding and
pause/disconnect/reassignment controls; connect the session image to snapshot
extras, guards, save namespaces, quick saves, rewind, two-player replay and
crash capture. The core APIs alone do not fulfill those host/lifecycle gates.
All fifteen stock-course versus races, human two-controller play, split-view
visual/performance qualification and full host session determinism across
platforms/presentation rates remain unperformed. Keep `beads-8uc1` in progress.

## Windows host integration milestone (2026-10-15)

The in-progress desktop and headless hosts now enter the shared versus core
after native stock asset setup. This is a prototype milestone, not completion
of the approved acceptance gates. The owner directed that further builds and
validation focus on Windows for now; new Linux qualification is deferred.

`fzero_local` supplies ordinary native menu inputs, selects the race config at
the `$9F08` course-loader entry, then parks the gameplay CPU after assets and
legal grid positions are ready. Native setup uses the interpreter so compiled
calls cannot bypass its configuration hook. The authoritative core advances
once per host tick, while the single SPC timeline remains active. A premature
frame-boundary scene jump failed native setup and was replaced by this path.

Each pane now derives its own perspective and skyline from the completed actor
pose. The renderer always resolves versus terrain through the shared full-course
tables, including inside the primary guest camera's streamed square and at
stock width. Guest OBJ/HUD are excluded. `fzero_ship` selects and decodes native
ship art by relative facing and distance; the two views carry stable P1/P2
labels, separate lap/time/speed/power/boost HUDs, and initial host effects.
No cartridge assets or private captures are checked in.

The launcher has a Local 2 Player game-mode entry and the desktop has a setup
screen with separate device joining, machine choices, course selection and
ready confirmation. One keyboard or SDL instance can occupy only one seat.
Each gamepad has an independent profile: `[Local2P.P1]` / `[Local2P.P2] in
`config.ini` can override `Controls` and `Deadzone`, inheriting common device
bindings otherwise. Either Start pauses; focus loss or a disconnect pauses
and clears held inputs. A replacement device must join explicitly, followed
by an explicit resume. Results expose rematch and changing the setup.

Private Windows validation so far:

- Headless Mute City I, machines0/1,1800 frames: session tick187/race tick7;
  both actors active; zero audio underruns. A2300-frame ordinary packed-input
  route produced independently steered positions4683,286 and3835,444;
  simulation hash2065fa93fcecb231. These are observations, not injected outcomes.
- Desktop2300-frame muted scripted run reached versus; the actual native/HD2
  split-frame bitmap was visually inspected. This is not a physical-device
  playtest or a performance qualification (1365 presentations,942 missed,
  target60Hz across setup and racing on the software backend).
- Scalar/whole-session oracle remains passing on Windows. Display additions
  compare all65536 speed values and16000 ship asset-selection poses against
  native routines. Renderer tests verify distant cameras, stock/wide widths,
  native/HD composition and immutable shared course ownership.
- Windows virtual-controller fixture passes distinct seats, independent
  bindings, ready confirmation, duplicate machines and keyboard replacement.
  Existing video, renderer and gamepad CTests pass. The Mods test with an empty
  private catalog reached an existing title-choice-count assertion; the full
  catalog fixture must be used for its final result. A new controller-test
  target initially lacked the shared overlay font object; its CMake link was
  corrected, and the regenerated CMake target builds and passes on Windows.

Remaining: complete shared sound/effect presentation, whole-session host
snapshots/guards/namespaces/quick saves/rewind/replay/crash captures, render-rate
and window-size determinism, all-course race qualification, normal two-human
race/rematch/disconnect playtests, and baseline/HD performance qualification.
Versus host persistence is not implemented at this checkpoint; native guest
snapshots alone cannot restore the shared race. Keep the issue open.

For development iterations, `FZERO_STAGE_RELEASE_CONTENT=OFF` skips optional
pack/music post-link staging; the default remains ON for ordinary/release
builds. Windows CMake uses native Git explicitly, avoiding the cached MSYS
Git shim. Neither change alters dependency pins or source identity checks.

## Windows session lifecycle milestone (2026-10-15)

Host integration was committed as `d69a170`. Whole-session host persistence
now wraps the native snapshot in the pinned engine's existing checksummed
envelope, identity `fzero-local-2p-v1`, and validates the canonical session
before any guest writes. Native bootstrap state, both humans, countdown,
progress/results, current/initial terrain, and execution/audio clocks travel
together through disk slots, quick saves, the rewind ring and crash captures.
The fixed legacy single-player trailer and its snapshot callbacks are retained.

Versus selects `<save root>/local-2p` before reading battery RAM, with slot
prefix `fzero-local-2p`; quick saves use that root too. The host prefix retains
setup frame/configuration before the authoritative handoff. A fresh load may
adopt a different stock course/machine configuration from its complete image.
Device handles remain external; live loads clear held host controls, preserve
device ownership, and require explicit assignment/resume if a seat is missing.
Reset reopens local setup. Rebuilding captured native assets after load does
not run another guest instruction or require an existing output framebuffer.

The native handoff now extracts authored course data rather than copying all
guest camera/HUD/scratch memory into the authoritative hash. Extraction follows
all 512 grid cells through block rows and tile groups, retains crater groups
and checkpoint/branch/recovery metadata, and zeros unrelated memory. The oracle
checks all 524,288 surface cells, extraction idempotence and scratch exclusion.
The core still receives a complete immutable shared map at its existing API.

Crash capture identifies mode, player count and versus rules; each versus row
contains both packed inputs and the shared race hash. External focus/device/
resume/rematch transitions start new complete checkpoints. Diagnostic replay
ignores live device/focus transitions; normal play still freezes on focus loss.
The replay support tool selects versus explicitly from recorded metadata and
defaults to dummy audio. Legacy nine-column single-player input rows remain
supported. `tests/validate_local_versus.py` is the private-ROM lifecycle driver.

Windows validation:

- Desktop/headless builds pass with unchanged pins. CTest 17/17 passes
  (185.55 seconds), including the expanded oracle and real shared-core virtual
  controller fixture. The latter covers separate bindings, ready/duplicate
  machines/replacement, manual and focus pause/resume, end/rematch/change,
  load/reset setup and a disconnected seat that cannot resume accidentally.
- All 15 courses pass ordinary native setup and handoff with distinct legal
  spawns; machine combinations include duplicate choices. These 1,800-frame
  routes are setup checks, not completed five-lap stock races.
- Actual disk loads and ring rewind preserve RAM, master clock and race hash
  across bootstrap (frame 1,000), countdown (1,650) and racing (2,000).
  Fresh processes continue all three checkpoints to the same tick 687 / race
  tick 507 and hash `da14e581ab108d4d`, including when startup options selected
  another course/machine pair. Fit/4:3/32:9/16:9 and large window changes reach
  the same shared hash. No physics outcomes were injected.
- Headless crash replay reaches that same hash. Deliberately changing its
  recorded race hash triggers divergence. Actual in-process memory loads of
  corrupt identity/guest/checksum, truncated data, and an invalid course in a
  correctly resealed envelope are rejected with the entire live snapshot
  byte-identical. Cross-mode file loads are refused; a single-player battery
  sentinel remains unchanged by the versus runs.
- Desktop replay of 300 racing frames passes every recorded check, presents
  300 frames with zero missed at 60.099 Hz on the native software route, and
  the actual restored split bitmap was inspected. This bounded baseline run
  does not qualify HD performance or a long two-human race.
- A fresh-process load initially dereferenced the absent output framebuffer
  during asset reconstruction; presentation is now suppressed during that
  reconstruction. Desktop replay initially consumed a live focus-loss event;
  ignoring live local control transitions during replay fixes the divergence.
  One intermediate Windows link failed because the validation process still
  held the executable open; the completed sweep was allowed to exit before
  rebuilding successfully.
- A 2,300-frame single-player route matches the prior runtime/headless entry
  point byte-for-byte in all 128 KiB WRAM, clock (821895944), video/activity
  metrics and audio samples, with zero underruns. Strict stock save/load and
  rewind resimulation at frame 1,500 still differ by 12 master clocks while RAM
  matches. Isolated `d69a170` runtime/headless objects reproduce the same
  539575220 vs 539575232 mismatch, so this is a pre-existing validation failure,
  not a passing single-player clock-fidelity result.
- The previously running Linux host build finished, but its Mods CTest timed
  out after 1,500 seconds (15/16 passed). No further Linux qualification is
  planned under the owner's Windows-only instruction.

Remaining implementation/qualification includes shared soundtrack/effects,
additional machine effects, complete stock races and race-state lifecycle
scenarios, renderer-rate/HD/worker determinism and sustained performance,
and a physical two-human/controller playtest. Keep `beads-8uc1` in progress.

## Sound and full-race qualification in progress (2026-10-15)

Persistence was committed as `145d482`. The shared host now encodes the native
`$F8DD` engine and `$F999/$F975` effect commands for both completed actors and
feeds the one original SPC program. The mix is symmetric in player IDs and
independent of cameras. The stock program has one engine-pitch command and
encoded effect groups: choose the faster engine and the higher-priority event
within each group, preserving independent flags. This does not claim two
independent engine voices. Music, landing and phase latches occupy the three
reserved host-prefix bytes; version 2 saves retain them, and version 1 saves
with empty reserved bytes migrate on load.

Windows sound oracle: 40,000 native encoder fixtures pass, including immutable
actor inputs and symmetric mixing. The lifecycle suite with active SPC writes
passes at `captures/l2p-sound-01`; race hashes are unchanged. Actual host
disk/rewind comparisons now include all 64 KiB SPC RAM as well as guest RAM,
master clock and shared race hash.

After correcting countdown rev pitch to use each actor's native countdown
acceleration, both Windows hosts build, the complete actor/sound oracle passes
again, and `captures/l2p-sound-02` passes disk/rewind/fresh-process/viewport/
replay/corrupt-state/cross-mode lifecycle checks. The preview copy is unchanged.

`tests/validate_local_races.py` drives the real headless host using only ordinary
pad inputs selected by observing completed actors and authored checkpoints.
It never edits actors, course data, progress or expected outcomes. Mute City I
machines 0/1 both finish five laps: tick 12610, race tick 12430, shared hash
`0d1e61a0dbe4b3d3`. Countdown/racing/one-finished/results disk and actual ring
rewind checks and a quick-slot load from another save root pass. The same final
hash results when presenting every eighth tick instead of every tick.

The first full 15-course sweep (`captures/l2p-races-01`) has **10 passing cases**.
Cases 6, 9, 10, 12 and 14 fail driver qualification with DNFs (one finisher on
case 10); investigate bot steering/jump speed and simulation before counting
any of those as a completed-course pass. Cases 3, 4 and 11 also exercise actual
shared contacts through host persistence. No physical human/controller playtest
or full-course acceptance is claimed from these automated routes.

The owner now requires all new versus UI to use or emulate actual F-Zero UI.
The generic setup/HUD/loading/pause/results preview does not satisfy that gate.
Native ROM font tiles (`$0E:E800`, 3bpp), menu palettes (`$0E:E100` onward),
cursor/border tiles and menu machine graphics have been located for a runtime
decoder; no ROM-derived art is to be committed. Versus is currently exposed by
the launcher's Game Modes option and `--local-2p`, rather than a new native-ROM
title-menu entry. A separate muted Windows preview lives under ignored
`build/playtest-local-2p`; keep it stable while the owner playtests. The native
UI restyle, failed driver cases, broader HD/rate/worker/performance gates and
physical two-human qualification remain outstanding. Windows only per owner.

## Native versus UI milestone (2026-10-15)

`fzero_native_ui.c` decodes the original cartridge's menu font (3bpp), race
instruments (2bpp), palettes, cursor/border tiles and four machine side-profile
icons on the presentation side. Setup, loading, individual HUDs, countdown,
pause, finish/results and rematch prompts use those assets on the native black
background. Single-player UI is untouched; no bitmap/font/ROM data is embedded
or redistributed. Numeric menu prompts use the actual race numerals at the
menu lettering's height; `$40..$5F` contain instrument fragments, not digits.
The original race clock punctuation and instrument colors are retained in the
adapted two-human layout. Player labels use the native cyan/yellow palettes.

Windows desktop/headless builds pass; CTest 17/17 passes (295.76 seconds,
including the 275.51-second full catalog fixture). The virtual-controller
race-flow fixture passes after the UI change. Private visual artifacts at
`captures/l2p-ui-01` cover waiting/joined/ready setup and countdown/racing/
one-finished/pause/results. All 16,384 decoded menu-font pixels and 1,536
machine-icon pixels match the actual uploaded VRAM from the unmodified native
machine-selection capture. Visual review caught and corrected wrong number
and cursor tile assumptions before this milestone.

`captures/l2p-ui-life-01` repeats the complete Windows host lifecycle suite
successfully, including real disk/ring restore, fresh processes, viewport
changes, exact replay, deliberate divergence and atomic corrupt-state refusal.
The ordinary route still reaches race hash `da14e581ab108d4d`. Same-build desktop
replays (300 frames each) pass every recorded race check at native and HD2;
both 48-line instrument strips in HD2 match exact nearest-neighbor replication
of native pixels. These bounded checks do not qualify sustained HD performance,
all stock races or physical human input. The separate earlier preview remains
unchanged. Continue with launcher/player-binding integration, the five failed
automated course cases, remaining machine effects and broader acceptance gates.

The owner requested launching the full recomp-ui launcher once ready with local
two player and netplay. This pinned F-Zero host currently supplies no netplay
backend/callbacks, and the approved plan excluded netplay. A clarification is
pending about expanding scope to working online racing; do not expose an
unconnected Netplay button or claim network support. Local work continues.

The owner clarified **finish and launch local 2P first**; netplay remains out of
scope for this build. The full recomp-ui launcher now advertises two supported
players and persists both players' device/source/deadzone choices. Local gamepad
reads honor the launcher's independent `Controls` / `ControlsP2` maps and
`DeadzoneP1/P2`, retaining explicit `Local2P.P1/P2` overrides. Keyboard input
uses the assigned seat's keybinds, including P2, or the next free seat's Start
binding when joining. The single-player reader still uses P1 and its original
device-profile precedence. Windows desktop builds and the four relevant
hotkey/state-mode/gamepad/local-controller tests pass. Expanded virtual input
checks cover different maps/deadzones on the same GUID, explicit override
priority, and keyboard joining/controlling P2. Full 17/17 precedes this bounded
input follow-up; physical controller acceptance remains outstanding.

## Owner playtest corrections and revised UI goal (2026-10-15)

The owner tested the separate `cb81022` preview and rejected automatic entry
into two-player setup when the mod is enabled. The mod must make **VS MODE**
available on the native main menu; the normal one-player choices must still
work by default. Selecting VS opens two-player setup. Returning to Main Menu
must restore the single-player context, content settings and save namespace.
The owner explicitly permits host-side work and targeted guest injection to
avoid old hardware limits and artificial setup delays.

Additional required corrections: replace ambiguous action shortcuts with a
cursor-based pause/results flow (Continue, Restart Race, Change Race, Main
Menu as applicable), with obvious navigation/confirmation; make player numbers
filled and readable; remove both black instrument bands and overlay native HUDs
on two complete half-screen views separated by a thin line; show an actual
machine explosion on crash-out. Do not count the earlier art-only visual review
as acceptance of these interaction/presentation requirements. Netplay remains
out of scope after explicit owner clarification.

First feedback follow-up: `fzero_native_ui` now decodes clear, filled 8x16
numerals from the native `$0F:8000` OBJ stream (`$80..$89` / `$90..$99`) for
menu/player identities. Small identities use the same shapes at eight lines;
the original clock/speed instruments retain their native race numerals. The
versus scenery capture enables its existing panorama in the formerly hidden
top band and clears native power-window color math there. HUDs now overlay the
scene and a single native-pixel line separates the two full views. Ship/label
clipping covers the whole pane. Single-player capture paths are unchanged.
Private final render at `captures/l2p-ui-feedback-01/overlay.png` was inspected;
the first intermediate exposed obsolete power-window tint, corrected before
validation. Two-frame restored continuation remains exactly tick382/race202/
hash `ef5c78c3d4124f17`, including both poses/statuses. Windows hosts build and
the focused video/mode7/renderer/gamepad/local-controls checks pass.

The long wait is still uncorrected: the old desktop path paces roughly1,600
native bootstrap frames at normal speed. Even the unpaced 1,800-frame headless
route took5.845 seconds on this host, so merely removing wall-clock pacing
will not meet the intended transition quality. Replace the fixed320/800/920/
1090/1150 input schedule with verified scene-driven setup and avoid redundant
initialization/rendering, using the running native menu context where valid.
Do not fake a saved initialized race or inject expected physics outcomes.

Verified entry points for the next change: stock main-menu state handler
`$03:8127`, native selection `$5A`, max-index calculation `$03:814B..8154`,
confirmation `$03:8198..81C1`; the two native cursor Y bytes are `$02CD/$02D1`,
with Y=`$68+24*selection` and that plus8. Records availability comes from `$F2`
and the existing always-records hooks at`$03:80FE/$03:8151`; preserve it. Menu
input is adapted in `fzero_runtime.c::run_one_frame` before `RtlSetPadState`.
`FzeroLocalConfigure` currently forces the byte interpreter and its course hook
is`$00:9F08`. The pinned engine already supports in-process session teardown/
reboot (`RtlWriteSram` -> `snes_free` -> `RtlRegisterGame`/`SnesInit` ->
`RtlReadSram`), documented in `runner/src/content_variant.h`; no dependency pin
change is implied. Reuse/restore the actual single-player context and guard
audio, buffers, replay, rewind and root changes when implementing the native
menu transition. Menu, fast setup and explosion work remain incomplete.


## Revised UI implementation and Windows QA handoff (2026-10-15)

The owner feedback changes are implemented. Local 2 Player now adds a native
**VS MODE** main-menu row while normal startup and one-player choices remain
available. The title row and shared cursor menus use decoded cartridge art.
Pause offers Continue / Restart Race / Change Race / Main Menu; results offer
Race Again / Change Race / Main Menu. Up/Down selects and Start/Enter or a
confirm face button activates the highlighted choice. The old per-action
shortcuts are removed. The filled player identities, complete half-screen
views, inline HUDs and thin divider from `be73b37` remain in place.

`fzero_local_mode` saves the real single-player context, isolates the versus
save root, and restores the original RAM/profile/namespace on Main Menu.
Modified single-player content is rebuilt around the stock versus session;
dependency revisions are unchanged. A consumed VS confirmation parks native
CPU menu processing before the return snapshot, preventing a delayed Start
sample from entering Grand Prix. Both native and HD desktop flows now verify
that the returned main menu stays there for subsequent ordinary frames.

New sessions prepare native course assets while players join/select and skip
the redundant native countdown. Both initial and changed-course Ready actions
in the final native and HD2 desktop tests reported **0.000 seconds / 0 native
frames** of remaining setup. This measures the Ready transition after preload,
not zero total initialization cost. Existing version 1/2 states keep their old
bootstrap behavior; version 3 preserves the new setup/menu/effect state.

Crash-outs hide the intact ship and play the cartridge explosion layouts,
tiles, colors and timing in both cameras before exposing results. A 5,824-pixel
comparison against native uploaded OBJ data passes; a mid-explosion snapshot
restored in a fresh process yields an identical final composed frame. Stock
L/R handling was already present in the authoritative actor path. Both seats'
shoulder bindings and all four stock machine actor checks pass. The renderer
now reflects native steering/lean state with cartridge angle poses; this is a
presentation approximation rather than an exact human OBJ animation decoder.

Validation: Windows CTest 17/17 passes (1,328.43 seconds; the Deluxe test reports
its existing no-stock-ROM skip). The explicit-ROM stock actor oracle passes.
The current host lifecycle suite passes disk/ring/fresh-process restore, exact
replay/divergence checks, atomic bad-state refusal, namespaces and all 15 course
setups. Final muted desktop tests use two SDL virtual controllers and ordinary
input through native VS entry, join/Ready, Restart Race, changed course/machine,
Main Menu and continued single-player menu frames. Both native and HD2 pass;
the final HD menu image was visually inspected. Private evidence is under
`captures/l2p-feedback-02` and `captures/l2p-feedback-life-01`.

The owner offered to perform hands-on QA. The separately staged, muted full
recomp-ui launcher is `build/playtest-local-2p-vs-menu/open-launcher.ps1`, with
Local 2 Player enabled and ordinary one-player startup. Its validated executable
was built from the exact pending source represented by this commit; the binary
still identifies the precommit `be73b37-dirty` revision and source fingerprint.
Older preview copies are preserved. No netplay backend is exposed.

Full feature qualification remains open in `beads-8uc1`: physical two-human
playtesting, sustained HD/rate/worker performance and remaining machine effects
are outstanding. Ordinary-input full-race bots now finish Mute City III, but
Port Town I, White Land II, Port Town II and Fire Field still end in DNFs and
are unqualified; all-course setup success does not establish all-course race
completion. No Linux validation was performed, per owner direction. The known
baseline single-player strict rewind clock discrepancy remains separately
recorded. Do not close the issue or claim all original acceptance gates passed.


## Owner QA: title style and resize regression (2026-10-15)

The owner rejected the VS row's mismatched lettering, color and copyright
spacing, and reported corruption after resizing with an apparently stuck
countdown at 2. The prior state-only desktop flow was inadequate: it could
return success while displaying a stale frame. Owner-requested independent
Astra xhigh QA reproduced the old corruption while all 139 compared local
simulation records remained identical to the clean run.

Root cause: `FzeroSetViewport` reset the shared native renderer whenever the
logical width changed. Versus reuses that immutable native asset capture while
the guest CPU is parked. The next camera publish therefore failed, and the
old pixel buffer was uploaded with the new width. Resizing now retains native
assets for an active versus session; ordinary single-player invalidation is
unchanged. The owner recording subsequently reaches racing with countdown 0,
supporting the frozen-display explanation for the stuck number.

The title adapter now decodes the actual main-menu letters at `$0C:F800`,
replacing the unrelated course-introduction font. M/V, absent from the original
menu words, reuse cartridge shapes with the title font's shading and black
outline. Native words and VS share 20-pixel row spacing, with clearance above
the copyright, and the native cursor and selected-row blink. Live native OBJ
palettes and PPU brightness apply to VS through the title fade. The original
GP/Practice/Records OBJ art remains.

The desktop validation driver now resizes the actual SDL window during
countdown and racing to 600x800, 1342x1378, 1280x720, 1600x600 and 768x576. It
requires valid rendering and fresh HUD pixels, presentation of countdown 3/2/1,
and progression into racing, alongside native entry/join/Restart/Change/Return.
Actual logical, physical and renderer dimensions must settle before a capture
counts. Opt-in captures include composed buffers and real presenter readback
before swap, plus state/dimension metadata; the first menu image waits for
native title pixels. A hidden-window option permits muted GPU checks. The
fixtures do not inject actor or progress outcomes.

Focused Windows CTest: 8/8 pass in 17.23 seconds, including the explicit-ROM
stock actor oracle, renderer, video, Mode7, gamepad, local controls, hotkeys and
state-mode checks. Normal one-player input with VS availability off/on reaches
race scene 02/03 and yields byte-identical 128 KiB RAM: SHA-256
`89426e6f0098c8db953e283ae7d305e9e9f728cf3fb966bc0e492610aa333247`.

Independent Astra xhigh first-pass QA covers native and HD2 on software,
Direct3D11 and OpenGL (`sharp.glslp`), with clean presenter images across all
five sizes. Follow-up QA caught the missing native fade and an asynchronous
resize capture that still showed the prior size; both are corrected. The final
binary (SHA-256 `a188e3a701cc62f136bf6c3201069e6acfbdc1f0e92a4e62293d52e8eb5ebaae`)
passes native/software with Records enabled and HD2/Direct3D11 at 60 Hz, with
all five requested output sizes verified. Actual countdown 3/2/1 and advancing
race time render correctly. Native GP and VS foreground RGB sets match exactly
in both the fade and steady state. The four menu rows are at 104/124/144/164,
leaving 12 clear rows above the copyright. Evidence remains private under
`captures/l2p-astra-qa-01` and `captures/l2p-qa-resize`.

These are bounded presentation checks with SDL virtual devices. The UI driver
holds zero throttle during its racing samples; they establish phase/timer and
rendering progress, not driven full races. Physical human play, the four
previously unqualified full-race bot cases, sustained performance and other
original acceptance gaps remain open in `beads-8uc1`. No Linux validation was
performed, per owner direction. Dependency pins and private assets are
unchanged; the absent `snesrecomp` gitlink is not staged.


### Steering artwork and title V correction (2026-10-15)

Owner playtest found that L/R animation used a guessed opponent-facing offset,
and that the added title V had an asymmetric silhouette. The followed human
now uses the cartridge human OAM layouts ($D71D..$D8BE, $F4E9..$F7C9) for ground
steering and L/R, including extreme-turn pieces and Golden Fox's small nose.
The other camera retains actual relative-facing/distance artwork. Ready and
countdown render a neutral pose through a presentation copy, without changing
race state. Air/recovery continues through the existing projected-ship path.
The V uses the cartridge intro glyph's complete taper/tip with native title
shading and outline.

Visual inspection caught a gap in the first CPU/OAM oracle: it shared an
incomplete texture map and therefore accepted missing Blue Falcon/Golden Fox
pieces. Their additional tilt art comes from shared tiles in bank $09 and
bank $0B, alongside the selected machine's $F000..$FFFF data. These are now
included. A native DMA observer confirmed the shared lower upload starts at
word $49C1 (source $0B:FE00, 64 bytes), leaving the preceding word untouched.
The ROM-only raster initializes that gap to zero instead of using stale VRAM;
it also preserves the upload's final overlap at $49E0. Temporary observer code
was removed before the final build. No dependency revisions changed.

The corrected oracle passes 1,456 complete human pose/layout bitmap fixtures
(four machines, 13 steering/shoulder poses, seven animation scales, four bob
heights). An additional opt-in FZERO_TEST_PLAYER_CAPTURES oracle independently
reads private native PPU/OAM/VRAM captures: four neutral references and both
L/R directions on all four machines pass. These artwork comparisons normalize
the one-frame capture bob, the independently animated engine-glow palette and
the single untouched upload word described above. They do not qualify those
separate palette/effect behaviors. References remain private in captures.

The desktop driver now actually accelerates both SDL virtual controllers,
uses L/R in both directions and ordinary steering, and requires movement,
nonzero speed and the correct native DD=80/81 poses. Slow HD screenshot
readback initially sampled after releasing a shoulder input; the fixture now
holds each input until its corresponding presenter image is captured.

Final muted Windows runs pass native/software with machines 0/1 and HD2 on
Direct3D11 with machines 2/3, including all five settled resize sizes, actual
3/2/1/countdown-to-racing progression, six driven samples per run, Restart,
Change Race and single-player return. Actual presenter images show complete
ships, scenery, overlay HUDs and separator. Final desktop binary SHA-256:
`f94344c53e0fb3e44a37e7843877ba991503d0fc663a419448d587947c287c9f`.
Evidence: captures/l2p-native-lean-04 (stock references) and
captures/l2p-native-lean-05-v (final desktop runs). Eight focused CTests pass in 13.29 s.
Single-player with VS availability off/on still produces identical 128 KiB
RAM at race scene 02/03, SHA-256
`89426e6f0098c8db953e283ae7d305e9e9f728cf3fb966bc0e492610aa333247`.

This qualifies ground steering/L/R presentation and the exercised Windows
menu/resize paths. Physical two-human QA, sustained cadence, air/recovery
effects, the four previously unqualified full-race bot cases and remaining
original gates are still open in beads-8uc1. Screenshot runs include readback
stalls and are not performance certification. No Linux build was performed,
per owner direction. The absent snesrecomp gitlink remains unstaged.

Independent owner-authorized Astra xhigh read-only review confirms complete
ship silhouettes for all four machines and clean HUD/divider/resize images.
It caught a Y-like tail in the initial geometric V candidate; using both the
core and highlight pixels of the original cartridge V restores its taper.
Astra's follow-up accepts the final V shape, shading and outline. Native and
HD2 desktop flows were repeated on the final binary above (21.30/20.41 s,
both pass), plus renderer/local-controls CTests (2/2, 3.20 s). This supplements
the earlier eight-test run and twelve private stock artwork references.


### Handling phases, transparent instruments and elimination results (2026-10-15)

Owner QA found a real missing state update: native $C507 advances exhaust
startup/decay in E1..E5, and $9B4F reads E1 for hover and airborne descent.
These phases now advance in the authoritative tick, alongside $BED7's DF spark
phase and $BF5F/$BF83/$C0AC transient effect timers. They persist atomically;
rendering never advances them. Native extreme-turn sparks use the cartridge
OBJ layouts/art/palette in both views. This does not qualify every airborne,
recovery, exhaust palette or track-effect presentation path.

A read-only full-tick oracle observes naturally executed stock Practice from
$A1C0 to $8DD1, with no guest writes. It compares handling, position/fractions,
velocity, hover, steering/animation/exhaust, surface, checkpoint/boundary and
transient effect fields. Both per-tick initialization and independent persistent
continuation pass on all four machines: 859/1199/827/1199 ticks per mode,
8,168 comparisons total, zero mismatches. Inputs include throttle, L/R,
steering, braking and course edges. An initial persistent-observer omission of
session-owned finish-side tracking caused a late mismatch; correcting that
observer resolved it without changing checkpoint physics. The reproducible
runner is tests/validate_actor_frames.py. Evidence: captures/l2p-native-frames-02.
These are bounded routes, not all-interaction/full-race parity certification.

The scalar oracle now covers 1,200,000 fixtures, plus 40,000 native transient
effect fixtures. CPU/OAM image checks cover the original CRASHED OUT banner
and all 20 spark sprite selections. These layout tests share the decoded OBJ
atlas; they are not an independent native PPU qualification of every effect.

HUD text/digits/player tags skip cartridge black backing pixels, including
TIME and POWER. Generic bottom menu instruction lines are removed. Contextual
join/ready/disconnect messages remain. The stock result text is CRASHED OUT
($CD2B); its exact paired OBJ glyphs and palette replace the generic framed
CRASH OUT message. DNF times use --'--"-- in the native clock style.

Owner selected last-survivor completion: the match freezes when only one
non-crashed participant remains, plays the explosion, then exposes results and
rematch choices. SURVIVED is distinct from FINISHED; no five-lap finish/time is
fabricated. Results show each machine/outcome and a time only for an actual
finish. A count-based completion policy is independently checked with four
racers: one or two crashes leave the remaining field racing. The host/config
still support two players. Both simultaneous DNFs produce no winner. Tests
cover either survivor, simultaneous real surface-damage DNFs, terminal pose
freezing, save/restore and a clean rematch, without injecting race outcomes.

Rules identity is now 2, session magic FZVS002: earlier incompatible versus
snapshots/replays are rejected. Single-player snapshot format is unchanged.
Actual Mute City neutral/throttle input produced a P2 crash and P1 survival;
explosion-age-10 save, fresh-process explosion/results restore, actual rewind
ring and ten-frame continuation pass. The host lifecycle validator also passes
both-player save/load, capture replay/divergence, tamper rejection, viewport
invariance and single-player namespace protection (captures/l2p-state-rules2).

Muted native and HD2/D3D11 desktop runs enter VS from the stock title, join two
SDL devices, accelerate/shoulder/steer, display actual DD=0/10 with nonzero DF,
resize to all five sizes and navigate Restart/Change/Main Menu. Presenter images
show native sparks and transparent HUDs (captures/l2p-effects-final-02). A QA
fixture initially waited for extreme-turn sparks while holding normal steering
alone; adding the matching shoulder fixed that fixture timeout. It was not a
confirmed game startup stall. Temporary startup instrumentation was removed.
Readback stalls in these runs do not certify sustained cadence.

The four previously unqualified stock full-race bot courses were rerun with
rules 2: Port Town I, White Land II, Port Town II and Fire Field still fail to
complete five laps because a bot crashes. These failures remain open; no stock
physics has been altered to make a bot pass. The other eleven full races have
not yet been requalified on rules 2. Physical two-human play, remaining native
air/recovery/effect paths, sustained performance and original acceptance gaps
remain open in beads-8uc1. Windows only per owner; no Linux build was run.
Dependency pins/private references are unchanged; the absent gitlink is unstaged.
