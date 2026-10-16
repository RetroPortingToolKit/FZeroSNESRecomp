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
    cases = [('death-storm-vs', 88, 9, False), ('mute-city-vs', 0, 0, False),
             ('death-wind-vs', 3, 9, False), ('bs-death-wind-vs', 3, 9, True),
             ('death-storm-single', None, 9, False)]

    def run(case):
        name, course, song, bs = case
        folder = out / name
        folder.mkdir()
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='0', FZERO_BS_CARS='0',
                   FZERO_BS_TRACKS=str(int(bs)), FZERO_RULES='', FZERO_PACK_LOADER=str(int(not bs)),
                   FZERO_PACKS_DIR=str(packs), FZERO_TRACK_PACKS=str(folder / 'settings'),
                   SNESRECOMP_SAVE_ROOT=name+'/s', SNESRECOMP_WAV=str(folder / 'audio.wav'),
                   FZERO_TEST_APURAM_DUMP=str(folder / 'apu.bin'),
                   FZERO_TEST_DSP_DUMP=str(folder / 'dsp.bin'), SNESRECOMP_WRAM_DUMP=str(folder / 'ram.bin'))
        cmd = [str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), str(1750 if course is None else 1100)]
        if course is None:
            env.update(FZERO_CUP='max-league/max', FZERO_TEST_COURSE='3',
                       SNESRECOMP_INPUT_SCRIPT='320-326:8,440-446:8,560-566:8,730-736:8,790-796:8')
        else:
            cmd += ['--versus', str(course), '9', '3']
        proc = subprocess.run(cmd, cwd=out, env=env, capture_output=True, text=True, timeout=120)
        (folder / 'run.log').write_text(proc.stdout + proc.stderr, encoding='utf-8')
        assert proc.returncode == 0, (name, proc.returncode, proc.stderr[-1500:])
        apu, dsp = (folder / 'apu.bin').read_bytes(), (folder / 'dsp.bin').read_bytes()
        assert apu[0x7fe] == 8 + song, (name, 'wrong uploaded song', apu[0x7fe])
        voices = [dsp[i*16+8] for i in range(6)]  # Native music voices; engine/effects use the remaining pair.
        assert sum(v > 0 for v in voices) >= 2, (name, 'music voices silent', voices)
        assert dsp[0x0c] and dsp[0x1c], (name, 'music master volume is zero')
        with wave.open(str(folder / 'audio.wav')) as wav:
            rate = wav.getframerate()
            wav.setpos(max(0, wav.getnframes() - rate))
            samples = array('h', wav.readframes(rate))
        rms = (sum(v*v for v in samples) / len(samples)) ** .5
        assert rms > 100, (name, 'silent PCM', rms)
        result = dict(name=name, song=song, music_voice_envelopes=voices,
                      master=[dsp[0x0c], dsp[0x1c]], rms=round(rms, 2), passed=True)
        print(json.dumps(result), flush=True)
        return result

    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(run, cases))
    (out / 'report.json').write_text(json.dumps(results, indent=2), newline='\n')


if __name__ == '__main__':
    main()
