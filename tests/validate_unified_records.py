"""Private-ROM check: GP and Practice update the same course/car records.

Use disposable saves. Cross real finish lines after seeding checkpoint/timing
state, then restart and browse the persisted records in widescreen.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from PIL import Image

from validate_records import ROUTE, BEST
from validate_practice_catalog import route
from validate_native_menus import player_route

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'stock', 'packs', 'out'):
        parser.add_argument('--'+key, type=Path, required=True)
    args = parser.parse_args()
    build, stock, packs, out = (getattr(args, k).resolve() for k in ('build', 'stock', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    clean = {k: v for k, v in os.environ.items()
             if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    original = hashlib.sha256(stock.read_bytes()).digest()
    report = []
    for name, cup, index, player in (('native', 'bs-deluxe/knight', 0, 0),
                                    ('guest-car', 'bs-deluxe/knight', 0, 10),
                                    ('imported', 'astra-front/astra', 3, 0)):
        folder = out/name
        folder.mkdir()
        shutil.copytree(ROOT/'assets', folder/'assets')
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='7', FZERO_CGP_REBALANCE='15',
                   FZERO_RULES='all', FZERO_PACKS_DIR=str(packs), FZERO_TRACK_PACKS='settings',
                   FZERO_PACK_LOADER='1', FZERO_CUP=cup, FZERO_ALWAYS_RECORDS='1',
                   SNESRECOMP_SAVE_ROOT='s', FZERO_TEST_SAVE_SRAM='1', FZERO_VIEWPORT_SCRIPT='0:16:9')

        def run(label, frames, **extra):
            e = dict(env, SNESRECOMP_WRAM_DUMP=str(folder/f'{label}.ram'),
                     FZERO_TEST_SRAM_DUMP=str(folder/f'{label}.sram'),
                     SNESRECOMP_FRAME_DUMP=str(folder/f'{label}.ppm'), **extra)
            result = subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'), str(stock), str(frames)],
                                    cwd=folder, env=e, capture_output=True, text=True, timeout=300)
            (folder/f'{label}.log').write_text(result.stdout+result.stderr)
            assert result.returncode == 0, (name, label, result.stderr[-2000:])
            return (folder/f'{label}.ram').read_bytes(), (folder/f'{label}.sram').read_bytes()

        gp_route = player_route(player)+',560-566:8,640-646:8,730-736:8,790-796:8'
        run('gp', 6400, SNESRECOMP_INPUT_SCRIPT=gp_route, FZERO_TEST_RECORDS_CUP='1')
        paths = list((folder/'s').rglob('records.bin'))
        matching = [p for p in paths if p.read_bytes()[37:40] == BEST]
        assert len(matching) == 1, (name, matching)
        saved = matching[0]
        digest = saved.read_bytes()[:32]
        before = {str(p): p.read_bytes() for p in paths}
        navigation = route(player, 255)+',1200-1206:8'
        navigation += ''.join(f',{1300+i*30}-{1303+i*30}:32' for i in range(index))
        navigation += ',1450-1456:8,1800-1806:8'
        ram, sram = run('practice', 2501, SNESRECOMP_INPUT_SCRIPT=navigation,
                        FZERO_STATE_SAVE=str(folder/'practice.sav'), FZERO_TEST_SAVE_FRAME='2500')
        assert ram[0x54:0x57] == b'\2\3\0' and ram[0x58], (name, 'Practice did not start')
        assert sram[5:8] == BEST, (name, 'GP time absent from Practice')
        # Faster total/lap than GP; preserve the native finish/write path.
        (folder/'finish.txt').write_text(
            '0 0 d40 0480\n0 0 d00 '+ram[0xad:0xae].hex()+
            '\n0 0 b70 90'+ram[0xae:0xaf].hex()+'\n0 0 b90 80'+ram[0xaf:0xb0].hex()+
            '\n0 0 c3 00\n0 0 c0 012234\n0 0 e90 002052004127010059012040ffffff\n')
        run('finish', 160, FZERO_STATE_LOAD=str(folder/'practice.sav'),
            FZERO_TEST_WRAM_SCRIPT=str(folder/'finish.txt'))
        current = saved.read_bytes()
        practice_best = current[37:40]  # Native Practice may also set its ghost flag.
        assert current[:32] == digest and practice_best[0] & 0x80 and \
            bytes([practice_best[0] & 15])+practice_best[1:] == bytes.fromhex('012234'), (name, 'Practice write missing')
        assert current[40:43] == BEST, (name, 'GP time was lost')
        assert set(str(p) for p in (folder/'s').rglob('records.bin')) == set(before), 'Practice created a mode namespace'
        # Restart the process and enter GP: it must recover the Practice best.
        _, sram = run('gp-restart', 1300, SNESRECOMP_INPUT_SCRIPT=gp_route)
        assert sram[5:8] == practice_best, (name, 'Practice best absent on GP restart')
        unchanged = saved.read_bytes()
        # Records starts on the active cup's page. Do not tab to a different
        # pack here: verify the very times written above on the visible page.
        browser = '300-306:32,360-366:32,420-426:8,650-656:8'
        ram, sram = run('records-wide', 950, SNESRECOMP_INPUT_SCRIPT=browser,
                        FZERO_STATE_SAVE=str(folder/'records.sav'), FZERO_TEST_SAVE_FRAME='949')
        assert ram[0x54:0x57] == b'\0\3\4' and saved.read_bytes() == unchanged
        assert sram[5:8] == practice_best and sram[8:11] == BEST, 'Visible Records page has wrong times'
        assert 'detail '+cup+' course=' in (folder/'records-wide.log').read_text()
        run('records-stock', 1, FZERO_STATE_LOAD=str(folder/'records.sav'), FZERO_VIEWPORT_SCRIPT='0:4:3')
        wide = Image.open(folder/'records-wide.ppm').convert('RGB')
        native = Image.open(folder/'records-stock.ppm').convert('RGB')
        extra = (wide.width-256)//2
        assert wide.crop((extra, 0, extra+256, 224)).tobytes() == native.tobytes(), \
            'Widescreen altered the native Records layout'
        assert saved.read_bytes() == unchanged, 'Changing viewport modified records'
        report.append(name+': GP -> Practice -> faster native finish -> GP restart -> widescreen Records')
        print('PASS', report[-1], flush=True)
    assert hashlib.sha256(stock.read_bytes()).digest() == original
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
