"""Muted native-course lap cues, with real checkpoint/finish detection.

The authorized teleport fixture positions racers without setting lap/success
flags. It holds awarded laps for180 ticks to observe original visual timing.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'rom', 'packs', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--courses', default='0,15')
    args = parser.parse_args()
    build, rom, packs, out = (getattr(args, key).resolve() for key in ('build', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'RNET_', 'SNES_NET_', 'LNG_'))}
    reports, restore_frame = [], None
    courses = list(map(int, args.courses.split(',')))
    for course, replay in [(c, False) for c in courses] + [(courses[-1], True)]:
        folder = out / (str(course) + ('-restore' if replay else ''))
        folder.mkdir()
        for asset in ('vehicle-packs', 'track-packs'):
            shutil.copytree(root / 'assets' / asset, folder / 'assets' / asset)
        env = dict(clean, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                   FZERO_DELUXE_DATA='embedded', FZERO_PACK_LOADER='1', FZERO_PACKS_DIR=str(packs),
                   FZERO_TRACK_PACKS='settings', FZERO_BS_TRACKS='0', FZERO_BS_CARS='0',
                   SNESRECOMP_SAVE_ROOT='s', SNESRECOMP_MSU1='off',
                   FZERO_TEST_LAP_CUES=str(folder), FZERO_TEST_LOCAL_TELEPORT='1')
        if replay:
            assert restore_frame is not None
            env.update(FZERO_LIFECYCLE_TEST='1', FZERO_REWIND_TEST='1', FZERO_TEST_SAVE_FRAME=str(restore_frame))
        with (folder / 'run.log').open('w', encoding='utf-8') as log:
            run = subprocess.run([str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), '4500',
                                  '--versus', str(course), '5', '11'], cwd=folder, env=env,
                                 stdout=log, stderr=log, timeout=240)
        text = (folder / 'run.log').read_text(encoding='utf-8', errors='replace')
        assert run.returncode == 0, (folder.name, run.returncode, text[-3500:])
        events = re.findall(r'lap-cues: PASS frame=(\d+) player=(\d) completed=(\d) age=(\d+) boost=(\d)', text)
        assert {(int(p), int(lap), int(age)) for _, p, lap, age, _ in events} == {
            (p, lap, age) for p in (1, 2) for lap in (1, 4) for age in (0, 22, 32, 128, 160)}, text[-3500:]
        assert all(boost == '1' for _, _, lap, age, boost in events if lap == '1' and age == '0')
        for player in (1, 2):
            assert f'player={player} laps=5 status=1' in text, text[-2500:]
        if not replay and course == courses[-1]:
            restore_frame = next(int(frame) - 3 for frame, p, lap, age, _ in events
                                 if (p, lap, age) == ('1', '4', '22'))
        if replay:
            assert 'lifecycle: save/load ten-frame resimulation identical' in text, text[-3500:]
            assert 'rewind: actual ring restore and ten-frame resimulation identical' in text, text[-3500:]
        reports.append(dict(course=course, natural_lap_events=len(events), native_and_wide2x=True,
                            final_lap_restore_frame=restore_frame if replay else None, passed=True))
        print('PASS', reports[-1], flush=True)
    (out / 'report.json').write_text(json.dumps(reports, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
