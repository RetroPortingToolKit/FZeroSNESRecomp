"""Windows desktop smoke test with the owner's ROM, shader and MSU test pack.

Run test_msu_integration.py first to create a synthetic pack, then:
  python tests/test_desktop_integration.py --shader PATH/TO/crt-geom.glslp

Opens bounded real OpenGL windows, never modifies the user's launcher config,
and leaves its isolated installation/logs in ignored captures/desktop-*.
"""
import argparse
import configparser
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", default="build-release-162")
    parser.add_argument("--shader", type=Path, required=True)
    parser.add_argument("--state", type=Path, help="Optional private BS Deluxe/MSU F1 game-over reproducer")
    parser.add_argument("--msu-pack", type=Path, default=ROOT / "captures/msu-validation/synthetic pack")
    args = parser.parse_args()
    assert os.name == "nt", "This staging test currently targets Windows"
    shader = args.shader.resolve()
    assert shader.is_file(), shader
    pack = args.msu_pack.resolve()
    assert (pack / "f-zero_msu1.ips").is_file(), pack
    # Exercise Browse -> native file dialog -> Play/close -> relaunch. A
    # pre-written rom.cfg only tests loading and missed issue #8's lost pick.
    subprocess.run([sys.executable, str(ROOT / "tests/test_rom_persistence.py"),
                    "--source", str(ROOT / args.build)], check=True)
    stage = Path(tempfile.mkdtemp(prefix="desktop-", dir=ROOT / "captures"))
    build = ROOT / args.build
    shutil.copy2(build / "F-Zero Forever.exe", stage)
    for dll in build.glob("*.dll"):
        shutil.copy2(dll, stage)
    shutil.copytree(build / "assets", stage / "assets")
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("FZERO_", "SNESRECOMP_", "SDL_", "LNG_"))}
    env.update(SDL_AUDIODRIVER="dummy", SNESRECOMP_SAVE_ROOT=str(stage / "saves"))
    exe = str(stage / "F-Zero Forever.exe")
    rom = "fzero.sfc"  # resolved against the caller's cwd, NOT the install
    shader_relative = os.path.relpath(shader, stage)
    pack_relative = os.path.relpath(pack, stage)
    config = stage / "config.ini"
    config.write_text(
        f"[Graphics]\nWindowScale=3\nFullscreen=0\nShader={shader_relative}\n"
        f"[Sound]\nVolume=64\nMsu1Enabled=1\nMsu1Dir={pack_relative}\n"
        "[Rewind]\nEnabled=1\nDepth=50\nInterval=4\n", encoding="utf-8")
    (stage / "fzero-video.ini").write_text(
        "[FZeroVideo]\nEnhancedRenderer=1\nAspect=21:9\n"
        "PresentationEnabled=1\nPresentationFPS=144\nBSDeluxe=1\n")

    def run(name, argv, extra):
        with (stage / f"{name}.log").open("w") as log:
            result = subprocess.run([exe, *argv], cwd=ROOT, env=dict(env, **extra),
                                    stdout=log, stderr=log, timeout=90)
        text = (stage / f"{name}.log").read_text()
        assert result.returncode == 0, (name, result.returncode, text[-5000:])
        print(f"{name}: passed", flush=True)
        return text

    # A fresh install must start unfiltered. The private playtest can retain
    # an imported shader on disk without silently enabling it.
    imported_config = config.read_text(encoding="utf-8")
    config.write_text("", encoding="utf-8")
    run("default-shader-off", [], {"LNG_SCRIPT": "wait:2;view:settings;wait:2;quit"})
    defaults = configparser.ConfigParser()
    defaults.read(config, encoding="utf-8-sig")
    assert defaults["Graphics"]["Shader"] == "", defaults["Graphics"]["Shader"]
    config.write_text(imported_config, encoding="utf-8")

    # Quit the real launcher twice: imported paths and sound/rewind settings
    # must survive both its load and its save-on-quit path (issue #2).
    for attempt in range(2):
        run(f"launcher-{attempt}", [], {"LNG_SCRIPT": "wait:2;view:settings;wait:2;quit"})
        saved = configparser.ConfigParser()
        saved.read(config, encoding="utf-8-sig")
        for section, key, value in (
            ("Graphics", "Shader", shader_relative), ("Sound", "Volume", "64"),
            ("Sound", "Msu1Enabled", "1"), ("Sound", "Msu1Dir", pack_relative),
            ("Rewind", "Enabled", "1"), ("Rewind", "Depth", "50"),
            ("Rewind", "Interval", "4"),
        ):
            assert saved[section][key] == value, (section, key, saved[section][key])

    text = run("crt-msu-deluxe", [rom], {
        "SNESRECOMP_AUTOCLOSE_FRAMES": "300", "FZERO_OVERLAY_SELFTEST": "80",
        "FZERO_STATE_SAVE_AT": "180:1", "FZERO_STATE_LOAD_AT": "220:1",
    })
    assert "Unable to load shader" not in text and "compile failed" not in text
    assert "Conn/Cubear v11 active" in text
    assert "ok: browser opened from pad and keyboard" in text
    assert "load slot 2: ok" in text
    assert (stage / "saves/bs-deluxe/msu1/fzero-bs-deluxe-msu11.sav").is_file()

    if args.state:
        shutil.copy2(args.state, stage / "saves/bs-deluxe/msu1/fzero-bs-deluxe-msu10.sav")
        config.write_text(imported_config.replace(f"Shader={shader_relative}", "Shader="), encoding="utf-8")
        text = run("owner-gameover-unfiltered", [rom], {
            "SNESRECOMP_AUTOCLOSE_FRAMES": "120", "FZERO_STATE_LOAD_AT": "2:0",
        })
        assert "load slot 1: ok" in text, text[-3000:]

    text = run("bad-shader-msu-off", [rom], {
        "SNESRECOMP_AUTOCLOSE_FRAMES": "10", "SNESRECOMP_MSU1": "off",
        "FZERO_SHADER": str(stage / "missing.glslp"),
    })
    assert "using unfiltered output" in text
    assert "Conn/Cubear v11 active" not in text
    print(f"Desktop validation logs: {stage}")


if __name__ == "__main__":
    main()
