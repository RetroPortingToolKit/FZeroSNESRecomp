"""Private-ROM host lifecycle checks using ordinary two-player pad inputs.

This checks boot/handoff, snapshots, rewind, exact capture replay and viewport
independence. It does not qualify five-lap stock races or physical controllers.
All artifacts remain under --out and every launch uses dummy audio.
"""
import argparse
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
    p.add_argument('--packs', type=Path, help='Use this installed/custom course catalog')
    p.add_argument('--course', type=int, default=0, help='Live catalog course ID for state/replay checks')
    p.add_argument('--machines', default='0,1', help='Two roster identities,0..11')
    p.add_argument('--bs', action='store_true', help='Enable the native BS track profile')
    p.add_argument('--all-courses', action='store_true', help='Check native setup of all 15 courses, not full races')
    a = p.parse_args()
    a.machines = tuple(map(int, a.machines.split(',')))
    if len(a.machines)!=2 or any(m<0 or m>=12 for m in a.machines): p.error('machines must be two IDs,0..11')
    build, stock, out = (v.resolve() for v in (a.build, a.stock, a.out))
    out.mkdir(parents=True, exist_ok=False)
    (out/'packs').mkdir()
    (out/'settings').mkdir()
    (out/'s').mkdir()
    sentinel = out/'s/save.srm'
    sentinel.write_bytes(bytes(range(256))*8)
    original_sram = sentinel.read_bytes()
    env = {k: v for k, v in os.environ.items() if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    env.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', FZERO_PACK_LOADER=str(int(a.packs is not None)),
               FZERO_BS_CARS='0', FZERO_BS_TRACKS=str(int(a.bs)), FZERO_CGP_CARS='0', FZERO_RULES='',
               FZERO_PACKS_DIR=str(a.packs.resolve() if a.packs else out/'packs'), FZERO_TRACK_PACKS=str(out/'settings'),
               SNESRECOMP_SAVE_ROOT=str(out/'s'))
    # The runtime's save-root API is bounded to 95 bytes.
    if len(env['SNESRECOMP_SAVE_ROOT'])+len('/local-2p') >= 96:
        p.error('Use a shorter --out path for the save-root API')
    if os.name == 'nt': env['PATH'] = 'C:/msys64/mingw64/bin;'+env.get('PATH', '')
    exe = build/('FZeroSNESRecompHeadless'+('.exe' if os.name == 'nt' else ''))
    route = '1810-2299:4097,1950-1980:524288,2100-2130:64'
    results = {}

    def run(label, frames, extra=None, course=None, machines=None, expected=0, versus=True):
        print(label, flush=True)
        args = [str(exe), str(stock), str(frames)]
        if versus: args += ['--versus', str(a.course if course is None else course), *(str(v) for v in (a.machines if machines is None else machines))]
        with (out/f'{label}.log').open('w', encoding='utf-8') as log:
            r = subprocess.run(args, cwd=build, env=dict(env, **(extra or {})), stdout=log, stderr=log, timeout=300)
        log = (out/f'{label}.log').read_text(encoding='utf-8', errors='replace')
        assert r.returncode == expected, (label, r.returncode, log[-5000:])
        found = re.search(r'local-versus: tick=(\d+) phase=(\d+) race=(\d+) hash=([0-9a-f]{16})', log)
        results[label] = {'exit': r.returncode, 'session': found.groups() if found else None}
        return log, found.groups() if found else None

    (out/'diagnostics').mkdir()
    state = out/'racing.sav'
    log, baseline = run('racing', 2300, dict(SNESRECOMP_INPUT_SCRIPT=route,
        FZERO_LIFECYCLE_TEST='1', FZERO_REWIND_TEST='1', FZERO_TEST_SAVE_FRAME='2000',
        FZERO_LOCAL_STATE_ATOMIC_TEST='1',
        FZERO_STATE_SAVE=str(state), FZERO_CAPTURE_DIR=str(out/'diagnostics')))
    assert baseline and 'rewind: actual ring' in log and 'lifecycle: save/load' in log and 'local-state:' in log, log
    _, continued = run('fresh-process', 300, dict(FZERO_STATE_LOAD=str(state),
        SNESRECOMP_INPUT_SCRIPT='0-299:4097,100-130:64'), course=14, machines=(3, 3))
    assert continued == baseline, (continued, baseline)

    # Preserve simulation across radically different per-pane projections.
    _, wide = run('viewport-changes', 2300, dict(SNESRECOMP_INPUT_SCRIPT=route,
        FZERO_VIEWPORT_SCRIPT='0:Fit@1920x1080,1700:4:3@640x960,1900:32:9@3840x720,2100:16:9@1280x720'))
    assert wide == baseline, (wide, baseline)

    for label, frame in [('bootstrap', 100), ('countdown', 650)]:
        path = out/f'{label}.sav'
        run(label, 1800, dict(FZERO_TEST_SAVE_FRAME=str(frame), FZERO_STATE_SAVE=str(path),
                            FZERO_LIFECYCLE_TEST='1', FZERO_REWIND_TEST='1'))
        _, restored = run(label+'-fresh', 2300-frame, dict(FZERO_STATE_LOAD=str(path),
            SNESRECOMP_INPUT_SCRIPT=f'{1810-frame}-{2299-frame}:4097,{1950-frame}-{1980-frame}:524288,{2100-frame}-{2130-frame}:64'))
        assert restored == baseline, (restored, baseline)

    capture = next((out/'diagnostics').iterdir())
    metadata = json.loads((capture/'session.json').read_text())
    assert metadata['mode']=='local-2p' and metadata['players']==2 and metadata['save_namespace']=='local-2p'
    stem = sorted(capture.glob('segment-*.inputs'))[-1].with_suffix('')
    inputs = stem.with_suffix('.inputs')
    lines = inputs.read_text().splitlines()
    replay = dict(FZERO_REPLAY_INPUT_FILE=str(inputs), FZERO_REPLAY_STATE=str(stem.with_suffix('.sav')),
                  FZERO_REPLAY_EXECUTION=str(stem.with_suffix('.execution')))
    _, replayed = run('capture-replay', len(lines), replay)
    assert replayed == baseline, (replayed, baseline)
    fields = lines[0].split()
    fields[-1] = str(int(fields[-1]) ^ 1)
    bad_inputs = out/'bad.inputs'
    bad_inputs.write_text(' '.join(fields)+'\n'+'\n'.join(lines[1:])+'\n')
    log, _ = run('capture-divergence', len(lines), dict(replay, FZERO_REPLAY_INPUT_FILE=str(bad_inputs)), expected=11)
    assert 'DIVERGENCE' in log

    image = state.read_bytes()
    for label, index in [('identity', 16), ('guest', 256), ('session', len(image)//2), ('checksum', len(image)-1)]:
        bad = bytearray(image)
        bad[index] ^= 1
        path = out/f'corrupt-{label}.sav'
        path.write_bytes(bad)
        run('reject-'+label, 1, dict(FZERO_STATE_LOAD=str(path)), expected=3)
    run('reject-versus-in-sp', 1, dict(FZERO_STATE_LOAD=str(state)), expected=3, versus=False)
    assert sentinel.read_bytes() == original_sram, 'Single-player battery was changed'
    assert (out/'s/local-2p').is_dir()
    assert list((out/'s/local-2p').glob('fzero-local-2p*.sav')), 'Versus slot missing'

    if a.all_courses:
        for course in range(15):
            machines = (course % 4, course % 4 if course % 2 else (course+1) % 4)
            log, session = run(f'course-{course:02}', 1800, course=course, machines=machines)
            assert session and f'course={course} machines={machines[0]},{machines[1]}' in log
    (out/'results.json').write_text(json.dumps(results, indent=2)+'\n')
    print('Local versus host lifecycle checks passed; full stock races/human play remain separate gates.', flush=True)


if __name__ == '__main__':
    main()
