"""Private-ROM checks: required ASM semantics and isolation between course packs."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ROUTE = '320-326:8,560-566:8,640-646:8,730-736:8,790-796:8'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock', type=Path, required=True)
    p.add_argument('--packs', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--build', type=Path, default=ROOT/'build')
    a = p.parse_args()
    out, packs, build, stock = [x.resolve() for x in (a.out,a.packs,a.build,a.stock)]
    out.mkdir(parents=True,exist_ok=False)
    clean = {k:v for k,v in os.environ.items() if not k.startswith(('FZERO_','SNESRECOMP_','SDL_'))}
    inspected = subprocess.check_output([str(build/'FZeroInspectPacks.exe'),str(packs)],env=clean,text=True)
    for audit in packs.glob('*/extraction.json'):
        for track, digest in json.loads(audit.read_text())['record_hashes'].items():
            assert f'{audit.parent.name}/{track} {digest}' in inspected
    print('Mechanics modules preserve all reviewed course/record hashes',flush=True)
    cases = [(label,cup,course,cars,True) for cars in (0,1) for label,cup,course in (
        ('native',('bs-deluxe' if cars else 'retail')+'/knight',0),
        ('cgp','cgp/cgp-2',3),
        ('rainbow','cgp/cgp-6',0),('max','max-league/max',2),('bower','bower-league/bower',3),
        ('velocity','astra-velocity/eclipse',0),('illusion','astra-velocity/apex',4))]
    cases += [('cgp','cgp/cgp-2',3,cars,False) for cars in (0,1)]
    if (packs/'astra-front/pack.json').is_file():
        cases += [('astra','astra-front/astra',0,cars,True) for cars in (0,1)]
    def run(case):
        label,cup,course,cars,legend=case
        folder=out/(f'{label}-cars{cars}'+('' if legend else '-no-legend'));folder.mkdir()
        shutil.copytree(ROOT/'assets/vehicle-packs',folder/'assets/vehicle-packs')
        env=dict(clean,FZERO_PACKS_DIR=str(packs),FZERO_PACK_LOADER='1',FZERO_TRACK_PACKS='settings',
                 FZERO_DELUXE_DATA='embedded',FZERO_BS_CARS=str(cars),FZERO_BS_TRACKS='0',
                 FZERO_CGP_CARS='0',FZERO_CGP_REBALANCE='0',FZERO_CUP=cup,FZERO_TEST_COURSE=str(course),
                 # Retired switches must never force terrain semantics globally.
                 FZERO_RULES='cgp-dmag,cgp-up-magnet,cgp-rainbow'+(',cgp-legend' if legend else ''),
                 FZERO_RULE_PROBE='1',SNESRECOMP_SAVE_ROOT='s',SNESRECOMP_INPUT_SCRIPT=ROUTE)
        result=subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'),str(stock),'1750'],
                              cwd=folder,env=env,capture_output=True,text=True,timeout=180)
        log=result.stdout+result.stderr;(folder/'run.log').write_text(log,encoding='utf-8')
        assert result.returncode==0 and 'rules-probe: PASS' in log,(case,log[-2200:])
        if label in ('astra','cgp','rainbow','velocity','illusion'):
            assert 'ASM parity PASS' in log and 'grounded grip' in log,case
        else:
            assert 'native downpull cases=48 PASS' in log,case
        if label in ('illusion','rainbow'):
            assert f'gravity={"stock" if label=="illusion" else "high"} PASS' in log,case
        print(folder.name+': scoped mechanics PASS',flush=True)
    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(run,cases))

if __name__=='__main__':
    main()
