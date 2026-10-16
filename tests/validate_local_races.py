"""Check stock versus races in the real headless host.

The default driver observes completed actors/checkpoints and sends normal pad
inputs, without actor/world/progress edits or injected outcomes. The explicit
--teleport-fixture mode instead forces poses, neutral motion and health through
authored gates/finish strips for lifecycle checks. It never sets lap counts,
times or outcomes, and cannot qualify natural driving or handling. Artifacts
and snapshots stay private under --out. Human playtesting remains separate.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--stock',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--courses',default=','.join(str(i) for i in range(15)))
    p.add_argument('--present-every',type=int,default=8,help='Presentation cadence; simulation always advances once per tick')
    p.add_argument('--frames',type=int,default=40000)
    p.add_argument('--teleport-fixture',action='store_true',
                   help='Explicit lifecycle-only fixture: forced poses/health, actual gate/lap/finish handling; not driving validation')
    a=p.parse_args()
    courses=[int(i) for i in a.courses.split(',')]
    if any(i<0 or i>=15 for i in courses) or not 1<=a.present_every<=120:p.error('Invalid course/cadence')
    build,stock,out=(v.resolve() for v in (a.build,a.stock,a.out))
    out.mkdir(parents=True,exist_ok=False)
    (out/'packs').mkdir();(out/'settings').mkdir()
    env={k:v for k,v in os.environ.items() if not k.startswith(('FZERO_','SNESRECOMP_','SDL_','LNG_'))}
    env.update(SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='dummy',FZERO_TEST_LOCAL_DRIVER='1',
               FZERO_LOCAL_DRIVER_LIFECYCLE='1',FZERO_REWIND_TEST='1',FZERO_TEST_QUICK_SLOT='1',
               FZERO_TEST_PRESENT_EVERY=str(a.present_every),FZERO_PACK_LOADER='0',
               FZERO_PACKS_DIR=str(out/'packs'),FZERO_TRACK_PACKS=str(out/'settings'))
    if a.teleport_fixture:
        env['FZERO_TEST_LOCAL_TELEPORT']='1'
        print('TELEPORT FIXTURES: course flow only; not natural driving/physics qualification',flush=True)
    if os.name=='nt':env['PATH']='C:/msys64/mingw64/bin;'+env.get('PATH','')
    exe=build/('FZeroSNESRecompHeadless'+('.exe' if os.name=='nt' else ''))
    results=[]
    for course in courses:
        directory=out/f'course-{course:02}';directory.mkdir()
        machines=(course%4,course%4 if course%2 else (course+1)%4)
        run_env=dict(env,SNESRECOMP_SAVE_ROOT=f'lr-{os.getpid()}-{course}',
                     FZERO_LOCAL_DRIVER_CHECKPOINTS=str(directory))
        print(f'course={course} machines={machines} presentation=1/{a.present_every}',flush=True)
        started=time.perf_counter()
        with (directory/'race.log').open('w',encoding='utf-8') as log:
            r=subprocess.run([str(exe),str(stock),str(a.frames),'--versus',str(course),*(str(i) for i in machines)],
                             cwd=build,env=run_env,stdout=log,stderr=log,timeout=600)
        log=(directory/'race.log').read_text(encoding='utf-8',errors='replace')
        final=re.search(r'local-versus: tick=(\d+) phase=(\d+) race=(\d+) hash=([0-9a-f]+).*status=(\d+).*status=(\d+)',log)
        finished=all(f'player={i} laps=5 status=1' in log for i in (1,2))
        phases=sorted(int(i) for i in re.findall(r'lifecycle event=(\d+)',log))
        ok=r.returncode==0 and finished and all(i in phases for i in (0,1,3,4)) and 'quick-slot:' in log
        if a.teleport_fixture:ok=ok and 'qa-teleport: rematch countdown and full results restore PASS' in log
        result=dict(course=course,machines=machines,mode='teleport-fixture' if a.teleport_fixture else 'pad-driver',exit=r.returncode,passed=ok,phases=phases,
                    final=final.groups() if final else None,seconds=round(time.perf_counter()-started,3))
        results.append(result)
        (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(json.dumps(result),flush=True)
    if not all(r['passed'] for r in results):raise SystemExit(1)


if __name__=='__main__':main()
