# Performance diagnostics

Open the launcher, choose **Mods > Diagnostics**, enable it, and press Play.
Play through the slowdown for a few seconds, then quit normally. Attach the
newest `diagnostics/performance-*.jsonl` file to your report and describe where
the slowdown happened. Include the approximate time if possible.

The folder is beside the executable on Windows or an unpacked Linux build,
and beside the AppImage on Linux. Each session gets its own timestamped file.
Diagnostics is **off by default**. Disable it in Mods when finished; that
stops creating new reports. Existing reports remain available. If Skip Launcher
is enabled, start the executable with `--launcher` to reach the toggle.

Logs stay local and are never uploaded automatically. They include build and
dependency versions, CPU/OS/RAM, graphics backend and available adapter/driver
information, shader preset name, active settings and periodic timing summaries.
Controller samples include connection status, instance ID, raw left-stick
coordinates and buttons; device add/remove/remap events are recorded too.
For an input lockup, keep Diagnostics enabled until after it occurs so these
samples can be compared with the input replay.
They do not include ROM contents, saves, personal file paths, or player names.
An unwritable diagnostics folder does not stop the game.

## One-click Mode 7 profiler

The one-off Windows profiling bundle includes **Profile Mode 7.cmd**. Extract
the whole bundle, run that file, keep your usual graphics settings, and play
the slow course for at least 30 seconds. Quit normally. The script creates a
small `profile-results/mode7-profile-*.zip` and selects it in Explorer. Send
that report back with a description of where the slowdown occurred.

The report includes the raw timing log, a readable summary, and selected CPU,
memory and graphics-driver fields. It excludes ROMs, recordings, saves and
configuration files. Nothing is uploaded. The original serial profiler ZIP
is retained; the worker-pool test ZIP has its own build label and profiler.
For a comparison with the same executable, set
`SNESRECOMP_RENDER_WORKERS=1` before launching it. The worker ZIP's README
includes the two Command Prompt commands needed to do this.

To compare HD sampling costs, make separate recordings with HD off, 2x and
4x, keeping the same course, aspect, shader and Presentation FPS. The summary
separates whole-session measurements from intervals ending in active racing,
groups different graphics configurations, and weights stage means by their
call counts. Two-second intervals can include scene transitions; the raw log
retains the timing and scene information for closer analysis.

The launcher invokes `FZeroSNESRecomp --profile-mode7 --launcher`. The flag
enables diagnostics for that session without saving the Diagnostics mod as on.
It also works with an explicit ROM path for automated validation. Normal
launches continue to honor the existing default-off Diagnostics setting.

Packaging uses `tools/stage_mode7_profiler.py` with a reviewed, music-free
player bundle and the profiling executable. It refuses private files and
existing output directories. The scripts require only Windows PowerShell;
players do not need Python or development tools.

## Reading a report

- `session` identifies the build, backend and allocated texture scale. OpenGL
  includes the actual renderer/vendor/version and swap interval. For SDL/D3D,
  Windows also lists attached display adapters; these are not proof of which
  adapter rendered a frame. `vsync=-99` means the query was unavailable.
- `settings` records the initial settings. `sample` records interval deltas
  about every two seconds. `final` flushes the last partial interval on exit.
  Settings in a sample describe its end, so resizing or changing aspect/FPS
  during that interval can span more than one configuration.
- Compare `requested_scale`, `allocated_scale`, and `effective_scale`. The
  latter describes the framebuffer actually submitted, including fallback to
  native resolution before a valid HD frame exists. Source and output sizes
  distinguish HD sampling, widescreen width, and window/display resolution.
- `simulation_fps` should be near 60.099; `presentation_fps` is independent.
  The sample includes target/display rates, missed presentation deadlines,
  and frame interval p95/p99/max. Percentiles are upper bounds in 0.25 ms
  histogram bins; intervals above 128 ms use the observed maximum.
- `render_workers` is the pool's participant capacity, including the main
  thread. Native rendering and flat menus remain serial even after a pool
  has been created. The composition stage measures main-thread elapsed time
  through the completion barrier, including work performed by workers.
- `stages` contains calls, total, mean and maximum **main-thread wall time**:
  simulation, native PPU capture, presentation composition (including HD Mode
  7), texture upload, draw submission, present/swap, pacing wait, and pause.
  Means use the stage's call count, not the simulation frame count.
- `present` includes driver/vsync waiting; `draw_submit` is CPU submission
  time, not a GPU execution measurement. No GPU fences are inserted. A large
  upload/present time can reflect earlier asynchronous GPU work. Overall CPU
  or GPU utilization alone cannot identify the limiting stage.
- `unattributed_ms` covers input, audio-lock interactions outside measured
  stages, bookkeeping, menus, logging and other work. Simulation time already
  includes any waits inside simulation. Pause, menu and state-action events
  help identify discontinuities; a long menu can span a sample interval.

Instrumentation adds a small amount of work while enabled. With the option
off there are no diagnostic files, allocations or performance-counter reads
from the logger. The existing crash-report system is independent.
