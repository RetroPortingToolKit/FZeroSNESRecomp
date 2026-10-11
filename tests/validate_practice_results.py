"""Private-ROM regression: real Practice finish to BEST 5/BEST LAP car icons.

Seeds final-lap timing and crosses the real finish line; it does not drive a
whole race. The native results and best-record transitions still execute.
Checks each icon against the author's independent donor artwork and palette.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image

from validate_practice_catalog import route
from validate_vehicles import NAMES, GROUPS, SLOTS
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from inspect_bs_deluxe import apply_ips


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'stock', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    build, stock, out = (getattr(args, k).resolve() for k in ('build', 'stock', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    original = stock.read_bytes()
    art = [original] + [apply_ips(original, (ROOT/f'assets/vehicle-packs/cgp-p{g}.ips').read_bytes())
                         for g in (1, 2, 3)]
    clean = {k:v for k,v in os.environ.items() if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    checks = []

    def case(identity):
        p = out/NAMES[identity]
        p.mkdir()
        shutil.copytree(ROOT/'assets', p/'assets')
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='7',
                   FZERO_BS_CARS='0', FZERO_BS_TRACKS='0', FZERO_CGP_REBALANCE='0', FZERO_RULES='',
                   FZERO_PACKS_DIR=str(build/'mods/packs'), FZERO_PACK_LOADER='1',
                   FZERO_TRACK_PACKS='packs', FZERO_CUP='cgp/cgp-1', SNESRECOMP_SAVE_ROOT='s')

        def run(label, frames, **settings):
            with (p/f'{label}.log').open('w') as log:
                result = subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'), str(stock), str(frames)],
                    cwd=p, env=dict(env, **settings, SNESRECOMP_WRAM_DUMP=str(p/f'{label}.ram'),
                                  SNESRECOMP_FRAME_DUMP=str(p/f'{label}.ppm')),
                    stdout=log, stderr=log, timeout=120)
            text = (p/f'{label}.log').read_text(errors='replace')
            assert result.returncode == 0 and 'fzero_native: PASS' in text, text[-2000:]
            return (p/f'{label}.ram').read_bytes(), text

        ram, log = run('finish', 2700, FZERO_TEST_RECORDS_CUP='1',
            SNESRECOMP_INPUT_SCRIPT=route(identity, 255)+',1200-1206:8,1300-1306:8,1700-1706:8',
            FZERO_STATE_SAVE=str(p/'results.sav'), FZERO_TEST_SAVE_FRAME='2500')
        assert log.count('records-fixture: finish crossing') == 1
        assert ram[0x54:0x57] == bytes((3, 3, 3)) and ram[0x14dff] == identity
        ram, _ = run('best-records', 500, FZERO_STATE_LOAD=str(p/'results.sav'),
                     SNESRECOMP_INPUT_SCRIPT='60-66:256,180-186:1', FZERO_TEST_QUICK_SLOT='1')
        assert ram[0x54:0x57] == bytes((3, 3, 6)) and ram[0x14dff] == identity, (NAMES[identity], ram[0x54:0x59].hex(), ram[0x14dff])
        image = Image.open(p/'best-records.ppm')
        # Identify the native car reservations independently from RAM OAM:
        # 16x16 bank-1 even tiles 0..6, matching retail palette 0..3.
        sprites = []
        for i in range(128):
            x,y,tile,attr = ram[0x200+i*4:0x204+i*4]
            high = ram[0x400+i//4] >> (i%4*2) & 3
            if high == 2 and y < 224 and tile in (0, 2, 4, 6) and attr & 15 == tile+1:
                sprites.append((x,y))
        assert len(sprites) == 2, (NAMES[identity], sprites)
        source = art[GROUPS[identity]]
        graphic = source[0x40000+SLOTS[identity]*0x8000+0x6aa0:]
        palette = source[0x7cd80+SLOTS[identity]*32:]
        compared = 0
        for sx,sy in sprites:
            for y in range(16):
                for x in range(16):
                    at = (y//8*2+x//8)*32+y%8*2
                    color = sum(((graphic[at+bit//2*16+bit%2]>>(7-x%8))&1)<<bit for bit in range(4))
                    if not color: continue  # native transparency shows moving scenery
                    rgb = palette[color*2] | palette[color*2+1]<<8
                    expected = tuple(((rgb>>shift)&31)*255//31 for shift in (0, 5, 10))
                    actual = image.getpixel((sx+x,sy+y))
                    assert max(abs(a-b) for a,b in zip(actual,expected)) <= 1, (NAMES[identity],x,y,actual,expected)
                    compared += 1
        assert compared > 100
        image.resize((768,672),Image.Resampling.NEAREST).save(p/'best-records.png')
        checks.append(dict(vehicle=NAMES[identity], icons=len(sprites), authored_pixels=compared))
        print('PASS Practice finish, results transition, authored car icons: '+NAMES[identity], flush=True)

    # All three donor groups plus a vanilla control; no full races required.
    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(case, (5, 9, 11, 1)))
    (out/'validation.json').write_text(json.dumps(checks,indent=2)+'\n')


if __name__ == '__main__': main()
