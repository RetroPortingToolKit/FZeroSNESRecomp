"""Private-ROM checks of durable capture, exact replay and real fault handling.

No cartridge or checkpoint fixtures are committed. Artifacts stay in --out.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--resume-desktop',action='store_true',help='Resume fault checks in an existing private validation directory')
    a=p.parse_args()
    build,rom,out=a.build.resolve(),a.rom.resolve(),a.out.resolve()
    out.mkdir(parents=True,exist_ok=a.resume_desktop)
    if os.name=='nt': ctypes.windll.kernel32.SetErrorMode(3)
    clean={k:v for k,v in os.environ.items() if not k.startswith(('FZERO_','SNESRECOMP_','SDL_','LNG_'))}
    clean.update(FZERO_DELUXE_DATA='embedded',FZERO_BS_CARS='0',FZERO_BS_TRACKS='0',FZERO_RULES='',
                 FZERO_CAPTURE_SEGMENTS='3',FZERO_TEST_CAPTURE_PACE='1')
    headless=build/'FZeroSNESRecompHeadless.exe'

    def run(label,exe,cwd,env,frames=0):
        print(label,flush=True)
        cmd=[str(exe),str(rom),str(frames)] if frames else [str(exe),str(rom)]
        with (out/f'{label}.log').open('w',encoding='utf-8') as log:
            r=subprocess.run(cmd,cwd=cwd,env=env,stdout=log,stderr=log,timeout=100)
        return r.returncode,(out/f'{label}.log').read_text(encoding='utf-8',errors='replace')

    captures=[]
    for label,frames,packs in ([] if a.resume_desktop else [('stock-retention',7300,0),('cgp-driving',2300,7)]):
        folder=out/label;folder.mkdir();(folder/'diagnostics').mkdir()
        env=dict(clean,FZERO_PACK_LOADER=str(bool(packs)*1),FZERO_CGP_CARS=str(packs),
                 FZERO_CAPTURE_DIR=str(folder/'diagnostics'),SNESRECOMP_SAVE_ROOT=f'diag-{label}',
                 SNESRECOMP_WRAM_DUMP=str(folder/'original.ram'),
                 SNESRECOMP_INPUT_SCRIPT='320-326:8,560-566:8,640-646:8,730-736:8,790-796:8,1160-2299:1')
        if packs: env['FZERO_CUP']='cgp/knight-cgp'
        code,log=run(label,headless,build,env,frames)
        assert code==0 and 'PASS' in log,log
        capture=next((folder/'diagnostics').iterdir());captures.append(capture)
        ids=sorted(int(f.stem.split('-')[1]) for f in capture.glob('segment-*.inputs'))
        assert ids==([2,3,4] if not packs else [0,1]),ids
        assert len(list(capture.glob('*.sav')))==len(ids)
        assert len(list(capture.glob('*.execution')))==len(ids)
        assert len(list(capture.glob('*.records')))==len(ids)
        stem=capture/f'segment-{ids[-1]:06}'
        route=stem.with_suffix('.inputs'); count=len(route.read_text().splitlines())
        replay=dict(clean,FZERO_REPLAY_INPUT_FILE=str(route),FZERO_REPLAY_STATE=str(stem.with_suffix('.sav')),
                    FZERO_REPLAY_EXECUTION=str(stem.with_suffix('.execution')),FZERO_REPLAY_VIDEO=str(capture/'video.ini'),
                    FZERO_TRACK_PACKS=str(capture),SNESRECOMP_SAVE_ROOT=f'diag-replay-{packs}',
                    SNESRECOMP_WRAM_DUMP=str(folder/'replayed.ram'))
        code,log=run(label+'-replay',headless,build,replay,count)
        assert code==0 and 'DIVERGENCE' not in log,log
        assert (folder/'original.ram').read_bytes()==(folder/'replayed.ram').read_bytes()
        bad=folder/'bad.inputs';lines=route.read_text().splitlines()
        row=lines[0].split();row[4]=str(int(row[4])^1);lines[0]=' '.join(row)
        bad.write_text('\n'.join(lines)+'\n')
        code,log=run(label+'-divergence',headless,build,dict(replay,FZERO_REPLAY_INPUT_FILE=str(bad)),count)
        assert code==11 and 'DIVERGENCE' in log,log

    # Stage one clean desktop installation. Leave CrashReplay absent to prove
    # an existing Diagnostics=0 preference does not turn crash recording off.
    stage=out/'desktop';stage.mkdir(exist_ok=a.resume_desktop)
    exe=stage/'FZeroSNESRecomp.exe';shutil.copy2(build/exe.name,exe)
    for dll in build.glob('*.dll'):shutil.copy2(dll,stage/dll.name)
    if not a.resume_desktop:
        shutil.copytree(build/'assets',stage/'assets')
        shutil.copytree(build/'mods',stage/'mods',ignore=shutil.ignore_patterns('*.pcm','*.msu'))
    (stage/'fzero-video.ini').write_text('[FZeroVideo]\nBSVehicles=0\nBSTracks=0\nDiagnostics=0\n'
        'EnhancedRenderer=1\nAspect=16:9\nHDMode7=0\nAlwaysShowRecords=1\n')
    (stage/'config.ini').write_text('[Sound]\nMsu1Enabled=0\n[Rewind]\nEnabled=0\n')
    env=dict(clean,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SNESRECOMP_SAVE_ROOT='s',
             SNESRECOMP_AUTOCLOSE_FRAMES='160')
    hashes=[]
    for drill in ['seh','die']:
        code,log=run('crash-'+drill,exe,stage,dict(env,SNESRECOMP_CRASH_TEST=drill))
        assert code!=0,log
        sessions=sorted((stage/'diagnostics').glob('replay-*'),key=lambda x:x.stat().st_mtime)
        capture=sessions[-1]
        reports=list(capture.glob('crash_report_*.json'));assert len(reports)==1,(capture,log)
        report=json.loads(reports[0].read_text())
        info=report['fzero_replay'];assert info['pending_input'] and info['completed_frames']==119,info
        assert len((capture/'segment-000000.inputs').read_text().splitlines())==120
        assert (capture/'segment-000000.sav').is_file() and (capture/'segment-000000.execution').is_file()
        assert (capture/'host.json').is_file()
        if drill=='seh':assert any(f.stat().st_size>1024 for f in capture.glob('*.dmp'))
        hashes.append((reports[0],reports[0].read_bytes()))
    code,log=run('normal-restart',exe,stage,dict(env,SNESRECOMP_WRAM_DUMP=str(out/'enabled.ram')))
    assert code==0,log
    assert all(file.read_bytes()==data for file,data in hashes),'A later run overwrote crash evidence'

    code,log=run('state-load',exe,stage,dict(env,FZERO_STATE_SAVE_AT='80:11',
        FZERO_STATE_LOAD_AT='120:11',SNESRECOMP_WRAM_DUMP=str(out/'state-loaded.ram')))
    assert code==0 and 'load slot 12: ok' in log,log
    capture=sorted((stage/'diagnostics').glob('replay-*'),key=lambda x:x.stat().st_mtime)[-1]
    segments=[json.loads(line) for line in (capture/'segments.jsonl').read_text().splitlines()]
    assert len(segments)==2 and segments[1]['start_frame']==120,segments

    (stage/'fzero-video.ini').write_text((stage/'fzero-video.ini').read_text()+'\nCrashReplay=0\n')
    before=set((stage/'diagnostics').iterdir())
    code,log=run('capture-disabled',exe,stage,dict(env,SNESRECOMP_WRAM_DUMP=str(out/'disabled.ram')))
    assert code==0 and set((stage/'diagnostics').iterdir())==before,log
    (stage/'fzero-video.ini').write_text((stage/'fzero-video.ini').read_text()+'\nCrashReplay=1\n')
    blocked=out/'not-a-directory';blocked.write_text('must survive')
    code,log=run('capture-unwritable',exe,stage,dict(env,FZERO_DIAGNOSTICS_DIR=str(blocked),
        SNESRECOMP_WRAM_DUMP=str(out/'unwritable.ram')))
    assert code==0 and 'Cannot start capture' in log and blocked.read_text()=='must survive',log
    assert (out/'disabled.ram').read_bytes()==(out/'unwritable.ram').read_bytes()
    assert (out/'enabled.ram').read_bytes()==(out/'disabled.ram').read_bytes()

    # The public support workflow performs isolated replay without crash-drill
    # env vars, even when the calling shell happened to have them set.
    capture=sorted((stage/'diagnostics').glob('replay-*'),key=lambda x:x.stat().st_mtime)[-1]
    tool=Path(__file__).resolve().parents[1]/'tools/replay_diagnostics.py'
    r=subprocess.run([sys.executable,str(tool),str(capture),'--build',str(build),'--rom',str(rom),
                      '--out',str(out/'tool-replay')],env=dict(clean,SNESRECOMP_CRASH_TEST='seh'),timeout=160)
    assert r.returncode==0
    result=json.loads((out/'tool-replay/result.json').read_text())
    assert result['frames']==40 and not result['diverged'],result
    assert (out/'state-loaded.ram').read_bytes()==(out/'tool-replay/final.ram').read_bytes()
    print('Exact stock/CGP replay, retention, divergence rejection, SEH/minidump, fatal capture and restart preservation passed.',flush=True)


if __name__=='__main__':main()
