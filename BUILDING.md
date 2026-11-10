# Building F-Zero Forever

Back to the [README](README.md). Playing and settings are covered in the [player guide](PLAYER_GUIDE.md).

## Build from source

Clone with submodules:

```bash
git clone --recurse-submodules git@github.com:mstan/FZeroSNESRecomp.git
cd FZeroSNESRecomp
```

Put your own USA ROM at `fzero.sfc`, then generate and build:

```bash
bash tools/regen.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Local ROMs, generated files, saves, captures, and builds are ignored by git.

Run the ROM-free regression suites with `ctest --test-dir build --output-on-failure`.
With your local ROM and the upstream patch ZIP, `tests/test_msu_integration.py`
checks stock/Deluxe playback using generated test tones, missing-track fallback
and invalid-patch rejection. On Windows, `tests/test_desktop_integration.py`
additionally checks real launcher persistence, imported shaders, controller
overlays and save/load. Both scripts accept `--help` and keep their test data
under ignored `captures/` directories; neither bundles music or a shader.

`python tests/test_rom_persistence.py --source build` checks ROM selection
through the actual Windows file dialog, then Play/close and repeated restarts.
It also checks cancellation, an invalid pick, and selecting a moved ROM. It
requires an interactive desktop and never seeds the ROM cache itself.

`python tests/test_launcher_options.py --source build --rom PATH
--output captures/launcher-options` exercises the real Skip Launcher control,
relaunch/recovery, and typed HD resolution persistence. On Linux, prefix it
with `xvfb-run -a`. `--appimage-layout` also checks settings beside an AppImage
instead of inside its executable directory, using the AppImage environment.

On Linux, `xvfb-run -a python3 tests/test_appimage_rom_persistence.py
--appimage PATH --output captures/rom-persistence-linux` exercises the packaged
AppImage through the real zenity picker, then Play/quit and repeated relaunches.
It requires `zenity` and `xdotool`; use a fresh output directory for each run.

