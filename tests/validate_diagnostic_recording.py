"""Private, muted capture/replay and post-frame divergence gate for playtests."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'packs', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--capture', type=Path, help='Validate an existing capture, including either online seat')
    p.add_argument('--single', action='store_true', help='Capture ordinary native single-player instead of local VS')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    build, rom, packs, out = (getattr(a, key).resolve() for key in ('build', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'SNES_RB', 'RNET_'))}
    clean.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
    clean['PATH'] = str(build) + os.pathsep + clean.get('PATH', '')
    if a.capture:
        capture = a.capture.resolve()
    else:
        (out / 'diagnostics').mkdir()
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='0', FZERO_BS_CARS='0',
                   FZERO_BS_TRACKS='0', FZERO_RULES='', FZERO_PACK_LOADER='1',
                   FZERO_PACKS_DIR=str(packs), FZERO_TRACK_PACKS=str(out / 'settings'),
                   FZERO_CAPTURE_DIR=str(out / 'diagnostics'), SNESRECOMP_SAVE_ROOT='s',
                   SNESRECOMP_WRAM_DUMP=str(out / 'original.ram'),
                   SNESRECOMP_INPUT_SCRIPT='1000-1249:4097')
        command = [str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), '1550' if a.single else '1250']
        if a.single:
            env['SNESRECOMP_INPUT_SCRIPT'] = '320-326:8,440-446:8,560-566:8,730-736:8,790-796:8,1150-1549:1'
        else:
            command += ['--versus', '15', '9', '3']
        with (out / 'capture.log').open('w') as log:
            r = subprocess.run(command, cwd=build, env=env,
                               stdout=log, stderr=log, timeout=120)
        assert r.returncode == 0, 'capture run failed'
        capture = next((out / 'diagnostics').glob('replay-*'))
    meta = json.loads((capture / 'session.json').read_text())
    assert meta['format'] == 2, meta
    routes = sorted(capture.glob('segment-*.inputs'))
    assert routes and all(len(r.read_text().splitlines()) == len(r.with_suffix('.frames').read_text().splitlines()) for r in routes), 'incomplete per-frame checks'

    def replay(label, source, expected):
        cmd = [sys.executable, str(root / 'tools/replay_diagnostics.py'), str(source),
               '--build', str(build), '--rom', str(rom), '--packs', str(packs), '--out', str(out / label)]
        with (out / (label + '.log')).open('w') as log:
            r = subprocess.run(cmd, cwd=root, env=clean, stdout=log, stderr=log, timeout=180)
        assert r.returncode == expected, (label, r.returncode, (out / (label + '.log')).read_text()[-2000:])
        return json.loads((out / label / 'result.json').read_text())

    result = replay('matched', capture, 0)
    if not a.capture:
        assert (out / 'original.ram').read_bytes() == (out / 'matched/final.ram').read_bytes(), 'final WRAM mismatch'
    tampered = out / 'tampered'
    shutil.copytree(capture, tampered)
    checks = sorted(tampered.glob('segment-*.frames'))[-1]
    lines = checks.read_text().splitlines()
    row = lines[0].split()
    row[2] = '%08x' % (int(row[2], 16) ^ 1)
    lines[0] = ' '.join(row)
    checks.write_text('\n'.join(lines) + '\n')
    bad = replay('rejected', tampered, 1)
    assert bad['diverged'], bad
    report = dict(mode=meta['mode'], online_view=meta.get('online_view'),
                  segments=len(routes), replay_frames=result['frames'], post_frame_tamper_rejected=True, passed=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    main()
