"""Private-ROM smoke qualification for a newly added course pack.

Checks every course's real SPC selection/render capture on retail and expanded
engines, then both engines' completed-cup records, detail navigation, vehicle
isolation and battery-save reload. Finish fixtures seed times, not driven laps.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from validate_native_menus import player_route
from validate_records import BEST, EMPTY, ROUTE, totals

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from audit_track_metadata import audit_metadata
from parse_track_pack import donor, fields


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'stock', 'out'):
        p.add_argument('--'+key, type=Path, required=True)
    p.add_argument('--pack', required=True)
    p.add_argument('--packs-root', type=Path, required=True)
    p.add_argument('--cup', help='Limit records checks to one cup ID')
    p.add_argument('--source', type=Path, help='Optional original ROM for independent metadata comparison')
    p.add_argument('--courses-only', action='store_true')
    p.add_argument('--records-only', action='store_true')
    a = p.parse_args()
    build, stock, out = a.build.resolve(), a.stock.resolve(), a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    shutil.copytree(ROOT/'assets/track-packs', out/'assets/track-packs')
    shutil.copytree(ROOT/'assets/vehicle-packs', out/'assets/vehicle-packs')
    (out/'packs').mkdir()
    shutil.copytree(a.packs_root/a.pack,out/'mods/packs'/a.pack)
    registry = ROOT/'assets/track-packs'
    original = stock.read_bytes()
    if (registry/f'{a.pack}.ini').is_file():
        manifest, layout = fields(registry/f'{a.pack}.ini'), fields(registry/f'{a.pack}.layout')
        packed = donor(original, (registry/f'{a.pack}.ips').read_bytes())
        report = audit_metadata(packed, layout, manifest)
        if a.source:
            source_report = audit_metadata(a.source.read_bytes(), layout, manifest)
            assert source_report['tracks'] == report['tracks'], 'Source/packed metadata differs'
        tracks = report['tracks']
        cups = [c.split('|')[0] for c in manifest['cup']]
    else:
        # Qualify an actual interactive import without adding it to defaults.
        from audit_track_metadata import SONGS
        content = json.loads((a.packs_root/a.pack/'courses.json').read_text(encoding='utf-8'))
        by_id = {c['id']: c for c in content['courses']}
        cups = [c['id'] for c in content['cups']]
        tracks = [dict(id=identity, name=by_id[identity]['name'], cup=cup['id'],
                       snes_music=SONGS[by_id[identity]['music']['spc']])
                  for cup in content['cups'] for identity in cup['courses']]
        if a.source:
            from convert_course_content import probe_fzedit_metadata
            decoded = probe_fzedit_metadata(a.source.read_bytes())['tracks']
            assert [(t['name'], t['snes_music']) for t in tracks] == \
                   [(t['name'], t['stock_spc_theme']) for t in decoded], 'Source/imported metadata differs'
    if a.cup: cups=[c for c in cups if c==a.cup]
    assert cups, 'No matching cups'
    clean = {k:v for k,v in os.environ.items() if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    base = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_BS_CARS='0', FZERO_BS_TRACKS='0',
                FZERO_CGP_REBALANCE='0', FZERO_RULES='', FZERO_TRACK_PACKS='packs',
                FZERO_TEST_SAVE_SRAM='1', FZERO_PACKS_DIR=str(out/'mods/packs'), FZERO_PACK_LOADER='1')
    results = []

    def run(label, env, frames, inputs='', **extra):
        setting = dict(env, SNESRECOMP_INPUT_SCRIPT=inputs,
                       SNESRECOMP_WRAM_DUMP=str(out/f'{label}.ram'),
                       SNESRECOMP_FRAME_DUMP=str(out/f'{label}.ppm'),
                       FZERO_TEST_APURAM_DUMP=str(out/f'{label}.apu'),
                       FZERO_TEST_SRAM_DUMP=str(out/f'{label}.sram'))
        setting.update(extra)
        proc = subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'), str(stock), str(frames)],
                              cwd=out, env=setting, capture_output=True, text=True, timeout=240)
        log = proc.stdout+proc.stderr
        (out/f'{label}.log').write_text(log, encoding='utf-8')
        assert proc.returncode == 0 and 'fzero_native: PASS' in log, (label, log[-2400:])
        return (out/f'{label}.ram').read_bytes(), (out/f'{label}.sram').read_bytes(), log

    def course(case):
        index, expanded, track = case
        ordinal = [t for t in tracks if t['cup'] == track['cup']].index(track)
        label = f"{'expanded' if expanded else 'retail'}-{track['id']}"
        env = dict(base, FZERO_CGP_CARS='7' if expanded else '0', SNESRECOMP_SAVE_ROOT=f's{index}',
                   FZERO_CUP=f"{a.pack}/{track['cup']}", FZERO_TEST_COURSE=str(ordinal), FZERO_ASPECT='21:9')
        inputs = player_route(0, 7)+',600-606:8,730-736:8,900-906:8,960-966:8' if expanded else ROUTE
        ram, _, log = run(label, env, 1750, inputs, FZERO_RULE_PROBE='1')
        assert ram[0x54:0x56] == b'\2\3', (label, ram[0x54:0x57].hex())
        from audit_track_metadata import SONGS
        apu = (out/f'{label}.apu').read_bytes()
        assert apu[0x7fe] == 8+SONGS.index(track['snes_music']), (label, apu[0x7fe])
        assert 'rules-probe: PASS' in log
        results.append(label+': race/music/mechanics PASS')
        print(results[-1], flush=True)

    if not a.records_only:
        cases = [(i+expanded*len(tracks), expanded, t) for expanded in (0, 1) for i,t in enumerate(tracks)]
        with ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(course, cases))
    if not a.courses_only:
        for expanded in (0, 1):
            for ci, cup in enumerate(cups):
                label = f"records-{'expanded' if expanded else 'retail'}-{cup}"
                env = dict(base, FZERO_CGP_CARS='7' if expanded else '0',
                           FZERO_CUP=f'{a.pack}/{cup}', SNESRECOMP_SAVE_ROOT=f'r{expanded}{ci}')
                state = str(out/f'{label}.sav')
                ram, sram, log = run(label, env, 6400, ROUTE, FZERO_TEST_RECORDS_CUP='1',
                                    FZERO_STATE_SAVE=state, FZERO_TEST_SAVE_FRAME='6300')
                assert ram[0x54:0x57] == b'\0\3\0' and log.count('records-fixture: finish crossing') == 5, label
                vehicle = 'BLUE FALCON' if expanded else 'SHARED RECORDS'
                match = re.findall(rf'\[records-browser\] page=(\d+) vehicle={vehicle} selected=(\d+)', log)[-1]
                selected = int(match[1]); column = selected//5
                offset = (5, 0xac, 0x153)[column]-5
                assert totals(sram[offset:]) == [BEST]*5, (label, 'completed times missing')
                saves = list((out/env['SNESRECOMP_SAVE_ROOT']).rglob('records.bin'))
                assert len(saves) == 1 and totals(saves[0].read_bytes()[32:]) == [BEST]*5
                before = saves[0].read_bytes()
                navigation = [('page-return', '60-66:1024,120-126:2048', BEST)]
                if expanded:
                    navigation += [('other-car', '60-66:512', EMPTY),
                                   ('return-car', '60-66:512,120-126:2', BEST)]
                for step, buttons, expected in navigation:
                    ram, sram, _ = run(label+'-'+step, env, 300, buttons, FZERO_STATE_LOAD=state)
                    assert ram[0x54:0x57] == b'\0\3\0' and totals(sram[offset:]) == [expected]*5, (label, step)
                # Visit every course's detail screen through real input.
                ram, _, detail = run(label+'-details', env, 900,
                    '60-66:8,200-206:32,340-346:32,480-486:32,620-626:32', FZERO_STATE_LOAD=state)
                assert ram[0x54:0x57] == (b'\0\3\4' if expanded else b'\0\5\0'), label
                if expanded:
                    for track in (t for t in tracks if t['cup'] == cup):
                        assert f"course={track['id']} " in detail, (label, track['id'])
                _, sram, _ = run(label+'-reload', env, 1400, ROUTE)
                assert totals(sram) == [BEST]*5, (label, 'battery records not recovered')
                assert saves[0].read_bytes() == before, (label, 'browsing altered records')
                results.append(label+': completion/details/vehicle isolation/persistence PASS')
                print(results[-1], flush=True)
    assert stock.read_bytes() == original
    (out/'validation.json').write_text(json.dumps(dict(pack=a.pack, checks=results), indent=2)+'\n')


if __name__ == '__main__':
    main()
