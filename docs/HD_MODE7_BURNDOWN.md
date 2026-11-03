# F-Zero HD Mode 7 quality burndown

Planning baseline: 2026-11-03. Tracking: `beads-8wg.5.126`.
Shared renderer work: `beads-8wg.2.192`; optimization coordination:
`beads-2wy8.1`. Game source: `c784622`; engine: `66eebe6a`;
launcher: `b13c876`.

## Acceptance reset

The owner reports that experienced F-Zero players reject the visual quality of
our HD rendering. Treat overall HD quality as **failing acceptance**. The exact
scenes and settings behind that second-hand feedback are unknown; do not claim
that it constitutes a controlled retest of a particular preview.

The previous camera-flicker fix, isolated silhouette improvements, CPU/GPU
agreement and successful builds remain useful evidence. They do not establish
that the game looks correct to its players. Preserve the historical results in
[the rebuild record](MODE7_REBUILD.md), including their limitations.

The outcome is a visibly convincing HD Mode 7 implementation, stable in motion,
with understandable controls and practical runtime cost. Prefer replacing or
simplifying a failing renderer over accumulating exceptions around it.

This is the active plan and decision record. **Two of twelve acceptance areas are
resolved; ten remain open.** HD-01 has a source-audited product contract.
The first implementation candidates and their limits are recorded below;
no overall visual, performance or release acceptance is claimed.

## Separate the concepts before changing the renderer

The current F-Zero adapter exposes a Mode 7 sampling multiplier. This audit has
not identified a separate user control that independently renders the entire
SNES scene at higher internal resolution. Do not invent another slider merely
to make two categories appear in the UI.

| Concept | Meaning in F-Zero | Proposed product decision |
| --- | --- | --- |
| Window size / display scaling | Size of the presented picture. Enlarging an existing pixel does not recover road detail. | Keep under display settings. Label it as window size, not graphics quality. |
| General internal resolution | An independently higher-resolution scene render, beyond the Mode 7 work below. | Remove a separate F-Zero feature if the audit finds no distinct visual benefit. Shared capabilities for other games can remain. |
| HD Mode 7 detail | Evaluate the transformed track at finer positions using the original course graphics. | Keep as part of HD Mode 7; prove that the selected setting affects the actual road renderer. |
| Perspective correction | Reconstruct consistent geometry from the game's limited-precision scanline transforms. | Make correct geometry part of HD Mode 7. Diagnostic overrides can isolate defects; players should not need a correctness workaround. |
| Edge smoothing / supersampling | Average several Mode 7 samples to reduce aliasing. This is independent of the detail grid. | Keep only qualified settings. Explain that 2/3/4 samples per axis means 4/9/16 samples, rather than calling everything “resolution.” |
| Display filtering / shader | Filtering applied to the composed picture; it can also change text and sprites. | Keep separate from Mode 7 sampling. Disable it during baseline comparisons; preserve explicit player choices. |
| Presentation FPS | How often a picture is presented, including any camera interpolation between simulation frames. | Keep separate. Establish image correctness at original cadence before assessing higher rates. |

The starting UI proposal is **HD Mode 7 On/Off**, **HD detail**, and optional
**Edge smoothing**, alongside the existing display settings. Start detail
qualification at 2x and 4x, then assess 8x. Do not retain a 64x range or add an
Auto setting without proving their behavior and practical benefit. Exact
choices and defaults are decisions to earn through the matrix below.

Fit to screen remains the fresh-install aspect default. Updating a build must
preserve a player's explicit aspect, filtering and mod choices. Requested and
effective HD quality must agree, or the UI must explain the reduction in terms
the player can act on, such as “Using 2x for four players.” Backend/thread
details belong in diagnostics.

Generic projection, sampling and GPU composition fixes belong upstream in
snesrecomp. F-Zero keeps course assets, camera policy, HUD and player-view
layout. Shared launcher changes should expose reusable capabilities; retiring
a redundant F-Zero control must not remove a useful feature from other games.
Integrate qualified shared fixes upstream and then update the game's pin.

## First bounded comparison

Reuse the existing `tools/snesref` frontend, separately supplied bsnes-hd core,
scripted inputs and reference captures. The retained reference is bsnes-hd 10.6,
binary SHA256
`3e05e441f35ca21baa328094f6f9bf30645ea998ebb39956b154e160a8b00ed0`.
Its source/settings and captures are under the engine's existing `build/oracle`.
Verify those identities before reuse; no new emulator frontend is needed.

1. Freeze the current candidate and one representative short stock route with
   a straight, a corner and visible road boundaries. Use the same verified ROM,
   stock gameplay/vehicle profile and inputs in both hosts. Disable CGP handling
   changes for this comparison: equal inputs with different handling do not
   produce equal cameras.
2. Establish scene alignment with HD disabled. Match crop/overscan, pixel
   aspect, drawable dimensions, course, timer, camera and visible landmarks.
   Frame number alone is insufficient. The retained bsnes-hd core does not
   expose WRAM; do not claim memory-state equivalence. If alignment cannot be
   established, repair that prerequisite before scoring image differences.
3. Compare native, our HD and bsnes-hd HD at a matched 4x setting, with display
   filtering and supersampling off and original presentation cadence. Then
   compare the same route with matched supersampling. Inspect full images and
   short continuous motion, not only the best-looking road-edge crop.
4. Attribute the first clear discrepancy to source sampling, geometry,
   composition, temporal interpolation or presentation. Implement and evaluate
   that candidate next. Do not begin another broad instrumentation project.

Emulator HD states and native recompile save states are not interchangeable.
Reuse inputs and guest scene observations, regenerating build-specific states
as needed. Preserve native in-game saves.

## Burndown matrix

“Reject” means reject an implementation or elective control, not waive a
mandatory visual outcome. Each row needs its own result, source identity and
concise evidence link. All rows start open; known limitations are called out.

| ID | Area / ownership | Decision and acceptance evidence | Current status |
| --- | --- | --- | --- |
| HD-01 | Feature contract — F-Zero + launcher | Map every visible control to its real effect. Retire separate general internal resolution unless a matched scene demonstrates useful additional detail outside HD Mode 7. End with one unambiguous set of controls. | Resolved contract: no independent whole-game internal-resolution feature exists to retain. Keep HD detail and Edge smoothing; display size/filtering/FPS stay separate. Runtime UI/default qualification is HD-11. |
| HD-02 | Active path and effective quality — adapter + engine | Trace native/HD selection in actual races, results and unsupported scenes. Show that the road is resampled from course data, and distinguish a configured multiplier from a fallback or cap. Silent native fallback must not masquerade as active HD. | Open: actual GPU detail and portable2x reduction are checked in races/results; reductions now notify the player and remain visible as last-race status in Mods. Unsupported-device/native-scene and complete launcher qualification remain. |
| HD-03 | Perspective geometry — shared engine | Match independent bsnes-hd road shape, horizon and depth across straight and turning views. Fix systematic edge displacement, bends or horizon bands. Compare replacing the current fitted-plane approach with a reference-aligned alternative; evaluate substantial reuse where technically suitable. | Resolved for straight/turning geometry: seven independent matched stills and all151 consecutive4x frames improve; the owner accepts both SS1 and SS2 road/horizon clips. Engine07716db3 preserves all302 accepted images. Coordinate/artwork differences remain in HD-04/05; other motion remains in HD-07. |
| HD-04 | Source coordinates and texels — engine + F-Zero maps | Verify pixel centers, fractional rows, negative coordinates, wrapping, flips, tile boundaries and full-course/cache transitions. Independently known source coordinates must select the expected texels. No wrong course blocks or road seams. | Open. |
| HD-05 | Composition and original artwork — engine + adapter | Preserve vehicle/HUD/text detail, palette, brightness, priority, windows and native effects. Check menus, demo, records and results separately. Any intentional full-course extension must be distinguished from an ordinary reference-view mismatch. | Open. |
| HD-06 | Aliasing and supersampling — shared engine | Match the expected reduction of jagged road edges and moving stripe shimmer with equivalent reference settings. Preserve original artwork; do not conceal geometry defects with blur or a new default texture-smoothing filter. Select useful quality/cost levels. | Open: six matched4x/SS2 samples retain near-road RGB MAE0.022..0.138; continuous SS2 comparison and useful quality/cost/default decisions remain. |
| HD-07 | Motion and camera interpolation — F-Zero first | Check continuous straight driving, steering, rotation, camera wrap, jumps and camera discontinuities. Eliminate HD-only jitter, crawling bands, flashes and wrong-region interpolation. Qualify native simulation frames first, then fractional presentations. | Open; earlier alias fix covers one reproduced defect. |
| HD-08 | Final presentation — shared GPU + host | Compare full-resolution output followed by the chosen display filter against the optimized visible-sample path at the same settings. Keep the shortcut only if it preserves the intended image. Test resize, DPI and aspect changes; explain why higher requested multipliers may stop helping at a fixed display size. | Open. |
| HD-09 | Multiplayer — F-Zero + shared compositor | Qualify each 2P/3P/4P camera independently and together. Resolve the current missing GPU grid path and silent 2x CPU cap through implementation or an explicitly accepted, honestly described limitation. No claim that every mode uses the globally requested quality. | Open: corrected worker grid passes4P8x/SS2, contacts, transitions and five resized targets. Serial3P4x full raster passes. Portable2x disclosure is validated. SERVER timing still misses60Hz; whole-view quality acceptance remains. |
| HD-10 | Runtime cost and portability — engine + title | Measure simulation/presentation frame times, CPU/GPU cost and memory at already accepted image quality. Keep the game responsive and stable at original cadence. Qualify portable fallback and affected shared consumers; do not trade another game's reproducible regression for an aggregate gain. | Open: matched1vs2 camera workers improve both3P/4P with identical images/traces; automatic3-worker follow-up also passes. Neither reaches60Hz onSERVER. Cross-game and complete runtime qualification remain. |
| HD-11 | Controls, defaults and updates — F-Zero + launcher | Implement HD-01's chosen controls; verify fresh installation, persistence and requested/effective quality. Native display scaling must not be presented as recovered Mode 7 detail. Choose defaults from accepted results rather than a large multiplier. | Open. |
| HD-12 | Player acceptance and release — F-Zero | Present representative matched stills and motion to the owner/regular players, retaining individual Pass/Fail/Deferred notes. Close known visual failures and run affected gameplay/save/load/rewind checks before an HD quality release claim. | Open; overall visual acceptance currently fails. |

**Current result: 2 resolved areas (HD-01/03), 10 open.** Preparation, builds, screenshots and
CPU/GPU agreement are supporting evidence, not additional completed rows.

## Representative test matrix

Use a small set of deliberate scenes rather than every scale multiplied by
every course and mode. Expand only when a concrete failure requires it.

| Case | Controlled comparisons | What must be judged |
| --- | --- | --- |
| Stock starting grid, straight and corner | Native vs our HD vs bsnes-hd; matched 4x; no supersampling/filtering | Source detail, projected road shape, stripe spacing, distant boundaries and original sprites. Reuse retained aligned scenes first. |
| Same short route in motion | Original cadence; then high-refresh presentation as a separate test | Shimmer, swimming textures, horizon steps, flashes, camera continuity and persistent displacement. A few sampled frames cannot pass this case. |
| Repair strip, boundary, jump and track-map seam | Existing authorized pose/checkpoint fixtures plus brief ordinary movement | Visual road/terrain agrees with collisions and landings; no texture/camera seam. Positioning must not set the success being asserted. |
| Lap crossing, finish, results and return to menu | Before/during/after transitions; stock and one installed course | HD remains appropriate through the transition; no stale raster, broken overlay, font/palette change or leaked race graphics. |
| Supersampling | Same accepted scene: off, then matched reference setting; 1x behavior checked separately | Real reduction in aliasing, readable native artwork and disclosed cost. Our 1x/native shortcut must not be assumed equivalent to all bsnes-hd 1x options. |
| Presentation and resizing | Fixed image; 4:3 and default Fit; one wide window, one smaller window; nearest first | Effective resolution, pixel aspect, crop, letterboxing, HUD placement and equivalence of optimized/full output. Filters/shaders tested separately afterward. |
| Two local views and one online camera | Same captured camera through the single-view and split-view consumers | Equivalent quality/composition per camera. This is a rendering check, not WAN/netcode qualification. |
| Three/four local views | Every active pane, plus the unused 3P cell; same per-camera source | Correct independent cameras, honest effective quality and acceptable frame times. bsnes-hd has no matching native four-player VS mode; compare equivalent individual views, not a fictitious emulator VS implementation. |
| Installed content and native scenes | One representative CGP/Astra course, one other pack, BS content; title/demo/records | Source/palette correctness and native-artwork preservation. Stock emulator parity is established first; custom content needs equivalent assets before claiming emulator equivalence. |

## How a candidate earns a decision

- Inspect both the complete image and predefined road/skyline/foreground
  regions. Fix systematic geometry errors; investigate unexplained road-edge
  errors above one output pixel in an aligned, unfiltered comparison. Do not
  declare success from a low average while a visibly wrong region remains.
  Any justified sampling/color-policy difference must be explicit and reviewed.
- Define comparison masks before evaluating a candidate. Masks may isolate
  original artwork from the Mode 7 plane; they must not exclude a broken
  horizon, road edge or transition to improve a score.
- Keep CPU/GPU consistency and analytic coordinate tests, but use the external
  emulator and player observation to judge appearance. Two consumers of the
  same wrong transform can agree perfectly.
- Assess established renderer reuse/replacement before another sequence of
  local fitting heuristics. Check dependency and license fit before selecting
  reuse. A separately run reference emulator is already available.
- After two unsuccessful quality experiments, compare approaches again and
  choose a concrete replacement or simplification. Repeated tuning without a
  visible improvement is not burndown.
- Performance comparisons use the same host, scene, drawable size, quality,
  supersampling, filtering and presentation cadence. Record variation honestly.
  A faster image with reduced quality is not an optimization win.
- Require sustained original simulation cadence (about 60.1 Hz), and report
  presentation frame-time percentiles and stalls as well as averages. Qualify
  higher presentation rates separately. Use the existing profiler and measured
  target capacity; do not claim GTX 1060 behavior without testing that hardware.
- For shared changes, reuse representative affected-game checks under the
  existing engine issue/campaign. Label the actual package each game receives.
  Equal baseline/candidate defects remain separately tracked pre-existing
  defects; new regressions must be resolved.

## Execution order and stopping points

1. **Resolve the confusing contract:** HD-01/02, and the single aligned
   comparison above. Confirm whether “internal resolution” is a redundant
   concept here. This is not permission to add a second rendering pipeline.
2. **Repair the picture:** HD-03 through HD-06. Obtain an independently
   convincing straight and turning view before optimizing or expanding scales.
3. **Repair motion and presentation:** HD-07/08. Bypass a presentation shortcut
   during diagnosis if needed, then prove or reject it with matched output.
4. **Carry quality into every supported view:** HD-09, followed by HD-10's
   measured cost/portable/shared-consumer checks. Do not announce multiplayer
   quality parity from successful single-player screenshots.
5. **Ship a comprehensible result:** HD-11/12. Retire unused/redundant controls
   and old failing implementations after the replacement is accepted. No
   save-state migration work is required; native saves remain protected.

Reuse [the existing rebuild evidence](MODE7_REBUILD.md), the engine's
`docs/HD_MODE7_REBUILD.md`, `tools/snesref`, renderer tests, existing image/parity
checks, `tests/validate_live_menu.py` and current save/load/rewind routes. The
native-vs-our-HD comparison is useful, but does not supply the independent
emulator pane by itself. Extend an existing comparison only as far as needed
to resolve the next discrepancy; keep tooling design below ten minutes.

Prepare a small human review with paused stills, the corresponding moving
route and a multiplayer/transition bundle. Leave it windowed with normal audio
and isolated saves, with easy switching and locally recorded observations.
Preserve the owner's live exploration session. Human feedback is a release
criterion, not a substitute for the engineering checks above.

For each row, append its actual decision, commit, measured/observed change and
existing evidence location to this matrix. Retain useful evidence and clean
disposable captures/installs. No new collector, dashboard, general profiler or
large benchmark sweep is a prerequisite for starting HD-01/02.

## First evaluated candidates, 2026-11-03

HD-01 is resolved as a product decision: the source audit found no independent
whole-scene internal-resolution control. WindowScale changes display size;
Mode7Scale resamples original track data; Mode7Supersample adds samples per
axis; display filtering and presentation cadence remain independent. The shared
HD controls now say **HD detail** and **Edge smoothing**. No redundant slider
is added. HD-11 still requires actual launcher/default/effective-path validation;
the retained 1..64 range and8x default are not yet newly qualified.

Two concrete correctness candidates are implemented on the isolated branches:

- Restore the authored additive distance haze in single-player races/results.
  A previous full-row override removed it. Six aligned native comparisons fall
  from mean RGB channel errors around3.8 to0.065..0.261. Up to122 pixels still
  differ by more than one channel step, so this does not claim full native
  pixel parity. The original PPU capture independently aligns with the emulator.
  New race/results haze tests pass; existing VS-specific haze/shadow isolation
  stays tested.
- Suppress half-color math when main color is clipped to black, consistently
  in both native PPU implementations, portable HD and GPU. Independently known
  colors catch the old error; the full PPU suite passes. Shared commit
  `28f260c4`, issue`beads-8wg.2.194`. Broader built-consumer qualification and
  upstream integration remain pending.

A generic reciprocal-depth anchor candidate (`eddeda4c`) replaces the
least-squares fit. Both compared variants include the same restored haze.
At4x, supersampling off, seven aligned straight/corner full-frame RGB MAE
values change respectively:
`2.844 ->0.771`, `9.355 ->2.962`, `6.275 ->3.168`, `5.794 ->1.740`,
`4.437 ->0.474`, `13.069 ->0.535`, `11.693 ->0.559`.
All seven improve; this is candidate evidence, not HD-03 closure.

The remaining horizon difference is included in those full-frame scores.
A VRAM-only diagnostic isolates much of it to negative coordinates: bsnes-hd
truncates toward zero while our native/HD sampler floors. At8x/frame1630,
changing only that conversion temporarily reduces horizon RGB MAE16.236 to
1.658 and channels differing by more than8 from14.684% to0.00355%.
The diagnostic was reverted; preserve floor semantics and separately review
intentional full-course extension and native5-bit color policy. Do not adopt a
rounding discrepancy solely to improve emulator image scores.

Checks passed: seven shared PPU tests, independent clipping/half-color cases,
772 GPU region comparisons, non-GPU compile, F-Zero renderer and mod tests.
These are correctness checks; they are not a runtime performance measurement.
Continuous motion, multiplayer quality/effective caps, smoothing, representative
built shared consumers, defaults and human review remain open.

[The compact evidence index](../captures/hd-quality-burndown-20261103/evidence.json)
links source/binary identities, declared regions, all per-frame full/horizon/
near-road numbers and retained raw evidence. Existing reference captures/tools
were reused. The owner's live preview and delivered ZIPs are untouched.

## Multiplayer implementation and current decision

Game commit`ca2504b` renders each local3P/4P camera through the existing shared
GPU sampler, then assembles a title-owned atlas for normal display filtering.
Equal integer-sized panes give the visible-sample and full-raster presenters the
same nearest-sampling geometry; odd windows may leave one edge row/column.
No multi-camera projection special case was added to the shared engine.

The first serial3P8x run passed camera-by-camera CPU/GPU samples, atlas samples,
the empty cell, real driving/contact/reconnect and live/results menu transitions.
It does not qualify the later worker-pool candidate. Existing diagnostics put
camera composition around11..14ms, with small GPU draw costs; no performance
win is claimed from that instrumented run.

The parallel-preparation prototype reused the shared pool but the strict4P8x
SS2 check rejected its silent fallback: native packet preparation submitted
Run(NULL) inside a camera callback, which the shared API deliberately rejects.
The corrected4P input/contact/transition fixtures passed on SERVER/GTX760,
but **the GPU-grid gate failed**. The correction composes native rows directly
inside a camera task, with a regression that requires real packets when called
from the pool. The failed prototype is retained as a rejected result. The
corrected build and subsequent complete4P gate passed, as recorded below.

The existing contact fixture now observes every actual simulation tick to retain
a one-tick explosion sound during catch-up. Its original health/response checks
are retained. Keyboard QA also initializes its controller section explicitly.
[Individual outcomes](../captures/hd-quality-burndown-20261103/multiplayer-candidates.json)
keep the passed3P implementation and rejected4P worker prototype separate.
The frozen serial GPU-grid build subsequently passed4P8x SS2 on real GTX760:
every camera, the atlas, keyboard ownership, driving, odd resize and transitions.
Contacts were explicitly excluded from that serial graphics run; the corrected
contact observer passed separately in the rejected worker prototype. These are
separate outcomes, not a combined acceptance of the current worker candidate.

Corrected game `cbc3376`/engine `eddeda4c` built as EXE SHA256
`39c1eb58c0fc0925ecc396ce0801d37052edd714bb1ee36b693840a23eb93fad`.
Its nested-worker regression passes, and one coherent4P8x SS2 run passes actual
GPU output for every camera and atlas, keyboard ownership, driving, prepared
contacts, odd resize and live/results transitions. Serial3P4x full-raster output
also passes its complete gate. These replace the earlier prepared-only status.

Matched, unpaced1610-frame8x/SS1 runs on SERVER/GTX760 used the same binary,
Fit/nearest and fixed alpha1. Order was3P1,2,2,1 workers and4P2,1,1,2.

| Mode | One worker FPS | Two workers FPS | Later automatic3-worker FPS |
| --- | --- | --- | --- |
| 3P | 34.358,35.623 | 41.945,38.755 | 48.529,47.842 |
| 4P | 28.009,28.454 | 37.276,37.132 | 39.832,39.539 |

All23 presented captures and driving traces are identical in each mode across
these runs. Retain parallel camera preparation. The automatic follow-up was
not interleaved; background CPU load varied, and this is whole-scenario
throughput including setup/contacts/transitions. No60Hz, idle-host, GTX1060 or
four-worker desktop performance claim follows from these measurements.
HD-09/10 remain open; stop expanding this multiplayer timing matrix.

Game `1f1ad5f` now reports reduced detail on screen and retains the actual last
race detail in Mods. Changing detail/smoothing invalidates the cached status;
disabling HD reports its configured native state. A real4P8x/SS2 GPU run checks
all cameras and atlas at768x576,600x450,1600x600,1280x720 and1342x1006, with
zero sampled mismatches. A separate portable4P8x request uses2x, shows the
limit, and passes driving/contacts/resize/transitions. Native picker/preload
artwork is excluded from failure notices; both complete gates verify no false
HD-unavailable warning. The portable cap and successful GPU request remain
separate, truthful outcomes. Actual launcher/device/default qualification is
still open.

## Continuous picture comparison and next quality decision

151 consecutive cornering frames1630..1780 now reproduce the six original
immutable guest captures byte for byte. Reference raw frame1629 matches the
retained bsnes dump after1630 calls; that observed alignment is used throughout.
Both candidates include restored haze; reference is4x, perspective on(wide),
no display filter and no supersampling. Every full image improves with anchors.

| Region | Least-squares mean RGB MAE | Reciprocal-anchor mean RGB MAE |
| --- | --- | --- |
| Complete image | 7.313 | 1.542 |
| Horizon47..86 | 27.202 | 8.058 |
| Near road90..224 | 3.943 | 0.199 |

These scores retain the broken/different regions; they are not a motion pass.
The largest remaining near-region spike is a CHECK label that is white in our
captured native PPU image and orange in the independently run reference.
Changing source-coordinate rounding does not change that spike. Its underlying
native/reference timing or palette cause is unestablished; do not attribute it
to the new HD geometry or remove it from the full-image results. Negative-UV
floor/truncation and authored full-course extension remain separately disclosed.

Two full-frame, original-cadence comparison clips are retained under
`G:/Temp/Codex/fzero-hd-quality-20261103/continuous`: candidate-left versus
bsnes-right at4x with smoothing off, and the same with2 samples per axis.
Both panes use4:3 presentation, nearest scaling and no added display filter.
Six matched SS2 stills have near-road MAE0.022..0.138; that is supporting
composition evidence, not shimmer or quality acceptance. SS2-versus-SS1
entries in the raw diagnostic table are explicitly marked mismatched settings.

The next visual decision is continuous full-frame review of road and horizon
at original cadence: jitter, shimmer, bands and geometry jumps. Owner review
accepts the first stock-corner, smoothing-off clip: "Looks comparable." This
passes that specific road/horizon motion checkpoint. Owner also accepts the matched smoothing2 clip: "still looks comparable."
Neither checkpoint is blanket acceptance of jumps, wraps, high-refresh motion,
other content or the complete renderer. Further
multiplayer performance tuning must not displace this picture-quality gate.
The existing pinned SMK consumer run exposed a real cross-game defect:
anchors alone correct664 sampled rows, versus1909 in the earlier validated
least-squares run. Game `d302c30` was regenerated through the supported native
analyzer and ran3600 strict DSP1LLE frames; native success alone did not hide
this HD fallback regression.

Engine `07716db3` now tries a validated whole-band regression if the anchor fit
fails, with identical residual/pole/subrow bounds and no game-name selection.
Two measured camera coefficient columns cover the opposite quantization cases.
The regression fails anchors-only and passes the corrected implementation.
SMK now corrects2241 rows across27 sampled frames, preserves sampled native
parity, and matches the historical final logic/video hash and master cycles.
This is a consumer check, not full-HD adoption into SMK's custom host.

All302 F-Zero SS1/SS2 comparison images remain byte-identical to the clips the
owner accepted, so those visual observations carry forward without another
review. Seven shared PPU tests,772 GPU comparisons, the F-Zero renderer gate,
Ubuntu GCC13.3 NDEBUG reconstruction and a GPU-disabled object compile pass.
The new-fit4P8x/SS2 live GPU gate passes, including every camera, contacts,
resizing and transitions. Broad shared-game/runtime acceptance remains open.

Machine-readable comparisons, source/binary identities and individual outcomes
are in the existing evidence directory. Raw captures/QA installs remain in the
bounded task scratch for this ongoing review and require cleanup when no longer
needed. **The matrix now has2 resolved areas and10 open acceptance areas.**


## Accepted geometry checkpoint and current lifecycle checks

The bounded independent focus review confirms HD-03's stated straight/turning
criterion is satisfied. Retain the reciprocal-anchor fit with its validated
whole-band fallback. Preserve the two owner passes without requesting repeat
review of byte-identical footage. This does not close coordinate wrapping,
composition, jumps, fractional motion or overall renderer acceptance.

Current EXE SHA256 `77e0ffc3939f3febb40aa5864453b2060d3858771b63c8b95837bd20555d379b`
uses game runtime `1f1ad5f` and engine `07716db3`. A fresh-video-settings2P run
uses actual8x GPU output and adapts to five window shapes; all sampled output
checks match. Its source defaults are Fit, smoothing off and automatic
presentation rate; the real run uses60Hz and preserves native menu artwork.
This validates the current fresh behavior, not a decision that8x is the best
default or that the full launcher persistence contract is complete.

The existing save/load route succeeds at4x/SS2 and120Hz requested presentation.
After restore, sampled GPU output still matches, including fractional cameras.
A clean private rewind-enabled profile passes controller and keyboard access
to the state browser, and opening/cancelling rewind. The initial overlay run
was an invalid fixture: rewind was disabled and the replay input replaced the
virtual pad. No production hotkey change was needed. Committed rewind is not
qualified by this open/cancel check. The separately tracked pre-existing
22-master-cycle snapshot replay mismatch remains `beads-8wg.5.122`.

One current-build local4P8x/SS2 run averages67.776FPS over1610 simulation frames
and23.755seconds on the local PC. It includes setup, contacts, resizing and
transitions; recorded stalls prevent a sustained-cadence claim. Stop expanding
the worker timing matrix. The next visual decision is the prepared native/HD
jump and landing clip, with the owner's individual observation still pending.
See [current runtime checks](../captures/hd-quality-burndown-20261103/current-runtime-checks.json).

## References

- [bsnes-hd feature and settings definitions](https://github.com/DerKoun/bsnes-hd#settings): Mode 7 scale, perspective correction and supersampling are separate concepts.
- [Pinned bsnes-hd renderer reference](https://github.com/DerKoun/bsnes-hd/blob/fc26b25ea236f0f877f0265d2a2c37dfd93dfde9/bsnes/sfc/ppu-fast/mode7hd.cpp).
- [Libretro bsnes-hd options](https://docs.libretro.com/library/bsnes_hd_beta/#core-options): record actual core options when comparing images.
- [Current F-Zero adapter](../src/fzero_renderer.c), [presentation host](../src/sdl_main.c), and [multiplayer compositor](../src/fzero_versus_video.c).
- Historical [CPU cost assessment](HD_MODE7_EFFICIENCY_ASSESSMENT.md) and [performance record](HD_MODE7_PERFORMANCE.md); these do not describe every current path or certify current visual quality.
