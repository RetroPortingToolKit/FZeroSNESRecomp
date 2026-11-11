"""Prepared single-player course checks in an isolated staged installation.

Setups are current-build snapshots prepared by QA, already paused in-game.
Only this installation's observations and test saves are written. A running
playtest is never interrupted when the selector changes.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tkinter as tk
from tkinter import ttk


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--install',type=Path,required=True)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--start',action='store_true')
    a=p.parse_args()
    install,rom=a.install.resolve(),a.rom.resolve()
    setups=json.loads((install/'qa-setups.json').read_text(encoding='utf-8'))
    observations=install/'human-observations.json'
    saved=json.loads(observations.read_text(encoding='utf-8')) if observations.exists() else {}
    window=tk.Tk();window.title('F-Zero Forever — Astra Velocity playtest')
    window.geometry('780x570')
    selected=tk.StringVar(value=setups[0]['name'])
    status=tk.StringVar(value='Deferred')
    message=tk.StringVar(value='Close the game before opening another setup. Each setup starts paused; press Start to drive.')
    ttk.Label(window,text='Astra Velocity — final human validation',font=('Segoe UI',16)).pack(anchor='w',padx=16,pady=12)
    combo=ttk.Combobox(window,textvariable=selected,state='readonly',values=[s['name'] for s in setups])
    combo.pack(fill='x',padx=16)
    instructions=ttk.Label(window,wraplength=740,justify='left')
    instructions.pack(fill='x',padx=16,pady=12)
    ttk.Label(window,text='Keyboard: Enter = Start/pause, X = accelerate, arrows = steer/tilt, Z = brake.\nGamepad controls and other settings are available in the launcher.',wraplength=740).pack(fill='x',padx=16)
    notes=tk.Text(window,height=10,wrap='word');notes.pack(fill='both',expand=True,padx=16,pady=12)
    process=None
    loaded=None
    def current(): return next(s for s in setups if s['name']==selected.get())
    def persist():
        if loaded is None:return
        item=next(s for s in setups if s['id']==loaded)
        saved[loaded]=dict(name=item['name'],status=status.get(),notes=notes.get('1.0','end').strip())
        observations.write_text(json.dumps(saved,indent=2)+'\n',encoding='utf-8')
    def load(event=None):
        nonlocal loaded
        persist()
        item=current();previous=saved.get(item['id'],{})
        instructions.configure(text=item['steps'])
        status.set(previous.get('status','Deferred'))
        notes.delete('1.0','end');notes.insert('1.0',previous.get('notes',''))
        loaded=item['id']
    def save():
        persist()
        message.set('Observation saved. Each course and mechanic keeps its own result.')
    def launch(launcher=False):
        nonlocal process
        if process is not None and process.poll() is None:
            message.set('The current playtest is still running. Close it before switching setups.');return
        item=current()
        env={k:v for k,v in os.environ.items() if not k.startswith(('FZERO_','SNESRECOMP_','SDL_','LNG_','RNET_','SNES_NET_'))}
        env.update(FZERO_PACKS_DIR=str(install/'mods/packs'),FZERO_PACK_LOADER='1',FZERO_TRACK_PACKS='settings',
                   SNESRECOMP_SAVE_ROOT='qa-setups/'+item['id'],SNESRECOMP_DEBUG_PORT='49385')
        if not launcher:
            env.update(FZERO_CUP='astra-velocity/'+item['cup'],FZERO_TEST_COURSE=str(item['ordinal']),FZERO_STATE_LOAD_AT='1:0')
        argv=[str(install/'F-Zero Forever.exe')]
        if launcher:argv.append('--launcher')
        argv.append(str(rom))
        with (install/'human-game.log').open('a',encoding='utf-8') as log:
            process=subprocess.Popen(argv,cwd=install,env=env,stdout=log,stderr=log)
        message.set('Opened '+item['name']+'. Press Start to unpause. Save your observation after testing.')
    combo.bind('<<ComboboxSelected>>',load)
    row=ttk.Frame(window);row.pack(fill='x',padx=16,pady=6)
    ttk.Button(row,text='Open selected setup',command=launch).pack(side='left')
    ttk.Button(row,text='Open launcher / full collection',command=lambda:launch(True)).pack(side='left',padx=8)
    ttk.Combobox(row,textvariable=status,state='readonly',values=('Pass','Fail','Deferred'),width=10).pack(side='left',padx=8)
    ttk.Button(row,text='Save observation',command=save).pack(side='left')
    ttk.Label(window,textvariable=message,wraplength=740).pack(fill='x',padx=16,pady=10)
    load()
    def close():
        persist();window.destroy()
    window.protocol('WM_DELETE_WINDOW',close)
    if a.start:window.after(150,launch)
    window.mainloop()


if __name__=='__main__':main()
