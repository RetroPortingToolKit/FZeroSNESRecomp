# HD Mode 7 rebuild review build

The implementation lives on `fix/mode7-perspective-rebuild` in the isolated
engine and F-Zero worktrees. Owning issue: beads-8wg.2.192. No merge/release.

The title publishes raw camera matrices, immutable streamed/full course maps,
native composition and temporal camera selection. Shared code reconstructs the
spatial plane for CPU/GPU output. The old scanline API and duplicated HD sampler
are removed. Resolution (1..64x GPU) and supersampling (1=off, 2..4 per axis) are
separate persisted launcher controls. No old HD API or snapshot migration shim
was added. Native in-game saves are preserved.

## Prepared comparison

Run [LaunchComparison.cmd](../_compare/LaunchComparison.cmd) in this private
worktree. One simulation draws native Mode 7 on the left and rebuilt 16x on the
right. The normal window starts paused with normal audio enabled for playback.
F1/F2/F3 select grid/straight/turn scenes; P plays/pauses both. The title bar lists
these controls. Resize either while paused or moving. Supersampling is off so
this comparison exposes actual geometry and texel sampling.

[Instructions](../_compare/README.md) and [owner notes](../_compare/NOTES.md)
are local to the comparison. Settings and disposable snapshots are isolated
from the owner's normal profile. No delivered ZIPs or regular saves were changed.

## Validation and limits

Shared PPU tests, 772 GPU ROI comparisons, CPU/GPU supersampling and GL
memory/state checks pass. Windows desktop/headless builds, F-Zero renderer,
video and mod suites pass. Linux NDEBUG reconstruction and the portable
GPU-disabled build also pass. Super Mario Kart's strict 3,600-frame LLE run
exercises real shared capture, native parity and 4x reconstruction.

Matched frame 1200 road-edge error against bsnes-hd falls from 10.979 to 0.275
high-resolution pixels on the measured rows. Six matched turning frames retain
original/rebuilt/bsnes-hd images. These specific geometric/sampled-motion checks
do not prove whole-game or continuous-motion accuracy. See the engine's
[research and acceptance record](../../_wt-mode7-rebuild-engine/docs/HD_MODE7_REBUILD.md)
for frozen references, settings, timing variation and remaining game checks.

A hidden 1,800-frame OpenGL 8x route passes with zero missed presentations.
Final checks add 16x single-player resize/supersampling and a 1,440-frame
two-player GPU resize route with zero sampled mismatches or missed presentations.
A four-player course-14 route completes 1,440 frames on the existing CPU
fallback, with 420 missed presentations; that is functional evidence only.
The title adapter still lacks four-player GPU jobs. CPU supersampling there
is especially costly. Runtime
capture timings are mixed under host contention; no general performance win is
claimed. CPU fallback remains slower than the original approximation.

Snapshot replay has the same 22-master-cycle defect in the original and
candidate builds (beads-8wg.5.122). Loading the prepared snapshots works, but
deterministic save/load replay is not qualified. Other-game visuals, broad
natural course play and owner visual acceptance remain open. This build is
prepared for comparison, not a fully qualified release.

## Retained local evidence

`build-mode7/` holds builds, source/binary identities, private captures, exact
jobs and run logs. `build-baseline/` retains the original headless snapshot
control. Engine `build/oracle/` retains bsnes-hd settings/core identity and
matched images. These are ongoing review evidence outside delivery folders.

Windows refused non-forced removal of the agent-created save fixture directory
`C:/Windows/Temp/m7-gjbf8jc2`; a copy is under
`build-mode7/gpu-smoke/save-fixture`. The protected original still needs cleanup.
No attributes, ACLs or security settings were changed.

## Motion flicker correction (2026-11-02)

Owner visual acceptance failed: intermittent incorrect screen regions while
playing, not paused. The raw HD camera publisher blended signed 13-bit centre
and scroll registers with an 8192-unit period, while the full-course reference
already used the hardware map's 1024-unit camera aliases. A change of camera
representative therefore moved the HD texture through another course block for
intermediate presentations; emulated frame endpoints remained correct.

A real consecutive capture reproduces it at frames 1456/1457: world camera X
3192 -> 3200, centre X 1144 -> 128, scroll X 1016 -> 0. At alpha 0.5 the old
publisher takes a 512-unit wrong turn. The corrected 4x image changes 24,777
pixels; endpoint alpha 0 and 1 images remain byte-identical. The retained
`build-mode7/flicker/transition.png` shows old endpoint, broken midpoint,
corrected midpoint and new endpoint from left to right.

The title adapter now blends the centre with period 1024 and the already
clipped centre-relative scroll independently. Its map reference and matrix
stay in the same representative. Shared spatial reconstruction is unchanged.
Regression coverage includes stationary aliases and the captured eight-unit
movement, both axes, horizontal/vertical flips and eight temporal phases.
These checks failed before the fix and the complete renderer suite passes
afterward. Owner recheck remains required; do not erase the reported failure.

A non-benchmark 1200-frame 16x run at requested120Hz exercises real presentation
interpolation:2387 presentations,27 sparse CPU/GPU comparisons,25 with
fractional camera values, zero sampled mismatches. Eight missed presentations
under host contention are recorded; this is correctness evidence, not a perfect
120FPS qualification. Evidence is retained under build-mode7/flicker.
