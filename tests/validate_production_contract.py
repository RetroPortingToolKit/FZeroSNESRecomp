"""Exercise the shipped desktop/headless ROM gates and Windows subsystem.

Creates only isolated private-ROM fixtures. No speaker output or owner config.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--rom', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    build, out = a.build.resolve(), a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    rom = a.rom.read_bytes()
    if len(rom) == 0x80200:
        rom = rom[512:]
    assert hashlib.sha256(rom).hexdigest() == 'bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2'
    exe = build / 'F-Zero Forever.exe'
    image = exe.read_bytes()
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    assert image[pe:pe+4] == b'PE\0\0'
    assert struct.unpack_from('<H', image, pe+24+68)[0] == 2, 'Desktop must use Windows GUI subsystem'
    stage = out / 'install'
    stage.mkdir()
    shutil.copy2(exe, stage / exe.name)
    shutil.copy2(build / 'FZeroSNESRecompHeadless.exe', stage)
    shutil.copytree(build / 'assets', stage / 'assets')
    for dll in build.glob('*.dll'):
        shutil.copy2(dll, stage)
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'LNG_', 'SDL_', 'SNES_NET_', 'RNET_'))}
    clean.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                 FZERO_TEST_LOCAL_UI_HIDDEN='1', SNESRECOMP_AUTOCLOSE_FRAMES='120',
                 SNESRECOMP_SAVE_ROOT='s', FZERO_PACK_LOADER='0')
    fixtures = {'raw': rom, 'headered': bytes(range(256))*2+rom,
                'wrong-hash': rom[:100]+bytes([rom[100]^1])+rom[101:],
                'truncated': rom[:-1], 'oversized': rom+bytes(1024), 'empty': b''}
    results = []
    for name, data in fixtures.items():
        path = out / (name+'.sfc')
        path.write_bytes(data)
        for desktop in (True, False):
            program = exe.name if desktop else 'FZeroSNESRecompHeadless.exe'
            args = [str(stage/program), str(path)] + ([] if desktop else ['120'])
            proc = subprocess.run(args, cwd=stage, env=clean, capture_output=True, timeout=45)
            (out / f'{name}-{desktop}.log').write_bytes(proc.stdout+proc.stderr)
            assert proc.returncode == (0 if name in ('raw', 'headered') else 2), (name, desktop, proc.returncode, proc.stderr[-1500:])
            results.append(dict(rom=name, desktop=desktop, code=proc.returncode))
    # Fresh launcher persists its default without playing or touching owner settings.
    env = dict(clean, SDL_VIDEODRIVER='windows', LNG_SCRIPT='wait:20;quit')
    proc = subprocess.run([str(stage/exe.name), '--launcher'], cwd=stage,
                          env=env, capture_output=True, timeout=45)
    assert proc.returncode == 0, proc.stderr[-1500:]
    import configparser
    config = configparser.ConfigParser()
    config.read(stage/'config.ini')
    assert config.getint('Sound', 'Msu1Enabled') == 1
    video = configparser.ConfigParser()
    video.optionxform = str
    video.read(stage/'fzero-video.ini')
    assert video.getint('FZeroVideo', 'LocalTwoPlayer') == 1
    assert video.getint('FZeroVideo', 'CGPStockRebalance') == 15
    assert video.getint('FZeroVideo', 'BSVehicles') == 0
    video['FZeroVideo']['LocalTwoPlayer'] = '0'
    video['FZeroVideo']['CGPStockRebalance'] = '0'
    with (stage/'fzero-video.ini').open('w') as f:
        video.write(f)
    config['Sound']['Msu1Enabled'] = '0'
    with (stage/'config.ini').open('w') as f:
        config.write(f)
    proc = subprocess.run([str(stage/exe.name), '--launcher'], cwd=stage,
                          env=env, capture_output=True, timeout=45)
    assert proc.returncode == 0
    config.read(stage/'config.ini')
    assert config.getint('Sound', 'Msu1Enabled') == 0, 'Explicit opt-out must persist'
    video.read(stage/'fzero-video.ini')
    assert video.getint('FZeroVideo', 'LocalTwoPlayer') == 0, 'VS opt-out must persist'
    assert video.getint('FZeroVideo', 'CGPStockRebalance') == 0, 'Rebalance opt-out must persist'
    (out/'report.json').write_text(json.dumps(results, indent=2))
    print('PASS: GUI subsystem; raw/headered ROM accepted; corrupt/short/long/empty ROM refused; MSU/CGP rebalances/VS defaults and opt-outs')


if __name__ == '__main__':
    main()
