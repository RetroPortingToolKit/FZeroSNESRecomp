"""Muted desktop qualification through native VS entry and two SDL devices.

Exercises the live Restart/Change/Main Menu actions, continued simulation,
resizing and the combined native/HD presentation. Uses a fresh installation.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'runtime', 'rom', 'packs', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--fps',type=int,default=60)
    p.add_argument('--native',action='store_true',help='Disable HD Mode 7 for this windowed run')
    a = p.parse_args()
    build, runtime, rom, packs, out = (getattr(a, k).resolve() for k in ('build', 'runtime', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    stage = out / 'install'
    stage.mkdir()
    shutil.copy2(build / 'F-Zero Forever.exe', stage)
    for dll in runtime.glob('*.dll'):
        shutil.copy2(dll, stage)
    shutil.copytree(ROOT / 'assets', stage / 'assets')
    shutil.copytree(packs / 'astra-front', out / 'catalog/astra-front', ignore=shutil.ignore_patterns('music', '.cache'))
    config = (runtime / 'config.ini').read_text()
    config = config.replace('SkipLauncher = 0', 'SkipLauncher = 1')
    config = config.replace('SkipLauncher=0', 'SkipLauncher=1')
    (stage / 'config.ini').write_text(config)
    shutil.copy2(runtime / 'keybinds.ini', stage)
    (stage / 'fzero-video.ini').write_text(
        f'[FZeroVideo]\nEnhancedRenderer=1\nAspect=Fit\nPresentationEnabled=1\nPresentationFPS={a.fps}\n'
        f'HDMode7={int(not a.native)}\nHDMode7Scale=2\nBSVehicles=0\nBSTracks=0\nCGPCars=0\nCGPStockRebalance=0\n'
        'LocalTwoPlayer=1\nAlwaysShowRecords=1\nCrashReplay=1\nDiagnostics=1\n')
    shots = out / 'images'
    shots.mkdir()
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'RNET_'))}
    env = dict(clean, SDL_AUDIODRIVER='dummy', SNESRECOMP_MSU1='off',
               FZERO_TEST_TRAFFIC_VISIBLE='1',
               FZERO_TEST_LOCAL_UI='1', FZERO_TEST_LOCAL_UI_HIDDEN='1', FZERO_TEST_LOCAL_UI_LIVE='1',
               FZERO_TEST_LOCAL_UI_RESIZE='1', FZERO_TEST_LOCAL_UI_DRIVE='1', FZERO_TEST_LOCAL_UI_DUMP=str(shots),
               FZERO_PACKS_DIR=str(out / 'catalog'), FZERO_TRACK_PACKS=str(out / 'settings'), SNESRECOMP_SAVE_ROOT='s',
               SNESRECOMP_INPUT_SCRIPT='300-302:32,330-332:32,360-362:32,390-392:8',
               SNESRECOMP_AUTOCLOSE_FRAMES='5000')
    with (out / 'game.log').open('w') as log:
        result = subprocess.run([str(stage / 'F-Zero Forever.exe'), str(rom)], cwd=stage,
                                env=env, stdout=log, stderr=log, timeout=240)
    text = (out / 'game.log').read_text(errors='replace')
    assert result.returncode == 0 and 'local-ui: PASS' in text, text[-4000:]
    assert (shots / 'live-menu-frame.bmp').exists(), 'Live menu was not displayed'
    assert '[traffic-video] pane=0 ' in text and '[traffic-video] pane=1 ' in text, 'Bumper pixels were not visible in both gameplay views'
    assert 'traffic-encounter: both human impacts PASS' in text, 'Prepared bumper contacts did not reach both humans'
    print('PASS live menu: two SDL devices, prepared bumper impacts/views, restart/change/exit, resized HD/native presentation', flush=True)


if __name__ == '__main__':
    main()
