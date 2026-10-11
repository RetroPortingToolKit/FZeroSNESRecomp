"""Private-ROM regression: native title demo after a CGP vehicle selection.

Seeds only the retained car selection before the real automatic demo starts.
Checks the replay against an independent, unmodified BS vehicle configuration,
then enters a normal race to verify that the player's selection still works.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess
import re
from PIL import Image
from validate_practice_catalog import route

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'stock', 'out'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    build, stock, out = (getattr(args, k).resolve() for k in ('build', 'stock', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    clean = {k:v for k,v in os.environ.items() if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    results = {}

    def case(name, identity, bs):
        p = out/name
        p.mkdir()
        shutil.copytree(build/'assets', p/'assets')
        selection = p/'retained-selection.txt'
        selection.write_text(f'1000 1000 14dff {identity:02x}\n')
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_BS_CARS=str(bs),
                   FZERO_CGP_CARS='0' if bs else '7', FZERO_BS_TRACKS='0',
                   FZERO_CGP_REBALANCE='0', FZERO_RULES='', FZERO_PACK_LOADER='1',
                   FZERO_PACKS_DIR=str(build/'mods/packs'), FZERO_TRACK_PACKS='packs',
                   SNESRECOMP_SAVE_ROOT='s')

        def run(label, frames, **settings):
            with (p/f'{label}.log').open('w') as log:
                result = subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'), str(stock), str(frames)],
                    cwd=p, env=dict(env, **settings, SNESRECOMP_WRAM_DUMP=str(p/f'{label}.ram'),
                                   SNESRECOMP_FRAME_DUMP=str(p/f'{label}.ppm')),
                    stdout=log, stderr=log, timeout=150)
            text = (p/f'{label}.log').read_text(errors='replace')
            assert result.returncode == 0 and 'fzero_native: PASS' in text, (name, text[-2000:])
            return (p/f'{label}.ram').read_bytes()

        ram = run('loop', 3600, FZERO_TEST_WRAM_SCRIPT=str(selection),
                  FZERO_TEST_WRAM_TRACE=str(p/'trace.bin'),
                  FZERO_STATE_SAVE=str(p/'demo.sav'), FZERO_TEST_SAVE_FRAME='2099')
        assert ram[0x54] == 0 and ram[0x52] == 4
        ram = run('replay', 1, FZERO_STATE_LOAD=str(p/'demo.sav'))
        assert ram[0x54:0x57] == bytes((2, 3, 0)) and ram[0x52] == 4 and ram[0x14dff] == identity
        image = Image.open(p/'replay.ppm')
        image.resize((768,672), Image.Resampling.NEAREST).save(p/'replay.png')
        results[name] = ((p/'trace.bin').read_bytes(), image.tobytes())
        if identity == 5:
            # Return naturally to the title, then choose Dragon Bird through
            # the real Practice menu. No vehicle override.
            inputs = route(5,255)+',1200-1206:8,1300-1306:8,1700-1706:8,1800-2399:1'
            inputs = re.sub(r'(?<!\d)\d+(?=[-:])', lambda m:str(int(m[0])+3600), inputs)
            run('race', 6000, FZERO_TEST_WRAM_SCRIPT=str(selection), FZERO_CUP='cgp/cgp-1',
                SNESRECOMP_INPUT_SCRIPT=inputs)
            ram = (p/'race.ram').read_bytes()
            assert ram[0x54:0x56] == bytes((2, 3)) and ram[0x52] == 1 and ram[0x14dff] == 5, ram[0x52:0x5c].hex()
        print('PASS title demo, retained identity and snapshot: '+name, flush=True)

    cases = [('bs-control', 0, 1), ('dragon-bird', 5, 0), ('black-bull', 9, 0), ('red-gazelle', 11, 0)]
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda c: case(*c), cases))
    control, pixels = results['bs-control']
    for name, (trace, rendered) in results.items():
        assert rendered == pixels, (name, 'demo artwork differs from native BS control')
        assert len(trace) == len(control) == 3600*8192
        for frame in range(1459,3425):
            a, b = trace[frame*8192:(frame+1)*8192], control[frame*8192:(frame+1)*8192]
            assert a[0x54:0x57] == b[0x54:0x57] == bytes((2,3,0)), (name, frame, 'demo ended early')
            assert a[0xa0:0xae] == b[0xa0:0xae] and a[0xb20:0xb22] == b[0xb20:0xb22], (name,frame,'replay physics differ')
    (out/'validation.json').write_text(json.dumps(dict(cases=[c[0] for c in cases],
        demo_matches_native_bs_artwork=True, replay_frames_matched=1966,
        normal_race_after_demo=True, snapshot_replay=True), indent=2)+'\n')


if __name__ == '__main__': main()
