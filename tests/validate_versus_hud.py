"""Muted real-course HUD/live-menu/audio and current-build restore checks.

The driving script uses pad inputs only. This is a short driving smoke check,
not proof of complete courses or a substitute for a reporter's audio sample.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'packs', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--courses', default='0,15,59')
    a = p.parse_args()
    build, rom, packs, out = (getattr(a, key).resolve() for key in ('build', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'RNET_', 'SNES_NET_', 'LNG_'))}
    report = []
    for course in map(int, a.courses.split(',')):
        folder = out / str(course)
        folder.mkdir()
        for assets in ('vehicle-packs', 'track-packs'):
            shutil.copytree(ROOT / 'assets' / assets, folder / 'assets' / assets)
        env = dict(clean, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', FZERO_DELUXE_DATA='embedded',
                   FZERO_PACK_LOADER='1', FZERO_PACKS_DIR=str(packs), FZERO_TRACK_PACKS='settings',
                   FZERO_BS_TRACKS='0', FZERO_BS_CARS='0', SNESRECOMP_SAVE_ROOT='s', SNESRECOMP_MSU1='off',
                   FZERO_TEST_FEEDBACK=str(folder),
                   SNESRECOMP_INPUT_SCRIPT='550-1099:4097,920-935:4161,980-995:8194',
                   FZERO_LIFECYCLE_TEST='1', FZERO_REWIND_TEST='1', FZERO_TEST_SAVE_FRAME='900',
                   SNESRECOMP_WRAM_DUMP=str(folder / 'native.bin'), FZERO_TEST_WORLD_DUMP=str(folder / 'world.bin'),
                   FZERO_TEST_DSP_TRACE=str(folder / 'dsp.frames'), SNESRECOMP_WAV=str(folder / 'audio.wav'))
        with (folder / 'run.log').open('w') as log:
            run = subprocess.run([str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), '1100',
                                  '--versus', str(course), '0', '1'], cwd=folder, env=env,
                                 stdout=log, stderr=log, timeout=180)
        text = (folder / 'run.log').read_text(errors='replace')
        assert run.returncode == 0, (course, run.returncode, text[-3500:])
        for expected in ('feedback-hud: PASS', 'lifecycle: save/load ten-frame resimulation identical',
                         'rewind: actual ring restore and ten-frame resimulation identical'):
            assert expected in text, (course, expected, text[-3500:])
        rows = list(csv.DictReader((folder / 'sound.csv').open()))
        arrival = [r for r in rows if int(r['countdown']) > 180]
        # In-port readback follows native SPC advancement, not just requested
        # commands. Exclude the handoff tick while a prior packet is pending.
        assert len(arrival) >= 60
        assert all(int(r['port1']) == 3 for r in arrival[2:]), (course, 'arrival engine command')
        racing = [r for r in rows if r['phase'] == '2' and r['p1_status'] == r['p2_status'] == '0']
        assert len(racing) >= 60, (course, 'insufficient active driving')
        audible = sum(int(r['env6']) > 0 or int(r['env7']) > 0 for r in racing)
        assert audible >= 30, (course, 'no native engine/effect voices during driving')
        report.append(dict(course=course, hud_and_operator_menu=True, native_arrival=True,
                           active_driving_frames=len(racing), engine_or_effect_active_frames=audible,
                           current_build_save_load_rewind=True))
        print('PASS', report[-1], flush=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
