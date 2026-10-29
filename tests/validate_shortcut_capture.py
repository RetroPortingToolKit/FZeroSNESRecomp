"""Hidden real-launcher shortcut capture/persistence with native SNES binds.

Inject SDL events through recomp-ui's existing opt-in script driver. Never
touch the owner's installation, controller profiles, saves or desktop focus.
"""
import argparse
import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    build, out = args.build.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    stage = out / 'install'
    stage.mkdir()
    shutil.copy2(build / 'F-Zero Forever.exe', stage)
    for dll in build.glob('*.dll'):
        shutil.copy2(dll, stage)
    root = Path(__file__).resolve().parents[1]
    shutil.copytree(build / 'assets', stage / 'assets')
    if (root / 'assets').is_dir():
        shutil.copytree(root / 'assets', stage / 'assets', dirs_exist_ok=True)
    (stage / 'config.ini').write_text('[General]\nSkipLauncher=0\n[Graphics]\nFullscreen=0\n'
                                   '[GamepadMap]\nControls=DpadUp,DpadDown,DpadLeft,DpadRight,Back,Start,B,A,Y,X,Lb,Rb\n', encoding='utf-8')
    (stage / 'keybinds.ini').write_text('[player1]\nup=Up\ndown=Down\n[player2]\nup=W\n', encoding='utf-8')
    (stage / 'fzero-video.ini').write_text('[FZeroVideo]\nBSTracks=0\nBSVehicles=0\nCrashReplay=0\n', encoding='utf-8')
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'RNET_', 'SNES_NET_', 'LNG_'))}

    def run(name, commands):
        env = dict(clean, SDL_AUDIODRIVER='dummy', LNG_TEST_HIDDEN='1',
                   LNG_SCRIPT=';'.join(commands), FZERO_TRACK_PACKS=str(out / 'settings'),
                   FZERO_PACKS_DIR=str(out / 'empty-packs'), SNESRECOMP_SAVE_ROOT=str(out / 'saves'))
        with (out / (name + '.log')).open('w', encoding='utf-8') as log:
            proc = subprocess.run([str(stage / 'F-Zero Forever.exe'), '--launcher'], cwd=stage,
                                  env=env, stdout=log, stderr=log, timeout=90)
        text = (out / (name + '.log')).read_text(encoding='utf-8', errors='replace')
        assert proc.returncode == 0, (proc.returncode, text[-3500:])
        config = configparser.ConfigParser()
        config.read(stage / 'config.ini', encoding='utf-8')
        keys = configparser.ConfigParser()
        keys.read(stage / 'keybinds.ini', encoding='utf-8')
        assert keys['player1']['up'] == 'Up' and keys['player2']['up'] == 'W'
        assert config['GamepadMap']['Controls'].split(',')[0].strip().lower() == 'dpadup'
        return {key: int(config['SaveStates'][key]) for key in
                ('QuickSaveKey', 'QuickLoadKey', 'QuickSavePad', 'QuickLoadPad')}

    bound = run('bind', ['view:controller', 'wait:3', 'capassist:0,1', 'padbutton:2', 'wait:2',
                         'capassist:1,1', 'padaxis:4,25000', 'wait:2',
                         'capassist:0,0', 'key:F5', 'wait:2', 'capassist:1,0', 'key:F6', 'wait:2',
                         'shot:' + str(out / 'bound.png'), 'quit'])
    assert bound == dict(QuickSaveKey=62, QuickLoadKey=63, QuickSavePad=3, QuickLoadPad=109), bound
    assert run('reopen', ['view:controller', 'wait:3', 'quit']) == bound
    cleared = run('clear', ['view:controller', 'capassist:0,1', 'key:escape', 'wait:2',
                           'capassist:1,0', 'key:escape', 'wait:2',
                           'capassist:0,1', 'key:backspace', 'wait:2',
                           'capassist:1,0', 'key:backspace', 'wait:2', 'quit'])
    assert cleared == dict(QuickSaveKey=62, QuickLoadKey=0, QuickSavePad=0, QuickLoadPad=109), cleared
    report = dict(real_sdl_button_axis_keyboard_capture=True, reopen_persistence=True,
                  escape_and_backspace=True, native_keyboard_and_pad_up_unchanged=True,
                  bound=bound, cleared=cleared, passed=True)
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS', report, flush=True)


if __name__ == '__main__':
    main()
