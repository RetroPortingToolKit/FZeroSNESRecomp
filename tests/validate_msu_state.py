"""Muted private-ROM test of real MSU save/load callbacks and rewind.

Uses nonconstant synthetic recordings; no copyrighted music is required.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'packs', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    a = p.parse_args()
    build, rom, packs, out = (getattr(a, k).resolve() for k in ('build', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    shutil.copytree(root / 'assets', out / 'assets')
    catalog = out / 'packs'
    shutil.copytree(packs, catalog, ignore=shutil.ignore_patterns('*.pcm', '*.msu'))
    media = out / 'music'
    media.mkdir()
    # Different values at every frame and a nonzero loop point reveal both
    # lost phase/read-ahead and wrong track/loop restoration.
    def recording(seed):
        data = bytearray(b'MSU1' + struct.pack('<I', 1197))
        for i in range(220_500):
            data.extend(struct.pack('<hh', ((i*37+seed)%30001)-15000, ((i*61+seed)%29003)-14500))
        return data
    pcm = recording(11)
    for n in list(range(1, 8)) + list(range(10, 65)):
        (media / f'cgp-{n}.pcm').write_bytes(pcm if n != 5 else recording(123))
    astra = catalog / 'astra-front'
    course = json.loads((astra / 'courses.json').read_text())['courses'][0]
    (astra / 'music').mkdir(exist_ok=True)
    (astra / 'music' / (Path(course['source']).stem + '.pcm')).write_bytes(recording(798))
    env = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'SNES_RB', 'RNET_'))}
    env.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', FZERO_DELUXE_DATA='embedded',
               FZERO_CGP_CARS='0', FZERO_BS_CARS='0', FZERO_BS_TRACKS='0', FZERO_RULES='',
               FZERO_PACK_LOADER='1', FZERO_PACKS_DIR=str(catalog), SNESRECOMP_MSU1=str(media / 'cgp'))
    results = []
    for name, course in [('versus-stock', 0), ('versus-course-file', 15), ('single-player', None)]:
        folder = out / name
        folder.mkdir()
        settings = dict(env, FZERO_TRACK_PACKS=str(folder / 'settings'),
                        SNESRECOMP_SAVE_ROOT=name+'/saves',
                        FZERO_TEST_MSU_STATE=str(folder / 'music.state'))
        cmd = [str(build / ('FZeroSNESRecompHeadless.exe' if os.name == 'nt' else 'FZeroSNESRecompHeadless')), str(rom)]
        if course is not None:
            cmd += ['1100', '--versus', str(course), '9', '3']
        else:
            settings.update(FZERO_RULES='cgp-msu', SNESRECOMP_INPUT_SCRIPT=
                            '320-326:8,800-806:8,920-926:8,1090-1096:8,1150-1156:8,1520-2199:1')
            cmd += ['2200']
        proc = subprocess.run(cmd, cwd=out, env=settings, capture_output=True, text=True, timeout=360)
        log = proc.stdout + proc.stderr
        (folder / 'run.log').write_text(log, encoding='utf-8')
        assert proc.returncode == 0 and 'msu-state: PASS' in log, (name, proc.returncode, log[-4000:])
        result = dict(name=name, passed=True, checks=[s for s in log.splitlines() if 'msu-state:' in s])
        print(json.dumps(result), flush=True)
        results.append(result)
    (out / 'report.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
