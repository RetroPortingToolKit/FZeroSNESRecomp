"""Qualify VS's twelve identities against private ROM/native 1P captures.

Builds private profile fixtures from the shipped reviewed art IPS and generated
author-ASM writes. No donor ROM, derived image or captured artwork is tracked.
All launches are muted. Requires Pillow and the Windows oracle/headless build.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_bs_deluxe import apply_ips
from validate_native_menus import player_route


def profiles(stock, out):
    assert hashlib.sha256(stock).hexdigest() == 'bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2'
    text = (ROOT / 'src/fzero_gameplay_patches.inc').read_text()
    data = {name: bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]+)', body))
            for name, body in re.findall(r'static const uint8_t (rule_\w+)\[\] = \{(.*?)\};', text, re.S)}
    for group in (1, 2, 3):
        image = bytearray(apply_ips(stock, (ROOT / f'assets/vehicle-packs/cgp-p{group}.ips').read_bytes()))
        image.extend(bytes(0x400000 - len(image)))
        for rule in (group - 1, group + 2, group + 5):
            writes = re.search(r'static const RuleWrite rule_0_%d\[\] = \{(.*?)\};' % rule, text, re.S)
            assert writes, rule
            for offset, size, name in re.findall(r'\{(0x[0-9a-fA-F]+|\d+),\s*(0x[0-9a-fA-F]+|\d+),\s*(\w+)\s*\}', writes[1]):
                offset, size = int(offset, 0), int(size, 0)
                assert len(data[name]) == size and offset + size <= len(image)
                image[offset:offset + size] = data[name]
        (out / f'profile{group}.sfc').write_bytes(image)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    a = p.parse_args()
    build, rom, out = (getattr(a, k).resolve() for k in ('build', 'rom', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    profiles(rom.read_bytes(), out)
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'SNES_NET_', 'SNES_RB', 'RNET_', 'LNG_'))}
    clean.update(SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy')
    env = dict(clean, FZERO_TEST_CGP_PROFILE_DIR=str(out), FZERO_TEST_EXPANDED_SELECTOR_DUMP=str(out))
    oracle = subprocess.run([str(build / 'fzero_actor_oracle.exe'), str(rom)], env=env,
                            capture_output=True, text=True, timeout=180)
    (out / 'oracle.log').write_text(oracle.stdout + oracle.stderr, encoding='utf-8')
    assert oracle.returncode == 0, (oracle.stdout[-1000:], oracle.stderr[-3000:])
    print('all twelve physics, mixed collisions, save/rematch, body/effect and spin oracles PASS', flush=True)
    report = {}

    def compare(identity):
        folder = out / str(identity)
        folder.mkdir()
        shutil.copytree(ROOT / 'assets', folder / 'assets')
        env = dict(clean, FZERO_DELUXE_DATA='embedded', FZERO_BS_CARS='0', FZERO_BS_TRACKS='0',
                   FZERO_CGP_CARS='7', FZERO_CGP_REBALANCE='15', FZERO_RULES='',
                   FZERO_TRACK_PACKS='packs', SNESRECOMP_SAVE_ROOT='s',
                   SNESRECOMP_INPUT_SCRIPT=player_route(identity) + ',600-606:8',
                   SNESRECOMP_FRAME_DUMP=str(folder / 'native.ppm'),
                   SNESRECOMP_WRAM_DUMP=str(folder / 'native.bin'))
        proc = subprocess.run([str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), '800'],
                              cwd=folder, env=env, capture_output=True, text=True, timeout=90)
        (folder / 'native.log').write_text(proc.stdout + proc.stderr, encoding='utf-8')
        assert proc.returncode == 0, (identity, proc.stderr[-2000:])
        ram = (folder / 'native.bin').read_bytes()
        assert ram[0x14dff] == identity and ram[0x54:0x57] == bytes([1, 1, 1]), identity
        actual = Image.open(out / f'expanded{identity}.ppm').convert('RGB')
        native = Image.open(folder / 'native.ppm').convert('RGB')
        # The existing Deluxe menu uses its shared small label spacing and
        # different text/curve colors. Compare the authored numerical/graph
        # silhouettes and the exact identity-specific frame colors instead.
        for box in ((176, 56, 232, 112), (120, 136, 224, 176)):
            lhs = actual.crop(box).convert('L').point(lambda v: 255 if v > 25 else 0)
            rhs = native.crop(box).convert('L').point(lambda v: 255 if v > 25 else 0)
            diff = ImageChops.difference(lhs, rhs)
            diff.save(folder / ('difference-%d.png' % box[1]))
            assert diff.getbbox() is None, (identity, 'authored card silhouette', box, diff.getbbox())
        box = (106, 48, 112, 176)
        assert ImageChops.difference(actual.crop(box), native.crop(box)).getbbox() is None, (identity, 'frame colors')
        actual.save(folder / 'versus.png')
        native.save(folder / 'native.png')
        report[identity] = dict(native_values=True, native_graph=True, native_frame_colors=True)
        print('machine', identity, 'native card PASS', flush=True)

    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(compare, range(12)))
    (out / 'report.json').write_text(json.dumps(report, indent=2), newline='\n')


if __name__ == '__main__':
    main()
