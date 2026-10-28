"""Muted real-course bootstrap plus native traffic/contact/state integration."""
import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('build','rom','packs','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--courses',default='0,14,15,65')
    p.add_argument('--special-course',type=int,default=59,help='Reviewed Sulfur Swamp index; require actual jump and up-magnet tile coverage')
    a=p.parse_args()
    build,rom,packs,out=(getattr(a,key).resolve() for key in ('build','rom','packs','out'))
    out.mkdir(parents=True,exist_ok=False)
    clean={k:v for k,v in os.environ.items() if not k.startswith(('FZERO_','SNESRECOMP_','SDL_','RNET_','SNES_NET_','LNG_'))}
    report=[]
    for course in map(int,a.courses.split(',')):
        case=out/str(course);case.mkdir()
        shutil.copytree(ROOT/'assets/vehicle-packs',case/'assets/vehicle-packs')
        shutil.copytree(ROOT/'assets/track-packs',case/'assets/track-packs')
        env=dict(clean,SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='dummy',FZERO_DELUXE_DATA='embedded',
                 FZERO_PACKS_DIR=str(packs),FZERO_TRACK_PACKS=str(out/'settings'),FZERO_PACK_LOADER='1',
                 FZERO_BS_CARS='0',FZERO_BS_TRACKS='0',SNESRECOMP_MSU1='off',SNESRECOMP_SAVE_ROOT='s',
                 FZERO_LIFECYCLE_TEST='1',FZERO_REWIND_TEST='1',FZERO_TEST_SAVE_FRAME='925',
                 SNESRECOMP_WRAM_DUMP=str(case/'native.bin'),SNESRECOMP_FRAME_DUMP=str(case/'final.ppm'))
        env['FZERO_TEST_WORLD_DUMP']=str(case/'world.bin')
        with (case/'game.log').open('w') as log:
            run=subprocess.run([str(build/'FZeroSNESRecompHeadless.exe'),str(rom),'950','--versus',str(course),'0','1'],
                               cwd=case,env=env,stdout=log,stderr=log,timeout=120)
        text=(case/'game.log').read_text(errors='replace')
        assert run.returncode==0,(course,text[-3000:])
        assert 'lifecycle: save/load ten-frame resimulation identical' in text,(course,text[-3000:])
        assert 'rewind: actual ring restore and ten-frame resimulation identical' in text,(course,text[-3000:])
        with (case/'traffic.log').open('w') as log:
            trial=subprocess.run([str(build/'fzero_traffic_tests.exe'),str(rom),str(case/'native.bin'),str(case/'world.bin')]+
                                 (['require-special'] if course==a.special_course else []),
                                 cwd=case,stdout=log,stderr=log,timeout=30)
        assert trial.returncode==0,(course,(case/'traffic.log').read_text())
        report.append(dict(course=course,boot=True,native_traffic_contacts_views_restore_restart=True,
                           game_save_load_rewind=True))
        print('PASS course',course,flush=True)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
