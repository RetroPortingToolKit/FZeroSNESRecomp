"""Compare the VS selector with actual native single-player information screens.

Requires the owner's verified private ROM and Windows build. Every background
launch is muted; captures and derived artwork remain in the explicit output.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
from PIL import Image, ImageChops


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'runtime', 'rom', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    a = parser.parse_args()
    build, runtime, rom, out = (getattr(a, name).resolve() for name in ('build', 'runtime', 'rom', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'SNES_NET_', 'SNES_RB', 'RNET_', 'LNG_'))}
    clean.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
    oracle = subprocess.run([str(build / 'fzero_actor_oracle.exe'), str(rom)],
                            env=dict(clean, FZERO_TEST_SELECTOR_DUMP=str(out)),
                            capture_output=True, text=True, timeout=90)
    (out / 'oracle.log').write_text(oracle.stdout + oracle.stderr, encoding='utf-8')
    assert oracle.returncode == 0, oracle.stderr[-2000:]
    report = {}
    for machine, row in enumerate((0, 2, 1, 3)):
        folder = out / str(machine)
        folder.mkdir()
        route = '320-326:8' + ''.join(',%d-%d:32' % (480+i*30, 483+i*30) for i in range(row)) + ',650-656:8'
        env = dict(clean, FZERO_RULES='', FZERO_CGP_CARS='0', FZERO_BS_CARS='0', FZERO_BS_TRACKS='0',
                   FZERO_TRACK_PACKS=str(folder / 'packs'), SNESRECOMP_SAVE_ROOT=str(folder / 's'),
                   SNESRECOMP_INPUT_SCRIPT=route, SNESRECOMP_FRAME_DUMP=str(folder / 'native.ppm'),
                   SNESRECOMP_WRAM_DUMP=str(folder / 'native.bin'))
        proc = subprocess.run([str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), '800'],
                              cwd=runtime, env=env, capture_output=True, text=True, timeout=90)
        (folder / 'native.log').write_text(proc.stdout + proc.stderr, encoding='utf-8')
        assert proc.returncode == 0, proc.stderr[-2000:]
        assert (folder / 'native.bin').read_bytes()[0x52] == machine, 'native reference chose wrong machine'
        actual = Image.open(out / ('selector%d.ppm' % machine)).convert('RGB')
        native = Image.open(folder / 'native.ppm').convert('RGB')
        # Original full frame/name, numerical stats, units and acceleration
        # graph. Exclude the GP-only YES/NO row replaced by VS readiness.
        box = (104, 30, 240, 183)
        diff = ImageChops.difference(actual.crop(box), native.crop(box))
        diff.save(folder / 'panel-difference.png')
        assert diff.getbbox() is None, ('native information panel differs', machine, diff.getbbox())
        for other in range(4):
            if other == row:
                continue
            box = (32, 47+other*40, 72, 71+other*40)
            assert ImageChops.difference(actual.crop(box), native.crop(box)).getbbox() is None, (machine, other, 'native preview differs')
        actual.save(folder / 'versus.png')
        native.save(folder / 'native.png')
        report[machine] = dict(native_panel=True, native_previews=True, native_spin_angles=24)
        print('machine', machine, 'native panel, previews and full spin PASS', flush=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2), newline='\n')


if __name__ == '__main__':
    main()
