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
