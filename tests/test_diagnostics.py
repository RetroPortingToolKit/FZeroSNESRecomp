"""Exercise opt-in logs in the actual desktop host, with a private ROM.

Uses SDL's dummy backend; checks artifacts and guest state, not GPU performance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--appimage-layout', action='store_true')
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    name = 'F-Zero Forever' + ('.exe' if os.name == 'nt' else '')
    hashes = []
    for label, enabled in [('default-off', None), ('enabled', 1), ('disabled', 0), ('unwritable', 1)]:
        folder = output / label
        folder.mkdir()
        stage = folder / 'install'
        stage.mkdir()
        shutil.copy2(source / name, stage / name)
        for dll in source.glob('*.dll'):
            shutil.copy2(dll, stage / dll.name)
        state = folder / 'portable' if args.appimage_layout else stage
        state.mkdir(exist_ok=True)
        video = state / 'fzero-video.ini'
        video.write_text('[FZeroVideo]\nEnhancedRenderer=1\nAspect=16:9\n'
                         'HDMode7=1\nHDMode7Scale=4\nBSDeluxe=0\n'
                         'PresentationEnabled=1\nPresentationFPS=60\nCrashReplay=0\n' +
                         ('' if enabled is None else f'Diagnostics={enabled}\n'))
        (state / 'config.ini').write_text('[Rewind]\nEnabled=0\n')
        diagnostics = state / 'diagnostics'
        if label == 'unwritable':
            diagnostics.write_text('Existing file must survive a failed directory creation.')
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
        env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                   SNESRECOMP_AUTOCLOSE_FRAMES='240',
                   SNESRECOMP_SAVE_ROOT=tempfile.mkdtemp(prefix='fzdiag-save-'),
                   SNESRECOMP_WRAM_DUMP=str(folder / 'wram.bin'),
                   FZERO_VIDEO_CONFIG=str(video))
        if args.appimage_layout:
            assert os.name != 'nt'
            env['APPIMAGE'] = str(state / 'FZero.AppImage')
        with (folder / 'run.log').open('w') as log:
            result = subprocess.run([str(stage / name), str(args.rom.resolve())],
                                    cwd=output, env=env, stdout=log, stderr=log, timeout=40)
        text = (folder / 'run.log').read_text(errors='replace')
        assert result.returncode == 0 and 'simulation=240' in text, (label, text)
        hashes.append(hashlib.sha256((folder / 'wram.bin').read_bytes()).hexdigest())
        if not enabled:
            assert not diagnostics.exists(), label
        elif label == 'unwritable':
            assert diagnostics.is_file() and 'Existing file' in diagnostics.read_text()
            assert 'Cannot create log' in text
        else:
            logs = list(diagnostics.glob('performance-*.jsonl'))
            assert len(logs) == 1
            records = [json.loads(line) for line in logs[0].read_text().splitlines()]
            header = records[0]
            assert header['kind'] == 'session' and header['schema'] == 1
            assert header['allocated_scale'] == 4 and header['backend'] == 'software'
            assert header['version'] and header['revision'] and header['compiler']
            assert header['logical_cpus'] > 0 and header['ram_mb'] > 0
            assert records[1]['kind'] == 'settings'
            samples = [r for r in records if r['kind'] in ('settings', 'sample', 'final')]
            assert any(r['kind'] == 'sample' for r in samples)
            assert samples[-1]['kind'] == 'final' and samples[-1]['simulation_total'] == 240
            assert sum(r['simulation_delta'] for r in samples) == 240
            assert sum(r['presentations_delta'] for r in samples) == samples[-1]['presentations_total']
            assert sum(r['stages']['simulation']['calls'] for r in samples) == 240
            assert sum(r['stages']['composition']['calls'] for r in samples) == samples[-1]['presentations_total']
            for sample in samples:
                assert sample['requested_scale'] == 4 and sample['hd_enabled'] == 1
                assert sample['target_hz'] == 60 and sample['aspect'] == '16:9'
                assert sample['effective_scale'] in (1, 4)  # before the first draw, no HD frame exists
                assert sample['source_width'] == 342 * sample['effective_scale']
                assert sample['unattributed_ms'] >= 0
                for timing in sample['stages'].values():
                    assert 0 <= timing['mean_ms'] <= timing['max_ms'] + 0.001
            assert samples[-1]['effective_scale'] == 4
            assert samples[-1]['stages']['composition']['total_ms'] > 0
            assert str(args.rom.resolve()) not in logs[0].read_text()
        if args.appimage_layout:
            assert not (stage / 'diagnostics').exists()
        print(f'{label}: passed', flush=True)
    assert len(set(hashes)) == 1, 'Diagnostics changed guest state'
    print('Logging on/off/failure produce identical guest state.', flush=True)


if __name__ == '__main__':
    main()
