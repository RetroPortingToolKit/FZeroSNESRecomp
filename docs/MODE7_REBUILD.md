# HD Mode 7 rebuild qualification

Shared engine details and frozen emulator references:
[HD_MODE7_REBUILD.md](../snesrecomp/docs/HD_MODE7_REBUILD.md).
Owning issue: beads-8wg.2.192. This branch is a candidate, not a qualified release.

The title adapter publishes raw camera matrices and course-map references.
Shared code reconstructs perspective for CPU and GPU output. Native HUD and
sprite composition, adaptive aspect handling and temporal camera selection
remain title-owned. No old HD contract or save-state migration was added.

Validated: renderer suite; Windows desktop/headless builds using supported
regeneration; muted 1800-frame headless run; hidden OpenGL 8x 1800-frame run
with zero missed presentations. Shared tests additionally cover Linux CPU
rendering, EXTBG/direct color, camera boundaries and CPU/GPU texel parity.

Current-build snapshot replay fails the same 22-master-cycle comparison in
the original and candidate builds. This is tracked as beads-8wg.5.122; it is
not a passing save/load qualification.

## Local evidence and remaining work

`build-mode7/` contains the candidate game/tools, original/candidate PNGs,
CPU/GPU timing samples, runnable jobs, capacity receipts, binary identities,
headless/desktop logs and isolated test saves. `build-baseline/` retains the
matched original headless build used to separate the snapshot defect.
These are ongoing qualification artifacts, outside delivery directories.

The GPU capture result improves at both 4x and 8x; the CPU fallback remains
slower than the old approximation. See the engine record for exact numbers
and the limits of those measurements.

Still required: matched bsnes-hd images/motion, representative other-game
qualification, broader F-Zero course/split-screen/resize checks and owner visual
acceptance. Supersampling controls and retirement of the old diagnostic
scanline API are unfinished. Do not describe the full approved plan as complete.

Windows refused non-forced removal of the agent-created QA save directory
`C:/Windows/Temp/m7-gjbf8jc2`. A copy is retained under
`build-mode7/gpu-smoke/save-fixture`; the original directory needs cleanup when
its protections permit. No attributes, ACLs or security settings were changed.
