"""Offline multiplayer human checks using a staged, disposable installation."""
import argparse
import configparser
import json
import os
from pathlib import Path
import subprocess
import tkinter as tk
from tkinter import ttk

CASES = (
    ("4P: race and menus", 4,
     "Choose machines (duplicates are allowed), then a course. Check all four views/HUDs. "
     "Drive a race, test contact/boost/crash and finish/results. Start opens a live menu while "
     "other racers keep moving. Try Restart, Change Race and Main Menu."),
    ("4P: input groups and device recovery", 4,
     "Use two keyboard groups or a pad plus keyboard to make racers do different things. "
     "Only the assigned racers should respond to each input group. If using a pad, unplug/reconnect "
     "it during the race: its input should become neutral while other racers continue, then recover "
     "the same seat. Mirrored keyboard control alone does not validate independent physical pads."),
    ("3P: presentation and content", 3,
     "Check the unused fourth cell is black. Resize tall/wide and check cameras, sprites, labels "
     "and HUDs. Try BS and custom courses, music/effects and a current-build save/load. "
     "Before readying, P1 can press S to choose 2, 3 or 4 players; two players use top/bottom views."),
)
# Number of pad seats, then keyboard layout for each seat. Repeated layouts
# deliberately let one person steer several racers with the same physical keys.
MODES = {
    "Keyboard controls everyone": (0, (0, 0, 0, 0)),
    "Two keyboard groups (P1/P3 and P2/P4)": (0, (0, 1, 0, 1)),
    "1 gamepad + keyboard for the other racers": (1, (0, 0, 0, 0)),
    "2 gamepads + keyboard for the other racers": (2, (0, 0, 0, 0)),
    "Four gamepads": (4, (0, 0, 0, 0)),
}
LAYOUTS = (
    dict(a='X', b='Z', x='S', y='A', l='D', r='C', start='Return', select='Right Shift',
         up='Up', down='Down', left='Left', right='Right'),
    dict(a='G', b='F', x='H', y='T', l='U', r='O', start='Space', select='Tab',
         up='I', down='K', left='J', right='L'),
)
KEY_HELP = ('Keyboard group 1: arrows, Z accelerate, X boost, D/C lean, Enter ready/menu, '
            'S player count before Ready. Group 2: I/J/K/L, F accelerate, G boost, U/O lean, Space ready/menu.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--install', type=Path, required=True)
    p.add_argument('--rom', type=Path, required=True)
    p.add_argument('--start', action='store_true', help='Open the first setup in its frozen picker')
    a = p.parse_args()
    install, rom = a.install.resolve(), a.rom.resolve()
    exe = install / 'F-Zero Forever.exe'
    if not exe.is_file() or not rom.is_file():
        p.error('A staged executable and private ROM are required')
    observations = install / 'human-observations.json'
    saved = json.loads(observations.read_text(encoding='utf-8')) if observations.exists() else {}
    window = tk.Tk()
    window.title('F-Zero offline multiplayer playtest')
    window.geometry('760x650')
    selected = tk.StringVar(value=CASES[0][0])
    mode = tk.StringVar(value=next(iter(MODES)))
    bs, native = tk.BooleanVar(value=False), tk.BooleanVar(value=False)
    second_lap_only = tk.BooleanVar(value=True)
    status = tk.StringVar(value='Deferred')
    message = tk.StringVar(value='No gamepads needed with the first two input modes. Close the game before switching setups.')
    choice = ttk.Combobox(window, textvariable=selected, state='readonly', values=[c[0] for c in CASES])
    choice.pack(fill='x', padx=16, pady=12)
    controls = ttk.Combobox(window, textvariable=mode, state='readonly', values=list(MODES))
    controls.pack(fill='x', padx=16)
    ttk.Label(window, text=KEY_HELP, wraplength=720, justify='left').pack(fill='x', padx=16, pady=10)
    instructions = ttk.Label(window, wraplength=720, justify='left')
    instructions.pack(fill='x', padx=16, pady=8)
    notes = tk.Text(window, height=8, wrap='word')
    notes.pack(fill='both', expand=True, padx=16, pady=8)
    process = None

    def selection_key():
        return f"{selected.get()} | Controls={mode.get()} | BS={bs.get()} | Native={native.get()} | BoostLap2Only={second_lap_only.get()}"

    def load(event=None):
        item = saved.get(selection_key(), {})
        instructions.configure(text=next(c[2] for c in CASES if c[0] == selected.get()))
        status.set(item.get('status', 'Deferred'))
        notes.delete('1.0', 'end')
        notes.insert('1.0', item.get('notes', ''))

    def save():
        saved[selection_key()] = dict(status=status.get(), notes=notes.get('1.0', 'end').strip())
        observations.write_text(json.dumps(saved, indent=2) + '\n', encoding='utf-8')
        message.set('Saved to ' + str(observations))

    def launch(force_launcher=False):
        nonlocal process
        if process is not None and process.poll() is None:
            message.set('The current game is still running. Close it before switching setups.')
            return
        _, count, _ = next(c for c in CASES if c[0] == selected.get())
        pads, groups = MODES[mode.get()]
        config = ('[General]\nSkipLauncher=1\n[Graphics]\nWindowScale=3\nFullscreen=0\n'
                  '[Sound]\nAudioFreq=32040\nVolume=100\n[Controller]\n')
        for seat in range(1, 5):
            config += f'SourceP{seat}={2 if seat <= pads else 1}\nGuidP{seat}=\n'
        config += '[Rewind]\nEnabled=1\nDepth=50\nInterval=15\n'
        (install/'config.ini').write_text(config, encoding='utf-8')
        (install/'keybinds.ini').write_text(''.join(f'[player{seat+1}]\n'+''.join(
            f'{button}={key}\n' for button,key in LAYOUTS[groups[seat]].items()) for seat in range(4)),encoding='utf-8')
        env = {k: v for k, v in os.environ.items() if not k.startswith(
            ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'RNET_', 'SNES_NET_'))}
        previous=configparser.ConfigParser(interpolation=None)
        previous.read(install/'fzero-video.ini',encoding='utf-8')
        callouts={key:previous.get('FZeroVideo',key,fallback=default) for key,default in (('LapCallouts','0'),('LapBoostWav',''),('FinalLapWav',''))}
        (install/'fzero-video.ini').write_text(
            '[FZeroVideo]\nEnhancedRenderer=1\nAspect=Fit\nPresentationEnabled=1\nPresentationFPS=60\n'
            f'Mode7Enabled={int(not native.get())}\nMode7Scale=8\nBSTracks={int(bs.get())}\nBSVehicles=0\n'
            'LocalTwoPlayer=1\nCGPCars=0\nCGPStockRebalance=0\nAlwaysShowRecords=1\nDiagnostics=1\n'
            +''.join(f'{key}={value}\n' for key,value in callouts.items())
            +f'LapBoostSecondLapOnly={int(second_lap_only.get())}\n',encoding='utf-8')
        env['FZERO_PACK_LOADER'] = str(int(not bs.get()))
        env['SNESRECOMP_SAVE_ROOT'] = 'human-saves'
        log = (install / 'human-game.log').open('a', encoding='utf-8')
        argv=[str(exe), '--players', str(count), str(rom)]
        if force_launcher:argv.insert(1,'--launcher')
        process = subprocess.Popen(argv,
                                   cwd=install, env=env, stdout=log, stderr=log)
        log.close()
        message.set(f'{count}P opened: {mode.get()}. Normal audio; the picker waits for everyone to ready.')

    choice.bind('<<ComboboxSelected>>', load)
    controls.bind('<<ComboboxSelected>>', load)
    row = ttk.Frame(window)
    row.pack(fill='x', padx=16, pady=8)
    options = ttk.Frame(window)
    options.pack(fill='x',padx=16)
    ttk.Checkbutton(options,text='BS courses (otherwise stock/custom)',variable=bs,command=load).pack(side='left')
    ttk.Checkbutton(options,text='Native rendering',variable=native,command=load).pack(side='left',padx=12)
    audio_options=ttk.Frame(window)
    audio_options.pack(fill='x',padx=16,pady=4)
    ttk.Checkbutton(audio_options,text='Boost voice only at lap 2',variable=second_lap_only,command=load).pack(side='left')
    ttk.Button(audio_options,text='Configure audio / mods...',command=lambda:launch(True)).pack(side='left',padx=12)
    ttk.Button(row, text='Open selected setup', command=launch).pack(side='left')
    ttk.Combobox(row, textvariable=status, values=('Pass', 'Fail', 'Deferred'),
                 state='readonly', width=12).pack(side='left', padx=12)
    ttk.Button(row, text='Save observation', command=save).pack(side='left')
    ttk.Label(window, textvariable=message, wraplength=720).pack(fill='x', padx=16, pady=8)
    load()
    if a.start:
        window.after(150, launch)
    window.mainloop()


if __name__ == '__main__':
    main()
