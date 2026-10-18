"""Replay a F-Zero tester capture in an isolated installation and save evidence.

The ROM and matching executable/assets come from the developer's machine.
The captured checkpoints and inputs come from diagnostics/replay-... .
"""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture',type=Path)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--rom',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--segment',type=int)
    p.add_argument('--desktop',action='store_true',help='Replay renderer/window crashes in the desktop host')
    p.add_argument('--packs',type=Path,help='Local copy of the tester\'s installed course packs')
    a=p.parse_args()
    capture,build,rom,out=(getattr(a,k).resolve() for k in ('capture','build','rom','out'))
    meta=json.loads((capture/'session.json').read_text(encoding='utf-8'))
    if meta.get('format') not in (1,2) or meta.get('game')!='fzero-usa': p.error('Unsupported capture format')
    executable=build/('F-Zero Forever.exe' if a.desktop else 'FZeroSNESRecompHeadless.exe')
    image=executable.read_bytes()
    for key in ('revision','framework','ui','build','version'):
        if meta[key]=='unknown' or meta[key].encode() not in image:
            p.error(f'The executable does not match the captured {key}: {meta[key]}')
    rows=[json.loads(line) for line in (capture/'segments.jsonl').read_text().splitlines() if line.strip()]
    ids=[r['segment'] for r in rows if isinstance(r.get('segment'),int) and 0<=r['segment']<1000000]
    available=[i for i in ids if all((capture/f'segment-{i:06}.{ext}').is_file() for ext in ('sav','execution','inputs'))]
    if not available: p.error('No complete checkpoint is available')
    segment=a.segment if a.segment is not None else available[-1]
    if segment not in available: p.error(f'Segment {segment} is unavailable; choose {available}')
    stem=capture/f'segment-{segment:06}'
    route=stem.with_suffix('.inputs')
    lines=route.read_text().splitlines()
    if not lines: p.error('No inputs were captured after this checkpoint; try the previous segment')
    # C replay validates each row and compares the actual guest clock, PC and
    # WRAM checksum before applying it. No input inference or skipped frames.
    out.mkdir(parents=True,exist_ok=False)
    shutil.copy2(executable,out/executable.name)
    for dll in build.glob('*.dll'): shutil.copy2(dll,out/dll.name)
    shutil.copytree(build/'assets',out/'assets')
    video=(capture/'video.ini').read_text(encoding='utf-8-sig')
    settings=dict(line.split('=',1) for line in video.splitlines() if '=' in line)
    msu=bool(int(settings.get('CGPRules','0')) & (1<<17))
    ignore=None if msu else shutil.ignore_patterns('*.pcm','*.msu')
    shutil.copytree(build/'mods',out/'mods',ignore=ignore)
    if a.packs:
        shutil.copytree(a.packs.resolve(),out/'mods/packs',dirs_exist_ok=True,ignore=ignore)
    (out/'replay-settings').mkdir()
    if (capture/'loader.cfg').is_file(): shutil.copy2(capture/'loader.cfg',out/'replay-settings/loader.cfg')
    (out/'video.ini').write_text(video+'\nCrashReplay=0\n',encoding='utf-8')
    namespace=meta.get('save_namespace','')
    online=meta.get('mode')=='online'
    local=meta.get('mode') in ('local-2p','online')
    if local and (meta.get('players')!=2 or meta.get('versus_rules') not in (1,2,3,4,5,6)):
        p.error('Unsupported local two-player rules')
    if not re.fullmatch(r'(?:local-2p|bs-deluxe(?:/rules-[0-9a-f]{16})?|rules-[0-9a-f]{16})?',namespace):
        p.error('Invalid capture save namespace')
    records=stem.with_suffix('.records')
    if records.is_dir():
        for directory in records.iterdir():
            if directory.is_symlink() or not directory.is_dir() or not re.fullmatch('[0-9a-f]{32}',directory.name):
                p.error('Invalid captured records directory')
            file=directory/'records.bin'
            if file.is_symlink() or not file.is_file() or file.stat().st_size>33312:
                p.error('Invalid captured records file')
            destination=out/'s'/namespace/'courses'/directory.name
            destination.mkdir(parents=True)
            shutil.copy2(file,destination/'records.bin')
    if a.desktop and (capture/'host.json').is_file():
        host=json.loads((capture/'host.json').read_text(encoding='utf-8'))
        shader=host.get('shader','')
        if shader and not (out/shader).is_file():
            p.error(f'The captured shader is unavailable: {shader}. Provide the same shader or use headless replay.')
        # Restore relevant launcher choices, without importing controller
        # bindings, ROM paths or the tester's other local settings.
        aspect=('4:3','16:9','21:9')[min(2,max(0,int(host.get('aspect_index',0))))]
        (out/'config.ini').write_text(
            f'[Graphics]\nLinearFiltering={int(bool(host.get("linear_filter")))}\n'
            f'DisplayAspect={aspect}\nShader={shader}\n'
            f'[Sound]\nEnableAudio={int(bool(host.get("audio_enabled")))}\nMsu1Enabled={int(msu)}\n'
            '[Rewind]\nEnabled=0\n',encoding='utf-8')
    env={k:v for k,v in os.environ.items() if not k.startswith(('FZERO_','SNESRECOMP_','SDL_','LNG_','SNES_NET_','SNES_RB','RNET_'))}
    env.update(FZERO_REPLAY_INPUT_FILE=str(route),FZERO_REPLAY_STATE=str(stem.with_suffix('.sav')),
               FZERO_REPLAY_EXECUTION=str(stem.with_suffix('.execution')),
               FZERO_REPLAY_CONTENT_HASH=meta['content_signature'],FZERO_REPLAY_VIDEO=str(out/'video.ini'),
               FZERO_VIDEO_CONFIG=str(out/'video.ini'),FZERO_TRACK_PACKS='replay-settings',
               FZERO_DELUXE_DATA='embedded',SNESRECOMP_SAVE_ROOT='s',
               SNESRECOMP_MSU1='mods/packs/installed-music' if msu else '',
               SNESRECOMP_WRAM_DUMP=str(out/'final.ram'),SNESRECOMP_FRAME_DUMP=str(out/'final.ppm'),
               SNESRECOMP_FRAME_BMP=str(out/'final.bmp'),
               SNESRECOMP_AUTOCLOSE_FRAMES=str(len(lines)))
    env['SDL_AUDIODRIVER']='dummy'
    if meta['format']==2:
        checks=stem.with_suffix('.frames')
        if not checks.is_file(): p.error('Missing full-state frame checks')
        env['FZERO_REPLAY_CHECKS']=str(checks)
    if online:
        aspect,view=meta.get('online_aspect'),meta.get('online_view')
        if aspect not in (0,1,2) or view not in (0,1): p.error('Invalid online capture viewpoint')
        env['FZERO_REPLAY_ONLINE']=f'{aspect}:{view}'
    if local: env['FZERO_TEST_LOCAL_READY']='1'
    cmd=[str(out/executable.name),str(rom)] if a.desktop else [str(out/executable.name),str(rom),str(len(lines))]
    if local: cmd+=['--local-2p'] if a.desktop else ['--versus','0','0','1']
    timed_out=False
    with (out/'replay.log').open('w',encoding='utf-8') as log:
        try:
            result=subprocess.run(cmd,cwd=out,env=env,stdout=log,stderr=log,timeout=max(120,len(lines)/10))
            exit_code=result.returncode
        except subprocess.TimeoutExpired:
            timed_out=True
            exit_code=None
    log=(out/'replay.log').read_text(encoding='utf-8',errors='replace')
    payload=dict(segment=segment,frames=len(lines),exit_code=exit_code,timed_out=timed_out,
                 diverged='DIVERGENCE' in log,log=str(out/'replay.log'))
    (out/'result.json').write_text(json.dumps(payload,indent=2)+'\n')
    print(json.dumps(payload,indent=2))
    if timed_out or exit_code or payload['diverged']: raise SystemExit(1)


if __name__=='__main__': main()
