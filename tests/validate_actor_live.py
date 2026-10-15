"""Compare portable actor movement to a real stock run; no guest state edits.

This qualifies the movement boundary only. It does not certify a complete
versus simulation, full race, contact response, or two-controller gameplay.
ROM, WRAM and logs remain in the caller's private output directory.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--stock', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    build, stock, out = (p.resolve() for p in (args.build, args.stock, args.out))
    out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    env.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
               FZERO_PACK_LOADER='0', FZERO_BS_CARS='0', FZERO_BS_TRACKS='0',
               FZERO_CGP_CARS='0', FZERO_CGP_REBALANCE='0', FZERO_RULES='',
               FZERO_TEST_ACTOR_PARITY='1', SNESRECOMP_SAVE_ROOT='s')
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/mingw64/bin;' + env.get('PATH', '')
    executable = build / ('FZeroSNESRecompHeadless' + ('.exe' if os.name == 'nt' else ''))
    # $03:863C..867E advances rows with Down; the $867B identity order is
    # 0,2,1,3. Verify each ordinary menu route by the resulting $52 value.
    # Leave the native car-change animation time to complete between presses.
    selections = ('', ',480-483:32,580-583:32', ',480-483:32',
                  ',480-483:32,580-583:32,680-683:32')
    names = ('blue-falcon', 'wild-goose', 'golden-fox', 'fire-stingray')
    route = ('320-326:8,800-806:8,920-926:8,1090-1096:8,1150-1156:8,'
             '1520-2999:1,1660-1700:64,1840-1870:128,1980-2010:1024,'
             '2100-2140:2048,2300-2340:2')
    results = {}
    for machine, name in enumerate(names):
        folder = out / name
        folder.mkdir(exist_ok=True)
        settings = dict(env, SNESRECOMP_INPUT_SCRIPT=route + selections[machine],
                        SNESRECOMP_WRAM_DUMP=str(folder / 'final.ram'))
        proc = subprocess.run([str(executable), str(stock), '3000'], cwd=folder,
                              env=settings, capture_output=True, text=True, timeout=180)
        log = proc.stdout + proc.stderr
        (folder / 'run.log').write_text(log, encoding='utf-8')
        assert proc.returncode == 0, (name, log[-3000:])
        ram = (folder / 'final.ram').read_bytes()
        assert len(ram) == 0x20000 and ram[0x52] == machine, (name, ram[0x52])
        counts = re.search(r'actor-live: comparisons=(\d+) mismatches=(\d+)', log)
        assert counts and int(counts[1]) >= 100 and int(counts[2]) == 0, (name, log[-1000:])
        results[name] = dict(machine=machine, frames=3000, comparisons=int(counts[1]), mismatches=0)
        print(name, results[name], flush=True)
    (out / 'validation.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
