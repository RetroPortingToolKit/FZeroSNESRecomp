# High-resolution Mode 7

HD Mode 7 improves the road's perspective and detail using the original course
graphics. Cars, HUD and text keep their original pixel artwork. Game speed and
guest state do not change.

In **Mods → High-resolution Mode 7**:

- **HD detail:** 2x, **4x (recommended)** or 8x. Higher values sample the road
  more finely; a small window may hide the difference.
- **Edge smoothing:** **Off** or **2 samples per axis**. The latter averages
  four samples per road pixel to reduce jagged edges, at additional cost.

Window scale changes the size of the displayed picture. Display filtering and
shader presets affect the composed picture, including native artwork. These
settings and presentation FPS remain separate from Mode 7 detail.

Fresh installations use adaptive **Fit to screen**, 4x detail, smoothing off
and automatic presentation FPS. Updates preserve explicit saved settings,
including older custom detail and smoothing values. Such values appear as
**Current custom (…)** until the player chooses a preset.

The corresponding `fzero-video.ini` settings are:

```ini
Mode7Enabled=1
Mode7Scale=4
Mode7Supersample=1
```

Single-player and local 2P/3P/4P use the shared GPU renderer. The portable
three/four-player fallback limits detail to 2x and displays the reduction;
Mods also retains the last race's effective-quality status. Unsupported scenes
keep native rendering. If HD cannot render an applicable scene, the game
reports that it is using native rendering.

The CPU publishes native-sized raster jobs and the GPU samples resident course
data. Nearest presentation computes the samples visible at the chosen window
size, with the same displayed result as a full HD raster followed by nearest
scaling. Linear filtering and shader presets can require larger bounded GPU
targets; hardware limits can reduce effective detail. Native snapshots and
debug frames remain native-sized.

The [quality burndown](https://github.com/RetroPortingToolKit/FZeroSNESRecomp/blob/main/docs/HD_MODE7_BURNDOWN.md) records the independent bsnes-hd
comparisons, owner observations, multiplayer checks and measured runtime costs.
Those results cover the tested scenes and machines; higher settings do not
guarantee a particular frame rate on every host. Historical CPU measurements
remain in [HD_MODE7_PERFORMANCE.md](https://github.com/RetroPortingToolKit/FZeroSNESRecomp/blob/main/docs/HD_MODE7_PERFORMANCE.md).
