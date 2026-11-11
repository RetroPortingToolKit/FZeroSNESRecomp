"""Local integration checks for extracted/raw pack parity (private ROM required)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ROUTE = '320-326:8,560-566:8,640-646:8,730-736:8,790-796:8'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock',type=Path,required=True)
    p.add_argument('--packs',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--build',type=Path,default=ROOT/'build')
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    build=a.build.resolve();packs=a.packs.resolve();stock=a.stock.resolve()
    clean={k:v for k,v in os.environ.items() if not k.startswith(('FZERO_','SNESRECOMP_','SDL_'))}
    def inspect(directory):
        r=subprocess.run([str(build/'FZeroInspectPacks.exe'),str(directory)],capture_output=True,text=True,env=clean,check=True)
        assert not r.stderr,r.stderr
        return r.stdout
    folder=inspect(packs)
    reviewed = 0
    for source in packs.glob('*/extraction.json'):
        hashes = json.loads(source.read_text())['record_hashes']
        reviewed += len(hashes)
        for course,expected in hashes.items():
            assert f'{source.parent.name}/{course} {expected}' in folder
    zipped=out/'zipped';zipped.mkdir()
    for source in packs.iterdir():
        if not (source/'pack.json').is_file():continue
        with zipfile.ZipFile(zipped/(source.name+'.zip'),'w',zipfile.ZIP_DEFLATED) as z:
            for f in source.rglob('*'):
                if f.is_file():z.write(f,f.relative_to(source).as_posix())
    assert inspect(zipped)==folder,'Folder and ZIP catalog/resources differ'
    markers={f:f.stat().st_mtime_ns for f in (zipped/'.cache').glob('*.ready')}
    assert markers and inspect(zipped)==folder
    assert all(f.stat().st_mtime_ns==stamp for f,stamp in markers.items()),'ZIP cache was rewritten'
    print(f'{reviewed} extracted record identities and folder/ZIP resources: PASS',flush=True)
    # Invalid inputs must produce an error and never register a partial pack.
    invalid=out/'invalid';invalid.mkdir()
    with zipfile.ZipFile(invalid/'escape.zip','w') as z:
        z.writestr('../escaped.txt','must never escape')
    result=subprocess.run([str(build/'FZeroInspectPacks.exe'),str(invalid)],capture_output=True,text=True,env=clean)
    assert result.returncode and result.stderr and not list(invalid.rglob('escaped.txt'))
    for case in ('unknown-mechanic','missing-course','duplicate-id'):
        target=out/case;target.mkdir()
        shutil.copytree(packs/'bower-league',target/'first')
        descriptor=target/'first/courses.json';data=json.loads(descriptor.read_text())
        if case=='unknown-mechanic':data['courses'][0]['requires']=['unknown-mechanic']
        if case=='missing-course':data['courses'][0]['source']='absent.fzc'
        descriptor.write_text(json.dumps(data))
        envelope=target/'first/pack.json'
        metadata=json.loads(envelope.read_text())
        metadata['payload']['sha256']=hashlib.sha256(descriptor.read_bytes()).hexdigest()
        envelope.write_text(json.dumps(metadata))
        if case=='duplicate-id':shutil.copytree(target/'first',target/'second')
        result=subprocess.run([str(build/'FZeroInspectPacks.exe'),str(target)],capture_output=True,text=True,env=clean)
        assert result.returncode and result.stderr and not result.stdout,(case,result)
    print('ZIP traversal, unknown mechanics, missing resources and duplicate IDs rejected: PASS',flush=True)
    shutil.copytree(ROOT/'assets/vehicle-packs',out/'assets/vehicle-packs')
    shutil.copytree(ROOT/'assets/track-packs/presentation',out/'assets/track-packs/presentation')
    for pack,cup in [('astra-velocity','eclipse'),('bower-league','bower'),('cgp','cgp-1'),('max-league','max')]:
        label=pack;ram=out/(label+'.ram')
        env=dict(clean,FZERO_PACKS_DIR=str(packs),FZERO_PACK_LOADER='1',FZERO_TRACK_PACKS=str(out/'settings'),
                 FZERO_DELUXE_DATA='embedded',FZERO_BS_CARS='0',FZERO_BS_TRACKS='0',FZERO_CGP_CARS='0',
                 FZERO_CGP_REBALANCE='0',FZERO_RULES='',FZERO_CUP=pack+'/'+cup,
                 SNESRECOMP_SAVE_ROOT='s-'+label,SNESRECOMP_INPUT_SCRIPT=ROUTE,
                 SNESRECOMP_WRAM_DUMP=str(ram),SNESRECOMP_FRAME_DUMP=str(out/(label+'.ppm')))
        r=subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'),str(stock),'1750'],cwd=out,env=env,capture_output=True,text=True,timeout=240)
        log=r.stdout+r.stderr;(out/(label+'.log')).write_text(log,encoding='utf-8')
        assert r.returncode==0 and 'fzero_native: PASS' in log,(label,log[-2500:])
        state=ram.read_bytes();assert state[0x54:0x56]==b'\2\3',(label,state[0x54:0x57].hex())
        assert f'loaded {pack}:' in log,(label,'Pack not loaded')
        print(label+': race started PASS',flush=True)

if __name__=='__main__':main()
