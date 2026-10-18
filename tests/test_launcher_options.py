"""Exercise F-Zero's real Skip Launcher control and boot recovery.

Uses recomp-ui's SDL input script, not a prewritten SkipLauncher setting.
Run under xvfb-run on Linux, or on a Windows desktop. Requires a private ROM.
Copies the executable/assets to a fresh output directory; leaves evidence there.
"""
import argparse
import configparser
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--appimage-layout', action='store_true',
                        help='Exercise AppImage state paths outside its mounted executable directory')
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    stage = output / 'install'
    stage.mkdir()
    state = output / 'portable' if args.appimage_layout else stage
    state.mkdir(exist_ok=True)
    name = 'F-Zero Forever' + ('.exe' if os.name == 'nt' else '')
    shutil.copy2(source / name, stage / name)
    shutil.copytree(source / 'assets', stage / 'assets')
    for dll in source.glob('*.dll'):
        shutil.copy2(dll, stage / dll.name)
    library = output / 'ROM Library'
    library.mkdir()
    rom = library / 'F-Zero (USA).sfc'
    shutil.copy2(args.rom, rom)
    cache = state / 'rom.cfg'
    cache.write_text(str(rom) + '\n')
    (state / 'fzero-video.ini').write_text('HDMode7=0\nPresentationFPS=60\nBSDeluxe=0\n')
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('LNG_', 'FZERO_', 'SNESRECOMP_', 'SDL_'))}
    env.update(SDL_AUDIODRIVER='dummy', SNESRECOMP_AUTOCLOSE_FRAMES='120',
               SNESRECOMP_SAVE_ROOT='saves')
    if os.name != 'nt':
        env.pop('WAYLAND_DISPLAY', None)
        env['SDL_VIDEODRIVER'] = 'x11'
    if args.appimage_layout:
        assert os.name != 'nt'
        env.update(APPIMAGE=str(state / 'FZero.AppImage'),
                   FZERO_VIDEO_CONFIG=str(state / 'fzero-video.ini'))

    def run(label, script='quit', arguments=(), game=False):
        # An unrelated cwd also checks executable-relative config/cache lookup.
        with (output / f'{label}.log').open('w') as log:
            result = subprocess.run([str(stage / name), *map(str, arguments)],
                cwd=output, env=dict(env, LNG_SCRIPT=f'size:1100x880;wait:20;{script}'),
                stdout=log, stderr=log, timeout=40)
        text = (output / f'{label}.log').read_text(errors='replace')
        assert result.returncode == 0, (label, text)
        assert ('simulation=120' in text) == game, (label, text)
        print(f'{label}: passed', flush=True)
        return text

    def saved_skip():
        ini = configparser.ConfigParser()
        ini.read(state / 'config.ini')
        return ini.getint('General', 'SkipLauncher')

    enable = 'click:40,820;wait:8;click:610,166;wait:8;'
    run('enable-and-quit', enable + 'shot:skip-enabled.png;quit')
    assert saved_skip() == 1
    text = run('skip-on-relaunch', game=True)
    assert 'skipped: verified' in text and '[fzero-launcher] opening' not in text
    for label, arguments in [('force', ('--launcher',)),
                             ('force-before-rom', ('--launcher', rom)),
                             ('force-after-rom', (rom, '--launcher'))]:
        text = run(label, 'shot:forced.png;quit', arguments)
        assert 'opening (--launcher)' in text and saved_skip() == 1
    run('disable-and-quit', 'click:40,820;wait:8;quit', ('--launcher',))
    assert saved_skip() == 0
    assert '[fzero-launcher] opening' in run('normal-on-relaunch')
    run('enable-and-play', enable + 'click:970,820;wait:20;quit', game=True)
    assert saved_skip() == 1
    cache.unlink()
    assert '[fzero-launcher] opening' in run('missing-cache')
    cache.write_text(str(library / 'missing.sfc') + '\n')
    assert 'unavailable or invalid' in run('missing-rom')
    invalid = library / 'invalid.sfc'
    invalid.write_bytes(bytes(0x80000))
    cache.write_text(str(invalid) + '\n')
    assert 'unavailable or invalid' in run('invalid-rom')
    cache.write_text('../ROM Library/F-Zero (USA).sfc\n')
    assert 'skipped: verified' in run('relative-cached-rom', game=True)
    assert saved_skip() == 1
    run('type-hd-scale',
        'click:1020,46;wait:8;click:170,410;wait:8;'
        'click:700,442;wait:3;key:Home;key:Delete;text:10;wait:3;click:950,600;wait:10;'
        'shot:hd-10-warning.png;quit', ('--launcher',))
    ini = configparser.ConfigParser()
    ini.read(state / 'fzero-video.ini')
    assert ini.getint('FZeroVideo', 'HDMode7Scale') == 10
    run('hd-scale-relaunch',
        'click:1020,46;wait:8;click:170,410;wait:8;shot:hd-10-relaunch.png;quit',
        ('--launcher',))
    if args.appimage_layout:
        assert not (stage / 'config.ini').exists(), 'Settings leaked into the mounted binary directory'
    print('All launcher persistence and recovery checks passed.', flush=True)


if __name__ == '__main__':
    main()
