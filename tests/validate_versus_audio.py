"""Verify real SPC music voices in VS; engine noise alone is not a pass.

Requires the owner's private ROM and installed default catalog. All launches
are muted; PCM, RAM and DSP readback remain in the fresh private output folder.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess
import wave
from array import array


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'packs', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    a = p.parse_args()
    build, rom, packs, out = (getattr(a, key).resolve() for key in ('build', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    shutil.copytree(root / 'assets', out / 'assets')
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'SNES_NET_', 'SNES_RB', 'RNET_', 'LNG_'))}
    clean.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
    courses = [('death-storm', 88, 9, False), ('mute-city', 0, 0, False),
               ('death-wind', 3, 9, False), ('bs-death-wind', 3, 9, True),
               ('astra', 15, 3, False)]
    cases = [(name + '-vs', course, song, bs, '') for name, course, song, bs in courses]
    cases += [(name + '-' + action, course, song, bs, action)
              for name, course, song, bs in courses for action in ('paused', 'resumed')]
    cases += [('death-storm-single' + ('-' + action if action else ''), None, 9, False, action)
              for action in ('', 'paused', 'resumed')]

    def run(case):
        name, course, song, bs, action = case
        folder = out / name
        folder.mkdir()
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='0', FZERO_BS_CARS='0',
                   FZERO_BS_TRACKS=str(int(bs)), FZERO_RULES='', FZERO_PACK_LOADER=str(int(not bs)),
                   FZERO_PACKS_DIR=str(packs), FZERO_TRACK_PACKS=str(folder / 'settings'),
                   SNESRECOMP_SAVE_ROOT=name+'/s', SNESRECOMP_WAV=str(folder / 'audio.wav'),
                   FZERO_TEST_APURAM_DUMP=str(folder / 'apu.bin'),
                   FZERO_TEST_DSP_DUMP=str(folder / 'dsp.bin'), FZERO_TEST_DSP_TRACE=str(folder / 'dsp.frames'),
                   SNESRECOMP_WRAM_DUMP=str(folder / 'ram.bin'))
        frames = (1850 if action else 1750) if course is None else (1600 if action else 1100)
        cmd = [str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), str(frames)]
        if course is None:
            env.update(FZERO_CUP='max-league/max', FZERO_TEST_COURSE='3',
                       SNESRECOMP_INPUT_SCRIPT='320-326:8,440-446:8,560-566:8,730-736:8,790-796:8')
            if action:
                env['SNESRECOMP_INPUT_SCRIPT'] += ',1600-1602:8'
                if action == 'resumed':
                    env['SNESRECOMP_INPUT_SCRIPT'] += ',1700-1702:8'
        else:
            cmd += ['--versus', str(course), '9', '3']
            if action:
                env['SNESRECOMP_INPUT_SCRIPT'] = '1000-1002:8'
                if action == 'resumed':
                    env['FZERO_TEST_LOCAL_RESUME_FRAME'] = '1250'
        proc = subprocess.run(cmd, cwd=out, env=env, capture_output=True, text=True, timeout=120)
        (folder / 'run.log').write_text(proc.stdout + proc.stderr, encoding='utf-8')
        assert proc.returncode == 0, (name, proc.returncode, proc.stderr[-1500:])
        if course is not None:
            assert 'phase=2 ' in proc.stderr, (name, 'did not reach racing')
            if action == 'resumed':
                assert 'audio-fixture: resumed VS' in proc.stderr, (name, 'resume not exercised')
        else:
            ram = (folder / 'ram.bin').read_bytes()
            assert ram[0x54] == 2 and ram[0x55] == (5 if action == 'paused' else 3), (name, 'native pause/race not exercised')
        apu, dsp = (folder / 'apu.bin').read_bytes(), (folder / 'dsp.bin').read_bytes()
        assert apu[0x7fe] == 8 + song, (name, 'wrong uploaded song', apu[0x7fe])
        voices = [dsp[i*16+8] for i in range(6)]  # Native music voices; engine/effects use the remaining pair.
        trace = (folder / 'dsp.frames').read_bytes()
        assert len(trace) == frames*128, (name, 'incomplete DSP timeline')
        tail = [trace[i:i+128] for i in range(len(trace)-60*128, len(trace), 128)]
        active = sum(sum(d[i*16+8] > 0 for i in range(6)) >= 2 for d in tail)
        assert active >= 15, (name, 'music voices silent', active, voices)
        # The native racing program uses 0x54 in both master registers. The
        # crash command replaces it with 0x7f and can leave just its tail on
        # voices4/5: nonzero envelopes and PCM alone wrongly accept that.
        assert all([d[0x0c], d[0x1c]] == [0x54, 0x54] for d in tail), (name, 'not the native racing program', dsp[0x0c], dsp[0x1c])
        with wave.open(str(folder / 'audio.wav')) as wav:
            rate = wav.getframerate()
            wav.setpos(max(0, wav.getnframes() - rate))
            samples = array('h', wav.readframes(rate))
        rms = (sum(v*v for v in samples) / len(samples)) ** .5
        assert rms > 100, (name, 'silent PCM', rms)
        result = dict(name=name, song=song, music_voice_envelopes=voices,
                      music_active_frames=active,
                      master=[dsp[0x0c], dsp[0x1c]], rms=round(rms, 2), passed=True)
        print(json.dumps(result), flush=True)
        return result

    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(run, cases))
    (out / 'report.json').write_text(json.dumps(results, indent=2), newline='\n')


if __name__ == '__main__':
    main()
