"""Muted, private-ROM integration for presentation-only VS MSU playback.

Synthetic stereo signatures verify the real mixer, alongside native fallback,
pause/rewind/lifecycle and unchanged canonical APU/RAM state. Course completion
uses the owner's authorized teleport fixture, not natural-driving qualification.
"""
import argparse
import collections
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import wave


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'packs', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--filter', default='')
    p.add_argument('--jobs', type=int, choices=(1, 2, 3), default=3)
    a = p.parse_args()
    build, rom, packs, out = (getattr(a, key).resolve() for key in ('build', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    shutil.copytree(root / 'assets', out / 'assets')
    media = out / 'synthetic'
    media.mkdir()
    for prefix, numbers, bias in [('cgp', list(range(1, 8)) + list(range(10, 65)), 0),
                                  ('F-ZERO Astra Front', range(10, 19), 100)]:
        for number in numbers:
            (media / f'{prefix}-{number}.pcm').write_bytes(
                b'MSU1' + bytes(4) + struct.pack('<hh', (number+bias)*100, -(number+bias)*100)*4410)
    empty = out / 'empty-media'
    empty.mkdir()
    # Valid header without any sample data must restore the stock soundtrack.
    (empty / 'test-10.pcm').write_bytes(b'MSU1' + bytes(4))
    legacy = out / 'legacy-media'
    legacy.mkdir()
    (legacy / 'f-zero_msu1.ips').write_bytes(b'VS must never apply a guest music patch')
    for number in list(range(1, 8)) + [10]:
        shutil.copy2(media / f'cgp-{number}.pcm', legacy / f'test-{number}.pcm')
    auto_rom = out / 'auto.sfc'
    shutil.copy2(rom, auto_rom)
    for number in list(range(1, 8)) + [10]:
        shutil.copy2(media / f'cgp-{number}.pcm', out / f'auto-{number}.pcm')
    clean_packs = out / 'clean-packs'
    shutil.copytree(packs, clean_packs, ignore=shutil.ignore_patterns('*.pcm', '*.msu'))
    custom = out / 'custom-packs'
    shutil.copytree(clean_packs, custom)
    bower = custom / 'bower-league'
    index = json.loads((bower / 'courses.json').read_text())
    course = index['courses'][0]
    music = bower / 'music'
    music.mkdir(exist_ok=True)
    (music / (Path(course['source']).stem + '.pcm')).write_bytes(
        b'MSU1' + bytes(4) + struct.pack('<hh', 21000, -21000)*4410)
    cases = [dict(name='stock', course=0, track=10), dict(name='astra', course=15, track=110),
             dict(name='custom-course', course=25, track=210, packs=custom),
             dict(name='native', course=0, off=True), dict(name='missing', course=0, missing=True),
             dict(name='empty', course=0, empty=True),
             dict(name='legacy-prefix', course=0, track=10, legacy=True),
             dict(name='auto-prefix', course=0, track=10, auto=True),
             dict(name='paused', course=0, track=10, pause=True),
             dict(name='resumed', course=0, track=10, pause=True, resume=True),
             dict(name='rewind', course=15, track=110, rewind=True),
             dict(name='finished', course=0, finish=True),
             dict(name='crash-results', course=0, crash=True)]
    if a.filter:
        cases = [c for c in cases if c['name'] in a.filter.split(',')]
    assert cases
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'SNES_NET_', 'SNES_RB', 'RNET_', 'LNG_'))}
    clean.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')

    def run(case):
        folder = out / case['name']
        folder.mkdir()
        frames = 1600 if case.get('pause') else 1100
        if case.get('finish'):
            frames = 3500
        if case.get('crash'):
            frames = 1800
        source = 'off' if case.get('off') else str(empty / ('test' if case.get('empty') else 'absent')) if case.get('empty') or case.get('missing') else str(media / 'cgp')
        if case.get('legacy'):
            source = str(legacy / 'test')
        if case.get('auto'):
            source = 'auto'
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='0', FZERO_BS_CARS='0',
                   FZERO_BS_TRACKS='0', FZERO_RULES='', FZERO_PACK_LOADER='1',
                   FZERO_PACKS_DIR=str(case.get('packs', clean_packs)), FZERO_TRACK_PACKS=str(folder / 'settings'),
                   SNESRECOMP_SAVE_ROOT=case['name']+'/s', SNESRECOMP_WAV=str(folder / 'audio.wav'),
                   FZERO_TEST_APURAM_DUMP=str(folder / 'apu.bin'), FZERO_TEST_DSP_DUMP=str(folder / 'dsp.bin'),
                   SNESRECOMP_WRAM_DUMP=str(folder / 'ram.bin'), SNESRECOMP_MSU1=source)
        if case.get('pause'):
            env['SNESRECOMP_INPUT_SCRIPT'] = '1000-1002:8'
            if case.get('resume'):
                env['FZERO_TEST_LOCAL_RESUME_FRAME'] = '1250'
        if case.get('rewind'):
            env.update(FZERO_REWIND_TEST='1', FZERO_TEST_SAVE_FRAME='1000')
        if case.get('finish'):
            env['FZERO_TEST_LOCAL_TELEPORT'] = '1'
        if case.get('crash'):
            env['FZERO_TEST_LOCAL_END_FRAME'] = '1000'
        cmd = [str(build / 'FZeroSNESRecompHeadless.exe'), str(auto_rom if case.get('auto') else rom), str(frames),
               '--versus', str(case['course']), '9', '3']
        proc = subprocess.run(cmd, cwd=out, env=env, capture_output=True, text=True, timeout=180)
        log = proc.stdout + proc.stderr
        (folder / 'run.log').write_text(log, encoding='utf-8')
        assert proc.returncode == 0, (case['name'], proc.returncode, log[-2500:])
        assert '[track-library] loaded astra-front: 10' in log and '[pack-loader]' not in log, 'incomplete catalog'
        with wave.open(str(folder / 'audio.wav')) as audio:
            pairs = list(struct.iter_unpack('<hh', audio.readframes(audio.getnframes())))
        tail = collections.Counter(left-right for left, right in pairs[-32040:])
        if 'track' in case:
            assert f"command=6 course={case['course']} track=" in log and 'output=PCM' in log, (case['name'], 'no race PCM')
            assert tail[case['track']*200] > 1000, (case['name'], tail.most_common(5))
        if case.get('missing') or case.get('empty'):
            assert 'output=SPC' in log or 'Playback stopped unexpectedly' in log, (case['name'], 'no fallback')
            assert any(left or right for left, right in pairs[-32040:]), (case['name'], 'silent fallback')
        if case.get('rewind'):
            assert 'resimulation identical' in log, (case['name'], 'rewind unqualified')
        if case.get('finish'):
            assert 'player=1 laps=5 status=1' in log and 'player=2 laps=5 status=1' in log, 'two actual finishes missing'
        result = dict(name=case['name'], dominant_stereo_differences=tail.most_common(3),
                      transitions=[line for line in log.splitlines() if '[versus-msu]' in line], passed=True)
        print(json.dumps(result), flush=True)
        return result

    results, errors = [], []
    # Warm the immutable source cache before parallel readers, and establish
    # the native reference independently of any replacement music.
    for case in cases:
        if case['name'] == 'native':
            results.append(run(case))
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futures = [pool.submit(run, case) for case in cases if case['name'] != 'native']
        for future in futures:
            try:
                results.append(future.result())
            except Exception as error:
                errors.append(repr(error))
    if {'stock', 'native', 'missing'}.issubset(c['name'] for c in cases):
        for filename in ('ram.bin', 'apu.bin', 'dsp.bin'):
            reference = (out / 'native' / filename).read_bytes()
            for name in ('stock', 'missing'):
                assert reference == (out / name / filename).read_bytes(), (name, filename, 'audio preference changed simulation')
    (out / 'report.json').write_text(json.dumps(dict(passed=results, failed=errors), indent=2), newline='\n')
    assert not errors, '\n'.join(errors)


if __name__ == '__main__':
    main()
