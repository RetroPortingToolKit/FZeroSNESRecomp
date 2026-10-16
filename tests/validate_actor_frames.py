"""Observe stock Practice ticks against the portable actor, without guest edits.

Each machine is selected through ordinary native menus. Both a per-tick
comparison and an independently continuing shadow are required. This bounded
route exercises acceleration, shoulders, steering, braking and course edges;
it does not certify every course interaction or physical-controller handling.
All launches are muted. ROM-derived captures stay in the supplied private out.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--stock', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    build, stock, out = (x.resolve() for x in (a.build, a.stock, a.out))
    out.mkdir(parents=True, exist_ok=False)
    exe = build/('FZeroSNESRecompHeadless.exe' if os.name == 'nt' else 'FZeroSNESRecompHeadless')
    # Interpret these actual native entries so AOT calls cannot bypass hooks.
    targets = out/'targets.txt'
    targets.write_text('008d4b\n00a1c0\n009144\n00dd20\n00b61b\n00ba70\n')
    env = {k: v for k, v in os.environ.items() if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_'))}
    env.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
               FZERO_PACK_LOADER='0', FZERO_BS_CARS='0', FZERO_BS_TRACKS='0',
               FZERO_CGP_CARS='0', FZERO_RULES='', SNESRECOMP_SAVE_ROOT='s',
               FZERO_TEST_ACTOR_FRAME_PARITY='1', SNESRECOMP_LLE_INTERP_TARGET_FILE=str(targets))
    results = []
    for machine, downs in enumerate((0, 2, 1, 3)):
        selection = ''.join(f'{400+i*40}-{406+i*40}:32,' for i in range(downs))
        route = ('300-306:32,320-326:8,'+selection+
                 '540-546:8,680-686:8,850-856:8,910-916:8,'
                 '1610-3059:1,1760-1809:1024,1910-1959:2048,2060-2109:128,'
                 '2160-2209:1088,2260-2289:2,2460-2609:64,2710-2809:1152')
        for mode in ('per-tick', 'persistent'):
            directory = out/f'machine-{machine}-{mode}'
            directory.mkdir()
            run_env = dict(env, SNESRECOMP_INPUT_SCRIPT=route,
                           SNESRECOMP_WRAM_DUMP=str(directory/'final.ram'))
            if mode == 'persistent': run_env['FZERO_TEST_ACTOR_FRAME_CHAIN'] = '1'
            with (directory/'run.log').open('w', encoding='utf-8') as log:
                run = subprocess.run([str(exe), str(stock), '3060'], cwd=directory,
                                     env=run_env, stdout=log, stderr=log, timeout=120)
            log = (directory/'run.log').read_text(encoding='utf-8', errors='replace')
            ram = (directory/'final.ram').read_bytes()
            found = re.search(r'actor-frame: mode=(\S+) comparisons=(\d+) mismatches=(\d+)', log)
            passed = (run.returncode == 0 and ram[0x52] == machine and found is not None
                      and int(found[2]) >= 600 and int(found[3]) == 0)
            result = dict(machine=machine, selected=ram[0x52], mode=mode, exit=run.returncode,
                          comparison=found.groups() if found else None, passed=passed)
            results.append(result)
            (out/'results.json').write_text(json.dumps(dict(
                executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                results=results), indent=2)+'\n')
            print(json.dumps(result), flush=True)
    if not all(r['passed'] for r in results): raise SystemExit(1)


if __name__ == '__main__': main()
