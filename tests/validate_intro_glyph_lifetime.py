"""Private-ROM check: imported Z survives its last visible OBJ frame.

Also verify the GP spare-car icon uses the selected Red Gazelle's native art.
All captures are private and launches use dummy audio; no course outcomes or
guest physics state are injected.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from validate_native_menus import player_route

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_bs_deluxe import apply_ips
VRAM = 224 * 1120


def native_tile(rom, tile):
    sizes = (32, 8, 16, 24)
    pos = 0x78000
    while pos + 2 <= len(rom):
        header, first = rom[pos:pos+2]
        pos += 2
        count, mode = header & 63, header >> 6
        if not count or first + count > 256:
            break
        if mode == 2 and first <= tile < first + count:
            return rom[pos+(tile-first)*16:pos+(tile-first+1)*16] + bytes(16)
        pos += count * sizes[mode]
    raise AssertionError('Native letter atlas missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'stock', 'packs', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    a = parser.parse_args()
    build, stock, packs, out = (getattr(a, k).resolve() for k in ('build', 'stock', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    shutil.copytree(ROOT / 'assets', out / 'assets')
    shutil.copytree(packs / 'astra-front', out / 'catalog/astra-front', ignore=shutil.ignore_patterns('music', '.cache'))
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'RNET_'))}
    env = dict(clean, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', SNESRECOMP_MSU1='off',
               FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='7', FZERO_CGP_REBALANCE='15', FZERO_BS_CARS='0',
               FZERO_PACK_LOADER='1', FZERO_PACKS_DIR=str(out / 'catalog'), FZERO_TRACK_PACKS='settings',
               SNESRECOMP_SAVE_ROOT='s', FZERO_CUP='astra-front/astra',
               SNESRECOMP_INPUT_SCRIPT=player_route(11) + ',600-606:8,730-736:8,850-856:8,940-946:8')

    def run(name, frames):
        folder = out / name
        folder.mkdir()
        e = dict(env, FZERO_CAPTURE_FRAMES=','.join(map(str, frames)), FZERO_CAPTURE_PREFIX=str(folder / 'frame'))
        with (folder / 'run.log').open('w') as log:
            p = subprocess.run([str(build / 'FZeroSNESRecompHeadless.exe'), str(stock), str(max(frames) + 2)],
                               cwd=out, env=e, stdout=log, stderr=log, timeout=240)
        assert p.returncode == 0, (name, p.returncode)
        return folder

    def glyph_visible(meta):
        return meta['state'][0] == 2 and any(
            (attr & 511) in (0x1a7, 0x1b7) and x < 256 and y < 160
            for _, x, y, attr, _ in meta['oam'])

    def tile_bytes(data, code):
        word = 0x5000 + code * 16
        return data[VRAM + word * 2:VRAM + word * 2 + 32]

    probe = run('probe', range(900, 1801, 8))
    visible = []
    for path in sorted(probe.glob('*.json')):
        meta = json.loads(path.read_text())
        if glyph_visible(meta):
            visible.append((meta['frame'], path.with_suffix('.bin')))
    assert len(visible) >= 3, 'Imported Z intro was not exercised'
    first = visible[0][1].read_bytes()
    expected = [tile_bytes(first, code) for code in (0xa7, 0xb7)]
    rom = stock.read_bytes()
    assert expected[0] != native_tile(rom, 0xa7), 'Did not capture the imported Z'
    last = visible[-1][0]
    tail = run('last-frames', range(last - 8, last + 25))
    seen, gone = 0, False
    art = apply_ips(rom, (ROOT / 'assets/vehicle-packs/cgp-p3.ips').read_bytes())
    icon = art[0x40000 + 2 * 0x8000 + 0x6aa0:][:128]
    icon_seen = False
    for path in sorted(tail.glob('*.json')):
        meta = json.loads(path.read_text())
        data = path.with_suffix('.bin').read_bytes()
        if glyph_visible(meta):
            assert [tile_bytes(data, code) for code in (0xa7, 0xb7)] == expected, \
                (meta['frame'], 'Z changed while its OBJ remained visible')
            seen += 1
        elif meta['frame'] > last:
            gone = True
        if any(slot in (22, 126) and x < 256 and y < 224 and (attr & 511) == 0x1c8
               for slot, x, y, attr, _ in meta['oam']):
            for tile in range(4):
                word = 0x5c80 + (tile // 2) * 256 + (tile % 2) * 16
                assert data[VRAM + word * 2:VRAM + word * 2 + 32] == icon[tile*32:(tile+1)*32], \
                    (meta['frame'], 'GP spare-car icon is not Red Gazelle')
            icon_seen = True
    assert seen >= 3 and gone and icon_seen, (seen, gone, icon_seen)
    print(f'PASS imported Z across {seen} final visible frames and disappearance; selected GP car icon', flush=True)


if __name__ == '__main__':
    main()
