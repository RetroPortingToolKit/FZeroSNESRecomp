"""Muted desktop qualification through native VS entry with gamepads or keyboard layouts.

Exercises the live Restart/Change/Main Menu actions, continued simulation,
resizing and the combined native/HD presentation. Uses a fresh installation.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import array
import math
import json
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'runtime', 'rom', 'packs', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    p.add_argument('--fresh-video-settings',action='store_true',help='Verify shipped video defaults rather than a seeded aspect/scale')
    p.add_argument('--saved-player-count',action='store_true',help='Use the persisted mod default without the in-game count picker')
    p.add_argument('--players',type=int,choices=(2,3,4),default=2)
    p.add_argument('--keyboard',action='store_true',help='Exercise four distinct keybinds.ini layouts through the host keyboard path')
    p.add_argument('--hd-parity',action='store_true',help='Compare sampled actual GPU output pixels against the portable renderer')
    p.add_argument('--hd-scale',type=int,default=8)
    p.add_argument('--hd-supersample',type=int,choices=(1,2,3,4),default=1)
    p.add_argument('--hd-full-target',action='store_true',help='Exercise full raster presentation instead of visible samples')
    p.add_argument('--portable-hd',action='store_true',help='Qualify the developer portable HD path and its effective-detail notice')
    p.add_argument('--benchmark',action='store_true',help='Unpaced, fixed-alpha existing gameplay qualification for matched timing')
    p.add_argument('--stock-only',action='store_true',help='Qualify native courses without the optional Astra import')
    p.add_argument('--cup',type=int,default=0)
    p.add_argument('--bs-tracks',action='store_true')
    p.add_argument('--no-contact-fixture',action='store_true',help='Content/menu smoke only; stock run separately qualifies prepared traffic contacts')
    p.add_argument('--fps',type=int,default=60)
    p.add_argument('--native',action='store_true',help='Disable HD Mode 7 for this windowed run')
    p.add_argument('--full-profile',action='store_true',help='Use all twelve CGP machines and stock rebalances')
    p.add_argument('--menu-music',action='store_true',help='Capture muted real-device output across first VS entry')
    a = p.parse_args()
    build, runtime, rom, packs, out = (getattr(a, k).resolve() for k in ('build', 'runtime', 'rom', 'packs', 'out'))
    out.mkdir(parents=True, exist_ok=False)
    stage = out / 'install'
    stage.mkdir()
    shutil.copy2(build / 'F-Zero Forever.exe', stage)
    for dll in runtime.glob('*.dll'):
        shutil.copy2(dll, stage)
    shutil.copytree(ROOT / 'assets', stage / 'assets')
    (out / 'catalog').mkdir()
    if not a.stock_only:
        shutil.copytree(packs / 'astra-front', out / 'catalog/astra-front', ignore=shutil.ignore_patterns('music', '.cache'))
    config = (runtime / 'config.ini').read_text()
    config = config.replace('SkipLauncher = 0', 'SkipLauncher = 1')
    config = config.replace('SkipLauncher=0', 'SkipLauncher=1')
    if a.keyboard:
        config=re.sub(r'(?mi)^SourceP[1-4]\s*=.*$', '', config)
        if '[Controller]' not in config:config+='\n[Controller]\n'
        config=config.replace('[Controller]', '[Controller]\n'+''.join(f'SourceP{i}=1\n' for i in range(1,5)))
    if a.menu_music:
        config=re.sub(r'(?m)^(\s*Volume\s*=).*$',r'\g<1>0',config)
    (stage / 'config.ini').write_text(config)
    shutil.copy2(runtime / 'keybinds.ini', stage)
    if a.keyboard:
        keys=list('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789')+[f'Keypad {i}' for i in range(10)]+['Keypad +','Keypad -']
        buttons=('a','b','x','y','l','r','start','select','up','down','left','right')
        (stage/'keybinds.ini').write_text(''.join(f'[player{p+1}]\n'+''.join(f'{b}={keys[p*12+i]}\n' for i,b in enumerate(buttons)) for p in range(4)),encoding='utf-8')
    (stage / 'fzero-video.ini').write_text(
        f'[FZeroVideo]\nEnhancedRenderer=1\nAspect=Fit\nPresentationEnabled=1\nPresentationFPS={a.fps}\n'
        f'Mode7Enabled={int(not a.native)}\nMode7Scale={a.hd_scale}\nMode7Supersample={a.hd_supersample}\nBSVehicles=0\nBSTracks={int(a.bs_tracks)}\nCGPCars={7 if a.full_profile else 0}\nCGPStockRebalance={15 if a.full_profile else 0}\n'
        'LocalTwoPlayer=1\nAlwaysShowRecords=1\nCrashReplay=1\nDiagnostics=1\n')
    shots = out / 'images'
    shots.mkdir()
    if a.fresh_video_settings:(stage/'fzero-video.ini').unlink()
    if a.saved_player_count:
        with (stage/'fzero-video.ini').open('a') as f:f.write(f'LocalPlayers={a.players}\n')
    clean = {k: v for k, v in os.environ.items() if not k.startswith(
        ('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_', 'SNES_NET_', 'RNET_'))}
    env = dict(clean, SDL_AUDIODRIVER='dummy', SNESRECOMP_MSU1='off',
               FZERO_TEST_TRAFFIC_VISIBLE='1', FZERO_PACK_LOADER=str(int(not a.bs_tracks)),
               FZERO_TEST_LOCAL_UI='1', FZERO_TEST_LOCAL_UI_CUP=str(a.cup), FZERO_TEST_LOCAL_PLAYERS=str(a.players), FZERO_TEST_LOCAL_UI_HIDDEN='1', FZERO_TEST_LOCAL_UI_LIVE='1',
               FZERO_TEST_LOCAL_UI_RESIZE='1', FZERO_TEST_LOCAL_UI_DRIVE='1', FZERO_TEST_LOCAL_UI_DUMP=str(shots),
               FZERO_PACKS_DIR=str(out / 'catalog'), FZERO_TRACK_PACKS=str(out / 'settings'), SNESRECOMP_SAVE_ROOT='s',
               SNESRECOMP_INPUT_SCRIPT='300-302:32,330-332:32,360-362:32,390-392:8',
               SNESRECOMP_AUTOCLOSE_FRAMES='5000')
    if a.hd_parity:env['FZERO_TEST_HD_PARITY']='1'
    if a.hd_full_target:env['FZERO_HD_FULL_TARGET']='1'
    if a.portable_hd:env['FZERO_HD_GPU']='0'
    if a.benchmark:env['FZERO_BENCHMARK']='1'
    if os.environ.get('SNESRECOMP_RENDER_WORKERS'):env['SNESRECOMP_RENDER_WORKERS']=os.environ['SNESRECOMP_RENDER_WORKERS']
    if a.keyboard:env['FZERO_TEST_LOCAL_UI_KEYBOARD']='1'
    if not a.no_contact_fixture:env['FZERO_TEST_TRAFFIC_CONTACT']='1'
    if a.menu_music:
        env.pop('SDL_AUDIODRIVER',None)
        env.update(FZERO_TEST_MENU_MUSIC='1',FZERO_TEST_AUDIO_OUTPUT=str(out/'output.pcm'))
    with (out / 'game.log').open('w') as log:
        result = subprocess.run([str(stage / 'F-Zero Forever.exe'), str(rom)], cwd=stage,
                                env=env, stdout=log, stderr=log, timeout=240)
    text = (out / 'game.log').read_text(errors='replace')
    if a.menu_music:
        assert result.returncode==0 and 'menu-music: held native VS picker' in text,text[-4000:]
        entered=int(re.search(r'\[menu-music\].*samples=(\d+)',text)[1])
        ended=int(re.search(r'held native VS picker.*samples=(\d+)',text)[1])
        assert ended-entered>=32040*2,'Audio device stopped producing during car selection'
        pcm=array.array('h');pcm.frombytes((out/'output.pcm').read_bytes())
        samples=pcm[(ended-32040)*2:ended*2]
        rms=math.sqrt(sum(s*s for s in samples)/max(1,len(samples)))
        assert rms>20,'Silent car-selection output'
        report=dict(sha256=hashlib.sha256((stage/'F-Zero Forever.exe').read_bytes()).hexdigest(),
                    sample_rate=32040,entry_sample=entered,end_sample=ended,picker_rms=rms)
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
        print('PASS native title music: real-device audio continues through VS car selection',report)
        return
    if a.hd_parity:
        assert '[fzero-hd-effective] HD unavailable' not in text,'Native loading artwork was misreported as an HD failure'
        checks=re.findall(r'\[fzero-hd-parity\].*mismatches=(\d+)',text)
        assert checks and all(int(n)==0 for n in checks),'Actual game CPU/GPU pixel parity failed'
        if a.players>2 and not a.native:
            grids=re.findall(r'\[fzero-grid-parity\] panes=(\d+).*scale=(\d+) mismatches=(\d+)',text)
            assert grids and all(int(p)==a.players and int(m)==0 and int(s)==a.hd_scale for p,s,m in grids),'GPU grid composition failed or silently fell back'
            for pane in range(a.players):
                assert re.search(rf'\[fzero-hd-parity\].*mismatches=0 .*pane={pane}\b',text),f'No actual GPU parity check for camera{pane+1}'
            if not a.hd_full_target:
                assert 'target=1342x1006' in text,'Odd resized GPU atlas was not checked'
    if a.portable_hd and a.players>2 and a.hd_scale>2:
        assert f'HD detail limited to 2x ({a.hd_scale}x requested)' in text,'Portable grid reduction was silent'
        assert '[fzero-hd-effective] HD unavailable' not in text,'Native loading artwork was misreported as a portable HD failure'

    assert result.returncode == 0 and 'local-ui: PASS' in text, text[-4000:]
    if a.saved_player_count:assert 'local-ui-stage: 12 ' not in text,'Saved count was not applied at VS entry'
    assert (shots / 'live-menu-frame.bmp').exists(), 'Live menu was not displayed'
    if not a.no_contact_fixture:
        assert all(f'[traffic-video] pane={p} ' in text for p in range(a.players)), 'Bumper pixels missing from a gameplay view'
        assert 'traffic-encounter: all human impacts PASS' in text, 'Prepared bumper contacts did not reach every human'
    if a.keyboard:
        assert 'local-ui-keyboard: four layouts, movement and menu ownership PASS' in text
    else:
        assert 'local-ui-reconnect: neutral disconnect, continuing peers, same-seat recovery PASS' in text
    report=dict(passed=True,keyboard=a.keyboard,observed_scales=sorted({int(n) for n in re.findall(r'local-ui-image:.* scale=(\d+)',text)}),players=a.players,cup=a.cup,native=a.native,hd_scale=a.hd_scale,hd_supersample=a.hd_supersample,hd_full_target=a.hd_full_target,portable_hd=a.portable_hd,bs_tracks=a.bs_tracks,contact_fixture=not a.no_contact_fixture,
                sha256=hashlib.sha256((stage/'F-Zero Forever.exe').read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    source='keyboard layouts' if a.keyboard else 'SDL gamepads'
    print(f'PASS live menu: {a.players} {source}, traffic fixture={not a.no_contact_fixture}, restart/change/exit, resized HD/native presentation', flush=True)


if __name__ == '__main__':
    main()
