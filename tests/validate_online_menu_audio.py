"""Muted native-CPU/SPC integration for the independent online picker.

Run the real online reducer/bootstrap without a transport, so a network
handshake failure cannot hide an audio regression. Check first entry and the
live Change Race route, with SPC and synthetic MSU. This is not a wire test.
"""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'rom', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    a = p.parse_args()
    build, rom, out = (getattr(a, k).resolve() for k in ('build', 'rom', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    (out / 'empty').mkdir()
    (out / 'synthetic').mkdir()
    # Select the documented direct-prefix adapter without applying a guest
    # patch. An empty course catalog has no declared CGP soundtrack resolver.
    (out / 'synthetic/f-zero_msu1.ips').write_bytes(b'Test marker: VS must never apply a guest music patch')
    pcm = b'MSU1' + struct.pack('<I', 0) + struct.pack('<hh', 1000, -1000) * 4096
    for track in range(1, 65):
        (out / f'synthetic/test-{track}.pcm').write_bytes(pcm)
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'RNET_'))}
    for route in ('first', 'change'):
        images = {}
        for music in ('spc', 'msu'):
            folder = out / f'{route}-{music}'
            folder.mkdir()
            shutil.copytree(ROOT / 'assets', folder / 'assets')
            frames = 300 if route == 'first' else 700
            script = '' if route == 'first' else (
                '20-22:32776,40-42:8,60-62:8,380-382:8,390-392:32,400-402:32,410-412:8')
            env = dict(clean, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', FZERO_TEST_ONLINE_MENU='1',
                       FZERO_DELUXE_DATA='embedded', FZERO_CGP_CARS='0', FZERO_CGP_REBALANCE='0',
                       FZERO_BS_CARS='0', FZERO_BS_TRACKS='0', FZERO_RULES='', FZERO_PACK_LOADER='0',
                       FZERO_PACKS_DIR=str(out / 'empty'), FZERO_TRACK_PACKS='settings', SNESRECOMP_SAVE_ROOT='s',
                       SNESRECOMP_INPUT_SCRIPT=script, SNESRECOMP_MSU1='off' if music == 'spc' else str(out / 'synthetic/test'),
                       FZERO_TEST_DSP_DUMP=str(folder / 'dsp.bin'), FZERO_TEST_DSP_TRACE=str(folder / 'dsp.frames'),
                       FZERO_TEST_APURAM_DUMP=str(folder / 'apu.bin'), SNESRECOMP_WRAM_DUMP=str(folder / 'ram.bin'),
                       SNESRECOMP_WAV=str(folder / 'audio.wav'))
            with (folder / 'run.log').open('w') as log:
                result = subprocess.run([str(build / 'FZeroSNESRecompHeadless.exe'), str(rom), str(frames),
                                         '--versus', '0', '0', '1'], cwd=folder, env=env,
                                        stdout=log, stderr=log, timeout=120)
            text = (folder / 'run.log').read_text(errors='replace')
            assert result.returncode == 0, (route, music, text[-2500:])
            ram = (folder / 'ram.bin').read_bytes()
            assert ram[0x46] == 5 and ram[0x54:0x56] == bytes([0, 1]), 'Picker did not park the common menu bank'
            trace = (folder / 'dsp.frames').read_bytes()
            active = sum(sum(trace[f*128 + v*16 + 8] > 0 for v in range(6)) >= 2
                         for f in range(len(trace)//128))
            assert active > 20, (route, music, 'native selection music voices are silent', active)
            if route == 'change':
                assert 'phase=5 ' in text and 'owner=1 ' in text, 'Live menu/real race path was not exercised'
                assert text.count('phase=1 ') >= 2, 'Did not return to the selector'
            if music == 'msu':
                assert 'command=5 ' in text and 'output=PCM' in text, 'MSU menu track was not resolved'
                with wave.open(str(folder / 'audio.wav')) as audio:
                    data = audio.readframes(audio.getnframes())
                samples = struct.unpack('<%dh' % (len(data)//2), data)[-4096:]
                assert sum(samples[::2])/2048 > 700 and sum(samples[1::2])/2048 < -700, 'Synthetic MSU signature missing'
            images[music] = ((folder/'ram.bin').read_bytes(), (folder/'apu.bin').read_bytes(), (folder/'dsp.bin').read_bytes())
        assert images['spc'] == images['msu'], 'Local music choice changed canonical RAM/APU/DSP state'
        print(f'PASS {route}: native menu program, active SPC voices, synthetic MSU, identical canonical state', flush=True)


if __name__ == '__main__':
    main()
