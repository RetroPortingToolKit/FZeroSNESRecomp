# Recording and replaying a playtest

Recording is enabled by default in **Mods > Crash diagnostics and replay**, for
single-player, local VS and online VS. A crash is not required: reproduce the
problem, quit, then run **Collect diagnostics.cmd** beside the game. Send the
ZIP selected in Explorer, the approximate time and a description of the issue.
It includes the three newest sessions, the latest performance log if enabled,
the last exit report, and the shared transport log for online matches. Nothing
is uploaded automatically. Captures contain
game memory, saves and local paths; share them privately when appropriate.

F-Zero's custom SDL host installs snesrecomp's existing post-mortem and host
report writers, including Windows SEH/minidumps, fatal exits and SIGABRT.
This follows the MMX4 diagnostics approach: preserve fault context and replay
the actual guest input route. No shared engine or UI pin change is required.

The game records every guest frame before executing it. Every 1,800 frames,
or after a load/reset/rewind changes the timeline, it creates a checkpoint.
Up to 120 checkpoints and their inputs remain in the session, approximately
one hour during uninterrupted play; frequent timeline changes and online
rollback shorten the history. Separate launch folders are retained until
someone deletes them.

The same shared SNES background replay writer used by Mega Man X records
per-frame checks of full WRAM, canonical race/online state and VRAM. It hashes
immutable copies on its worker; a failed or overloaded writer stops optional
capture without stopping play. Checkpoints and the small input log are still
written at frame boundaries. Online records the actual execution order,
including rollback and a new checkpoint after restoration, rather than guessing
an input route from the final confirmed tick. Playback runs offline.

The fault handler only writes existing engine state and a cached F-Zero
summary. It never tries to take a new snapshot of a faulting machine. Inputs
are flushed before execution, so the input that triggered a mid-frame crash
remains available even after abrupt process termination. A hard termination
can leave checkpoints and inputs without a completed crash report.

## Replay

Keep the exact build used by the tester. The capture includes the game,
framework and UI revisions plus a source/configuration fingerprint; the tool
rejects mismatched build stamps, including changed local game code. Use
your own verified F-Zero USA ROM and the same installed packs/recordings.

```powershell
python tools/replay_diagnostics.py "path/to/diagnostics/replay-..." `
  --build build --rom "path/to/fzero.sfc" --out captures/replayed-crash
```

`--out` must be new. The tool stages a separate installation and save root,
restores captured settings and external course records, and runs the newest
available segment. It writes `replay.log`, `result.json` and final RAM/frame
artifacts. No tester save files are changed. Use `--segment N` to select an
earlier retained checkpoint or `--packs path/to/tester-packs` to supply the
same imported course/music collection.

Add `--desktop` to exercise the real renderer/audio host. Renderer, GPU,
driver, shader and relevant launcher choices are recorded in `host.json`.
Custom shader files need to be supplied separately; missing shaders are
reported instead of silently substituted. Headless replay does not reproduce
GPU drivers, SDL scheduling, audio callback races or the tester's hardware.
For those failures, inspect the minidump and crash report alongside the route.
MSU songs restart when a checkpoint loads; the shared chip does not save its
PCM cursor. Guest replay was checked with MSU enabled, but audio playback is
not a sample-exact recording.

Each frame is checked against the captured master clock, resume PC and first
8 KiB of guest WRAM before its input is applied. New format2 captures also
verify full WRAM, the complete shared race, online menu state and VRAM after
each recorded frame. Divergence, incomplete checks or malformed input
stop replay with an error. This detects guest-state mismatch; it is not a
whole-machine hash. Matching guest replay does not prove a host/hardware
crash will happen again. Checkpoints pair the ordinary game snapshot with the
framework's pointer-free execution extension to preserve clock residue that
ordinary save/load intentionally re-anchors. In-process rollback blobs are
never used as disk files.

## Files

- `session.json`: build identity, active-content signature, pack IDs and save namespace.
- `video.ini`, `loader.cfg`, `host.json`: effective game and host choices.
- `segments.jsonl`: completed checkpoint inventory.
- `segment-N.sav`, `.execution`, `.inputs`, `.frames`, `.records/`: checkpoint, exact
  execution clocks, input route, shared-writer checks and external records.
- `crash_report_*.json`, `*.dmp`: shared engine reports when fault handling runs.

The shared `last_run_report.json` beside the game describes the latest exit.
Timestamped crash reports inside their session folder survive subsequent runs.
Captures contain memory/save data and local paths; send them privately when
appropriate. Cartridge files and PCM recordings are never copied into a capture.

## Validation

```powershell
python tests/validate_crash_replay.py --build build `
  --rom "path/to/fzero.sfc" --out captures/crash-replay-check
```

This privately tests stock and CGP capture/replay, final WRAM equality,
three-checkpoint retention, deliberate divergence rejection, real Windows
SEH/minidump and fatal-error drills, preservation across a later launch, and
the isolated support-tool workflow. `SNESRECOMP_CRASH_TEST=seh|die` is an
explicit support drill, never a saved setting or default behavior.
