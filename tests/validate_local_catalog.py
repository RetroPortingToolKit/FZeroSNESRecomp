"""Qualify VS course resources and race flow using the live single-player catalog.

This is an explicitly enabled headless teleport fixture, not driving evidence.
Only poses, motion and health are forced. Real gates/finish crossings award
laps and results; snapshots, rematch and native/HD rendering use public APIs.
All ROM-derived evidence stays in the caller's fresh private output directory.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('headless', 'rom', 'packs', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--bs', action='store_true', help='Use the native BS profile (1P disables the pack loader)')
    parser.add_argument('--first', type=int, default=0)
    parser.add_argument('--count', type=int, help='Limit the number of consecutive courses')
    parser.add_argument('--frames', type=int, default=800000)
    parser.add_argument('--timeout', type=int, default=1800)
    args = parser.parse_args()
    if args.first < 0 or (args.count is not None and args.count < 1):
        parser.error('first must be nonnegative and count must be positive')
    args.headless, args.rom, args.packs, args.out = (
        p.resolve() for p in (args.headless, args.rom, args.packs, args.out))
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / 'settings').mkdir()
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    env.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
               FZERO_PACKS_DIR=str(args.packs), FZERO_TRACK_PACKS=str(args.out / 'settings'),
               FZERO_PACK_LOADER=str(int(not args.bs)), FZERO_BS_CARS='0', FZERO_BS_TRACKS=str(int(args.bs)),
               FZERO_CGP_CARS='0', FZERO_DELUXE_DATA='embedded', FZERO_RULES='',
               SNESRECOMP_SAVE_ROOT='qa-saves', SNESRECOMP_MSU1='off',
               FZERO_TEST_LOCAL_TELEPORT='1', FZERO_TEST_LOCAL_CATALOG=str(args.out),
               FZERO_TEST_PRESENT_EVERY='8')
    if args.count is not None:
        env['FZERO_TEST_LOCAL_CATALOG_COUNT'] = str(args.count)
    started = time.monotonic()
    with (args.out / 'run.log').open('w') as log:
        run = subprocess.run([str(args.headless), str(args.rom), str(args.frames),
                              '--versus', str(args.first), '0', '1'],
                             cwd=args.out, env=env, stdout=log, stderr=log,
                             timeout=args.timeout)
    text = (args.out / 'run.log').read_text(errors='replace')
    table = args.out / 'catalog.tsv'
    with table.open(newline='', encoding='utf-8') as file:
        courses = list(csv.DictReader(file, delimiter='\t'))
    passed = bool(courses) and run.returncode == 0 and (
        'catalog-test: ALL %d COURSES PASS' % len(courses)) in text
    report = dict(passed=passed, validation='teleport lifecycle; no natural driving claim',
                  bs=args.bs, seconds=round(time.monotonic() - started, 3),
                  executable_sha256=hashlib.sha256(args.headless.read_bytes()).hexdigest(),
                  rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(), courses=courses)
    (args.out / 'report.json').write_text(json.dumps(report, indent=2))
    print(('PASS' if passed else 'FAIL') + ': %d courses, %.2fs; %s' % (
        len(courses), report['seconds'], args.out))
    if not passed:
        print(text[-4000:])
        raise SystemExit(1)


if __name__ == '__main__':
    main()
