"""Offline multiplayer human checks using a staged, disposable installation."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tkinter as tk
from tkinter import ttk

CASES = (
    ("4 gamepads: race and menus", 4, False,
     "Connect four gamepads. Start on each device, choose machines (include a duplicate), "
     "then select a course. Check each pad controls only its ship and all four views/HUDs are readable. "
     "Drive a real race, test contact/boost/crash and finish/results. Start opens a live menu; "
     "other racers keep moving. Try Restart, Change Race and Main Menu. Try the remaining machine groups."),
    ("Keyboard + 3 gamepads: device recovery", 4, True,
     "P1 uses the keyboard; the other three seats use gamepads. Check input ownership. "
     "Disconnect/reconnect the last pad during a race: its input becomes neutral, other racers continue, "
     "and the returning pad controls only that seat. Repeat after Change Race. "
     "Keyboard: arrows, Z/X, Enter Start, D/C shoulders, A player count."),
    ("3 players: presentation and content", 3, False,
     "Connect three gamepads. Check the unused fourth cell is black. Resize the window tall/wide "
     "and check camera margins, sprites, labels and HUDs. Try a BS league and an installed custom "
     "league if available. Check music/effects with normal audio. Use P1 Y before readying to "
     "switch between 2, 3 and 4 players; verify two-player top/bottom views."),
)


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
    window.geometry('720x540')
    selected = tk.StringVar(value=CASES[0][0])
    bs = tk.BooleanVar(value=False)
    native = tk.BooleanVar(value=False)
    status = tk.StringVar(value='Deferred')
    message = tk.StringVar(value='Fresh test profile. Close the game before switching setups.')
    choice = ttk.Combobox(window, textvariable=selected, state='readonly', values=[c[0] for c in CASES])
    choice.pack(fill='x', padx=16, pady=12)
    instructions = ttk.Label(window, wraplength=680, justify='left')
    instructions.pack(fill='x', padx=16, pady=8)
    notes = tk.Text(window, height=9, wrap='word')
    notes.pack(fill='both', expand=True, padx=16, pady=8)
    process = None

    def selection_key():
        return f"{selected.get()} | BS={bs.get()} | Native={native.get()}"

    def load(event=None):
        item = saved.get(selection_key(), {})
        instructions.configure(text=next(c[3] for c in CASES if c[0] == selected.get()))
        status.set(item.get('status', 'Deferred'))
        notes.delete('1.0', 'end')
        notes.insert('1.0', item.get('notes', ''))

    def save():
        saved[selection_key()] = dict(status=status.get(), notes=notes.get('1.0', 'end').strip())
        observations.write_text(json.dumps(saved, indent=2) + '\n', encoding='utf-8')
        message.set('Saved to ' + str(observations))

    def launch():
        nonlocal process
        if process is not None and process.poll() is None:
            message.set('The current game is still running. Close it before switching setups.')
            return
        _, count, keyboard, _ = next(c for c in CASES if c[0] == selected.get())
        config = install / 'config.ini'
        text = ('[General]\nSkipLauncher=1\n[Graphics]\nWindowScale=3\nFullscreen=0\n'
                '[Sound]\nAudioFreq=32040\nVolume=100\n[Controller]\n')
        for seat in range(1, 5):
            text += f'SourceP{seat}={1 if keyboard and seat == 1 else 2}\nGuidP{seat}=\n'
        text += '[Rewind]\nEnabled=1\nDepth=50\nInterval=15\n'
        config.write_text(text, encoding='utf-8')
        env = {k: v for k, v in os.environ.items() if not k.startswith(
            ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'RNET_', 'SNES_NET_'))}
        (install/'fzero-video.ini').write_text(
            '[FZeroVideo]\nEnhancedRenderer=1\nAspect=Fit\nPresentationEnabled=1\nPresentationFPS=60\n'
            f'Mode7Enabled={int(not native.get())}\nMode7Scale=8\nBSTracks={int(bs.get())}\nBSVehicles=0\n'
            'LocalTwoPlayer=1\nCGPCars=0\nCGPStockRebalance=0\nAlwaysShowRecords=1\nDiagnostics=1\n',encoding='utf-8')
        env['FZERO_PACK_LOADER'] = str(int(not bs.get()))
        env['SNESRECOMP_SAVE_ROOT'] = 'human-saves'
        log = (install / 'human-game.log').open('a', encoding='utf-8')
        process = subprocess.Popen([str(exe), '--players', str(count), str(rom)],
                                   cwd=install, env=env, stdout=log, stderr=log)
        log.close()
        message.set(f'{count}P opened with normal audio. The machine picker waits for all players.')

    choice.bind('<<ComboboxSelected>>', load)
    row = ttk.Frame(window)
    row.pack(fill='x', padx=16, pady=8)
    options = ttk.Frame(window)
    options.pack(fill='x',padx=16)
    ttk.Checkbutton(options,text='BS courses (otherwise stock/custom)',variable=bs,command=load).pack(side='left')
    ttk.Checkbutton(options,text='Native rendering',variable=native,command=load).pack(side='left',padx=12)
    ttk.Button(row, text='Open selected setup', command=launch).pack(side='left')
    ttk.Combobox(row, textvariable=status, values=('Pass', 'Fail', 'Deferred'),
                 state='readonly', width=12).pack(side='left', padx=12)
    ttk.Button(row, text='Save observation', command=save).pack(side='left')
    ttk.Label(window, textvariable=message, wraplength=680).pack(fill='x', padx=16, pady=8)
    load()
    if a.start:
        window.after(150, launch)
    window.mainloop()


if __name__ == '__main__':
    main()
