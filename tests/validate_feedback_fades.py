"""Private-ROM regression for title/Records fades and native league panels.

Check the final presented image, retain native captures as independent evidence,
and ensure the car information card survives before the league menu opens.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PIXELS = 224 * 1120 + 65536
RAM = PIXELS + 256 * 224 * 4

def rgb(image):
    data=image.tobytes()
    return zip(data[::3],data[1::3],data[2::3])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'stock', 'packs', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--only', choices=('stock', 'gp', 'practice', 'records'))
    a = parser.parse_args()
    build, stock, packs, out = (getattr(a, k).resolve() for k in ('build', 'stock', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    shutil.copytree(packs / 'astra-front', out / 'catalog/astra-front',
                    ignore=shutil.ignore_patterns('music', '.cache'))
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'RNET_'))}
    for name in ((a.only,) if a.only else ('stock', 'gp', 'practice', 'records')):
        folder = out / name
        folder.mkdir()
        (folder / 'frames').mkdir()
        shutil.copytree(ROOT / 'assets', folder / 'assets')
        records = name == 'records'
        frames = list(range(400, 521, 2)) if records else list(range(600, 1001 if name == 'practice' else 821, 4))
        script = '300-306:32,360-366:32,420-426:8' if records else (
            ('300-306:32,' if name == 'practice' else '') + '320-326:8,600-606:8,730-736:8' +
            (',850-856:8,970-976:8' if name == 'practice' else ''))
        env = dict(clean, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                   FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='7', FZERO_CGP_REBALANCE='15', FZERO_BS_CARS='0',
                   FZERO_ALWAYS_RECORDS='1', FZERO_PACK_LOADER='1', FZERO_PACKS_DIR=str(out / 'catalog'),
                   FZERO_TRACK_PACKS='settings', SNESRECOMP_SAVE_ROOT='s', FZERO_CUP='astra-front/astra',
                   FZERO_VIEWPORT_SCRIPT='0:16:9', FZERO_TEST_VS_MENU='1', SNESRECOMP_INPUT_SCRIPT=script,
                   FZERO_CAPTURE_FRAMES=','.join(str(f + 1) for f in frames), FZERO_CAPTURE_PREFIX=str(folder / 'native'),
                   FZERO_TEST_FRAME_DIR=str(folder / 'frames'),
                   FZERO_TEST_FRAME_RANGE=f'{frames[0]}:{frames[-1]}:{frames[1]-frames[0]}')
        if name=='stock':
            env.update(FZERO_CGP_CARS='0',FZERO_CGP_REBALANCE='0',FZERO_RULES='',FZERO_BS_TRACKS='0')
        with (folder / 'run.log').open('w') as log:
            result = subprocess.run([str(build / 'FZeroSNESRecompHeadless.exe'), str(stock), str(frames[-1] + 2)],
                                    cwd=folder, env=env, stdout=log, stderr=log, timeout=240)
        assert result.returncode == 0, (name, result.returncode)

        def evidence(frame):
            data = (folder / f'native-{frame+1:06}.bin').read_bytes()
            native = Image.frombytes('RGB', (256, 224), data[PIXELS:RAM], 'raw', 'BGRX')
            actual = Image.open(folder / f'frames/{frame:06}.ppm').convert('RGB')
            extra = (actual.width - 256) // 2
            return native, actual.crop((extra, 0, extra + 256, 224)), data[RAM:RAM + 0x20000]

        if records:
            for frame in (424, 430, 436, 442, 448):
                native, actual, ram = evidence(frame)
                assert native.crop((0, 0, 256, 160)).tobytes() == actual.crop((0, 0, 256, 160)).tobytes(), \
                    (name, frame, 'Records appeared over the outgoing title')
                ink = sum(g > r and g > b for r, g, b in rgb(actual.crop((84, 164, 144, 180))))
                assert ink > 8, (name, frame, 'VS row vanished before the title faded')
        else:
            for frame in (652, 684, 716):
                native, actual, _ = evidence(frame)
                region = (110, 52, 234, 180)
                assert actual.crop(region).tobytes() == native.crop(region).tobytes(), \
                    (name, frame, 'league overlay covered the car information card')
            native_final, final, _ = evidence(940 if name == 'practice' else 820)
            region = (128, 119, 232, 127)  # Imported Astra replaces the donor fourth league.
            reference = final.crop(region)
            assert reference.tobytes() != native_final.crop(region).tobytes(), (name, 'imported league was never drawn')
            if name=='stock':
                print('PASS stock engine: imported league overlay and native font',flush=True)
                continue
            checked = 0
            for frame in frames:
                if frame < 744:
                    continue
                native, actual, ram = evidence(frame)
                # LEAGUE begins at native x=120; RIVAL CAR starts farther
                # right. GP uses cyan, Practice uses the small yellow font.
                heading = sum((g>r+16 and b>r+16) or (r>b+16 and g>b+16)
                              for r,g,b in rgb(native.crop((120,55,128,72))))
                if heading < 4 or not native.crop(region).getbbox():
                    continue
                level = 0 if ram[0x5d] & 128 else ram[0x5d] & 15
                expected = reference.point([n * level // 15 for n in range(256)] * 3)
                assert actual.crop(region).tobytes() == expected.tobytes(), \
                    (name, frame, 'donor league appeared during a panel transition')
                checked += 1
            assert checked >= 5, (name, 'league transition was not exercised')
        print(f'PASS {name}: native card, imported labels, title/Records transition', flush=True)


if __name__ == '__main__':
    main()
