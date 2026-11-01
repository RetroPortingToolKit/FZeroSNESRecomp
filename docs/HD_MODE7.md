# High-resolution Mode 7

F-Zero uses snesrecomp's shared GPU Mode 7 renderer by default at **8x**.
**Mods → High-resolution Mode 7** offers integer scales **1x through 64x**.
It works with adaptive widescreen, local split screen and presentation FPS.
Cars, HUD and menus retain their pixel artwork; rotating/perspective track
sampling gains detail. Game speed and guest state do not change.

The settings in `fzero-video.ini` are:

```ini
Mode7Enabled=1
Mode7Scale=8
Mode7Smoothing=0
```

These replace the retired CPU-HD control and its `HDMode7`/`HDMode7Scale` keys.
Existing installations adopt the new 8x default once; unrelated preferences
remain intact. Subsequent changes to the new settings persist normally.

The CPU publishes immutable native-sized raster jobs. The GPU samples resident
source memory. Nearest presentation computes only visible samples of the chosen
grid, giving the same displayed image as a full raster followed by nearest
sampling. Selecting 64x does not allocate a giant CPU image or upload one every
frame. Linear filtering and shader presets can require larger bounded GPU
targets; hardware limits may reduce their effective resolution.

Local three/four-player views use the portable CPU renderer at up to2x; this
cap preserves responsiveness while the shared GPU packet supports full-width
cameras only. Two-player and single-player GPU scales still reach64x.

**Track smoothing** is a separate, optional Off/On control in this feature.
It averages four samples across each presented track pixel while retaining
point-sampled sprites, HUD, menus and foreground overlays. It rejects samples
across camera discontinuities and protected foreground pixels. It defaults off;
1x, unsupported devices and local three/four-player CPU views remain native or
use their existing fallback. GPU memory stays bounded by the presentation size;
insufficient texture/budget capacity falls back to unsmoothed output.

The 2026-11-01 trial compared 17 moving Mute City frames at 768x576 against
black-box bsnes 115 and bsnes-hd beta 10.6 cores, with their documented HD and
supersampling controls. Core binaries, options, source capture identities,
lossless frames, clips and results are retained locally in
`captures/mode7-smoothing`. Emulator crop, one-frame timing and F-Zero-specific
course/color policies differ, so these are visual comparisons, not pixel-parity
claims. See the [bsnes-hd settings](https://github.com/DerKoun/bsnes-hd#settings).

On the RTX 3080 Ti, median GPU draw time was 0.18ms without smoothing and
0.40ms with it at both 8x and64x. Four-sample smoothing reduced track sampling
RMSE by46-49% against an offline 16-sample area-average reference sharing our
transforms; protected foreground pixels were unchanged. Residual changes over
time fell42-48% against that reference. This is evidence about sampling error,
not an artistic quality score or a whole-game FPS claim. Distant scenery still
aliases; the emulator controls likewise trade sharpness for averaging. Higher
resolution alone does not provide supersampling anti-aliasing.

Unsupported scenes and devices retain native rendering. The old 2..10x CPU
sampler remains an internal developer/reference path (`FZERO_HD_GPU=0`), not
another launcher mod. It is not the automatic fallback for the new default.

Native snapshots/debug frames remain native-sized. Rendering does not alter
simulation or add netplay features. See the shared engine's
`docs/GPU_HD_MODE7.md` for its capture/presentation contract and tests.
Historical CPU measurements are in [HD_MODE7_PERFORMANCE.md](HD_MODE7_PERFORMANCE.md).

The owner approved the interactive 32x/64x preview on 2026-10-30. That establishes
the tested machine's visual behavior and responsiveness, not a universal
no-slowdown guarantee on every host.
