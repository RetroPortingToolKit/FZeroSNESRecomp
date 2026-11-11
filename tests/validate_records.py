"""Private-ROM records browser regression checks.

Seeds final-lap timing/checkpoints, then crosses each real finish line. Native
record writes and result-to-records transitions run normally; the fixture skips
the final winner's fly-away animation. This is not a driving/physics test.
All ROM-derived captures, snapshots and saves remain in the supplied output dir.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ROUTE = '320-326:8,560-566:8,640-646:8,730-736:8,790-796:8'
BEST = bytes.fromhex('813032')  # Valid-record flag + BCD 1:30.32.
EMPTY = bytes.fromhex('095999')


def totals(sram):
    return [sram[5 + 33*i:8 + 33*i] for i in range(5)]


def name_pixels(path):
    """White course-name pixels, excluding the numbered rows and underline."""
    with path.open('rb') as f:
        assert f.readline().strip() == b'P6'
        width, height = map(int, f.readline().split())
        assert f.readline().strip() == b'255' and (width, height) == (256, 224)
        rgb = f.read()
    return sum(rgb[(y*width+x)*3:(y*width+x+1)*3] == b'\xff\xff\xff'
               for top in (39, 55, 71, 87, 103) for y in range(top, top+6)
               for x in range(32, 112))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--stock', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    build, stock, out = args.build.resolve(), args.stock.resolve(), args.out.resolve()
    # Never reuse someone's save directory or silently make a test non-fresh.
    out.mkdir(parents=True, exist_ok=False)
    original_hash = hashlib.sha256(stock.read_bytes()).digest()
    clean = {k: v for k, v in os.environ.items()
             if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    report = {'checks': [], 'defects': []}

    def run(folder, label, env, frames, **overrides):
        setting = dict(env, SNESRECOMP_WRAM_DUMP=str(folder/f'{label}.ram'),
                       SNESRECOMP_FRAME_DUMP=str(folder/f'{label}.ppm'),
                       FZERO_TEST_SRAM_DUMP=str(folder/f'{label}.sram'))
        setting.update(overrides)
        result = subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'), str(stock), str(frames)],
                                cwd=folder, env=setting, capture_output=True, text=True, timeout=240)
        log = result.stdout + result.stderr
        (folder/f'{label}.log').write_text(log, encoding='utf-8')
        assert result.returncode == 0, (folder.name, label, log[-2000:])
        return (folder/f'{label}.ram').read_bytes(), (folder/f'{label}.sram').read_bytes(), log

    cgp_env = None
    for name in ('stock', 'satellaview', 'cgp'):
        folder = out/name
        folder.mkdir()
        shutil.copytree(ROOT/'assets', folder/'assets')
        (folder/'packs').mkdir()
        for pack in ('cgp', 'max-league', 'bower-league'):
            (folder/f'packs/{pack}.disabled').write_text('0\n' if name == pack else '1\n')
        env = dict(clean, FZERO_DELUXE_DATA='embedded',
                   FZERO_BS_CARS=str(int(name == 'satellaview')),
                   FZERO_BS_TRACKS=str(int(name == 'satellaview')),
                   FZERO_CGP_CARS='7' if name == 'cgp' else '0',
                   FZERO_CGP_REBALANCE='15' if name == 'cgp' else '0',
                   FZERO_RULES='all' if name == 'cgp' else '',
                   FZERO_TRACK_PACKS='packs', SNESRECOMP_SAVE_ROOT='s',
                   FZERO_SCENE_TRACE='1', FZERO_TEST_SAVE_SRAM='1')
        if name == 'cgp':
            env['FZERO_CUP'] = 'cgp/cgp-1'
            cgp_env = env
        ram, sram, log = run(folder, 'complete', env, 6400,
                            FZERO_TEST_RECORDS_CUP='1', SNESRECOMP_INPUT_SCRIPT=ROUTE,
                            FZERO_STATE_SAVE=str(folder/'records.sav'), FZERO_TEST_SAVE_FRAME='6300')
        assert ram[0x54:0x57] == b'\0\3\0', (name, ram[0x54:0x57].hex())
        assert log.count('records-fixture: finish crossing') == 5, name
        if name == 'cgp':
            files = list((folder/'s').rglob('records.bin'))
            assert len(files) == 1, files
            saved = files[0].read_bytes()
            assert len(saved) == 32+32768+512
            assert totals(saved[32:]) == [BEST]*5, 'CGP native writes missing'
            assert '[records-browser] page=2 vehicle=ALL CARS selected=10' in log
            assert totals(sram[0x153-5:]) == [BEST]*5, 'Baron times missing from the third displayed cup'
            assert totals(sram) == [EMPTY]*5, 'Baron times incorrectly assigned to BS-1 CGP'
        else:
            assert totals(sram) == [BEST]*5, (name, 'native writes missing')
            assert name_pixels(folder/'complete.ppm') > 100, (name, 'course names missing')
        # Select the first completed course using native input.
        detail, detail_sram, detail_log = run(folder, 'detail', env, 260,
                                    FZERO_STATE_LOAD=str(folder/'records.sav'),
                                    SNESRECOMP_INPUT_SCRIPT='60-66:8')
        expected = b'\0\5\0' if name == 'stock' else b'\0\3\4'
        assert detail[0x54:0x57] == expected, (name, 'record detail unreachable')
        offset = 0x153-5 if name == 'cgp' else 0
        assert totals(detail_sram[offset:]) == [BEST]*5
        if name == 'cgp':
            assert detail[0x14c25] == 10, 'Detail reads a different course slot'
            assert 'cgp/cgp-1 course=marine-city' in detail_log
        # Reopen from battery SRAM, without loading a snapshot.
        script = folder/'open-records.txt'
        script.write_text('400 400 5a 02\n400 400 f2 0f\n')
        ram, reopened, _ = run(folder, 'reopen', env, 950,
                               FZERO_TEST_WRAM_SCRIPT=str(script),
                               SNESRECOMP_INPUT_SCRIPT='420-426:8')
        assert ram[0x54:0x57] == b'\0\3\0'
        assert totals(reopened[offset:]) == [BEST]*5
        if name != 'cgp': assert name_pixels(folder/'reopen.ppm') > 100
        report['checks'].append(f'{name}: native writes, completed-cup menu, battery-save restart')
        print(report['checks'][-1], flush=True)

    # Matching cup and vehicle recover their saved times on starting a race.
    # Add/remove unrelated packs in the same private directory: no rename or
    # snapshot import, so this exercises actual stable discovery/save keys.
    folder = out/'cgp'
    path = next((folder/'s').rglob('records.bin'))
    key, old_times = path.read_bytes()[:32], path.read_bytes()[32:32+0x400]
    for label, enabled in (('same-cup', False), ('unrelated-packs', True)):
        for pack in ('max-league', 'bower-league'):
            (folder/f'packs/{pack}.disabled').write_text('0\n' if enabled else '1\n')
        ram, loaded, _ = run(folder, label, cgp_env, 1300, SNESRECOMP_INPUT_SCRIPT=ROUTE)
        assert ram[0x54:0x56] == b'\2\3'
        assert totals(loaded) == [BEST]*5, (label, 'existing times inaccessible')
        assert path.read_bytes()[:32] == key and path.read_bytes()[32:32+0x400] == old_times
        assert len(list((folder/'s').rglob('records.bin'))) == 1
        report['checks'].append(f'CGP: preserved identity/times with {label}')
        print(report['checks'][-1], flush=True)

    before = path.read_bytes()
    (folder/'packs/cgp.disabled').write_text('1\n')
    disabled = dict(cgp_env)
    disabled.pop('FZERO_CUP')
    run(folder, 'pack-disabled', disabled, 400)
    assert path.read_bytes() == before, 'Disabling pack modified its records'
    (folder/'packs/cgp.disabled').write_text('0\n')
    _, loaded, _ = run(folder, 'pack-restored', cgp_env, 1300, SNESRECOMP_INPUT_SCRIPT=ROUTE)
    assert totals(loaded) == [BEST]*5
    report['checks'].append('CGP: disable/re-enable preserves and reloads saved times')
    assert hashlib.sha256(stock.read_bytes()).digest() == original_hash
    (out/'audit.json').write_text(json.dumps(report, indent=2)+'\n')
    for defect in report['defects']:
        print('CONFIRMED DEFECT:', defect, flush=True)
    return int(bool(report['defects']))


if __name__ == '__main__':
    raise SystemExit(main())
