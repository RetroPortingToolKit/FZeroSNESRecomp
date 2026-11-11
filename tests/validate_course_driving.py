"""Private-ROM driving checks for imported FZEdit course packs.

Discover terrain from the editable source, teleport only the approach state,
then use ordinary pad input and guest physics. Never write sampled terrain,
health outcomes, jump outcomes or finish/record state to manufacture a pass.
ROMs, snapshots, traces and screenshots stay in the fresh --out directory.
"""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import random
import re
import subprocess
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ROUTE = '320-326:8,560-566:8,640-646:8,730-736:8,790-796:8'
RAM_FRAME = 8192


def word(data, address):
    return int.from_bytes(data[address:address+2], 'little')


def health(ram):
    value=word(ram,0xc9)
    return 0 if value&0x8000 else value


def source_map(path):
    """Read FZEdit's visible map and properties, rather than assumed tile IDs."""
    with zipfile.ZipFile(path) as z:
        project = next(n for n in z.namelist() if n.lower().endswith('.fzm'))
        props = dict(line.split('=', 1) for line in z.read(project).decode('utf-8-sig').splitlines() if '=' in line)
        base = Path(project).parent
        def read(key):
            return z.read((base/props[key].replace('\\', '/')).as_posix())
        track = ET.fromstring(read('TrackFile'))
        layers = track.findall('layer')
        assert layers, 'Missing FZEdit map layer'
        width, height = int(layers[0].get('width')), int(layers[0].get('height'))
        first = int(track.find('tileset').get('firstgid'))
        values = [0]*(width*height)
        # The runtime composes every CSV layer in order. Zero is transparent,
        # and editor visibility does not suppress an authored background.
        for layer in layers:
            assert (int(layer.get('width')),int(layer.get('height'))) == (width,height)
            assert layer.find('data').get('encoding') == 'csv', 'Expected FZEdit CSV map'
            overlay = [int(v.strip()) for row in csv.reader(io.StringIO(layer.find('data').text)) for v in row if v.strip()]
            assert len(overlay) == len(values)
            for index,value in enumerate(overlay):
                if value:
                    values[index] = value
        tiles = bytes(value-first if value else 0 for value in values)
        tsx = ET.fromstring(read('TilesetTSX'))
        terrain = {int(t.get('id')): {p.get('name'): int(p.get('value')) for p in t.findall('properties/property')} for t in tsx.findall('tile')}
    # FZEdit displays a 16-pixel tile; the race engine uses eight world units.
    return width, height, tiles, terrain


def terrain_approach(kind, width, height, tiles, terrain, rng, expected_heading=None, target_index=None):
    """Find a real approach across safe road into the requested surface."""
    safe = {i for i,p in terrain.items() if not any(p.get(k,0) for k in ('BACKGROUND','WALL','BARRIER'))}
    wanted = {i for i,p in terrain.items() if p.get(kind,0)}
    # An approach must stay on its heading before contact. Push tiles or
    # magnets can move the car sideways past a small pad; jumps can launch
    # it before the intended target. Those interactions have separate cases.
    approach_safe = {i for i in safe if not any(terrain[i].get(k,0) for k in
        ('JUMP','LANDMINE','MAGNET','DOWNPULL_MAGNET','LEFT_PUSH','RIGHT_PUSH'))}
    # Scan boundaries in C with byte translation/regex. Exhaustively shuffling
    # the enormous void area is slow and produces no useful approaches.
    classes=tiles.translate(bytes(2 if i in wanted else 1 if i in approach_safe else 0 for i in range(256)))
    candidates=[]
    if target_index is not None:
        # A dash plate spans several graphic tiles. Its safe entry in the
        # native travel direction can be on the other edge of that plate.
        # Stay within the same bounded connected surface when retrying.
        connected={target_index}
        pending=[target_index]
        while pending and len(connected)<256:
            current=pending.pop()
            x,y=current%width,current//width
            for nx,ny in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
                if not (0<=nx<width and 0<=ny<height):
                    continue
                neighbor=ny*width+nx
                if neighbor not in connected and tiles[neighbor] in wanted:
                    connected.add(neighbor)
                    pending.append(neighbor)
        directions=[(0,-1,0),(1,0,0x3000),(0,1,0x6000),(-1,0,0x9000),
                    (1,-1,0x1800),(1,1,0x4800),(-1,1,0x7800),(-1,-1,0xa800)]
        candidates=[(index,dx,dy,heading) for index in sorted(connected) for dx,dy,heading in directions]
    else:
        for pattern,delta,dx,dy,heading in ((b'\1\1\1\2',3,1,0,0x3000),(b'\2\1\1\1',0,-1,0,0x9000)):
            for match in re.finditer(pattern,classes):
                index=match.start()+delta
                if match.start()//width==(match.end()-1)//width:
                    candidates.append((index,dx,dy,heading))
        for x in range(width):
            column=classes[x::width]
            for pattern,delta,dy,heading in ((b'\1\1\1\2',3,1,0x6000),(b'\2\1\1\1',0,-1,0)):
                candidates.extend(((match.start()+delta)*width+x,0,dy,heading) for match in re.finditer(pattern,column))
    rng.shuffle(candidates)
    for index,dx,dy,heading in candidates:
        tx,ty = index%width,index//width
        if expected_heading is not None:
            diff=(heading-expected_heading)%0xc000
            if min(diff,0xc000-diff)>=0x1800:
                continue
        # Three tiles of safe approach make this a crossing, not a write
        # directly onto a surface. Keep void cases away from wrap edges.
        points = [(tx-dx*d,ty-dy*d) for d in range(1,4)]
        if not all(0<=x<width and 0<=y<height and classes[y*width+x]==1 for x,y in points):
            continue
        if kind in ('JUMP','DASH'):
            ahead = [(tx+dx*d,ty+dy*d) for d in range(1,9 if kind=='JUMP' else 3)]
            if not all(0<=x<width and 0<=y<height and tiles[y*width+x] in safe for x,y in ahead):
                continue
        if kind=='PIT':
            ahead=[(tx+dx*d,ty+dy*d) for d in range(1,7)]
            if not all(0<=x<width and 0<=y<height and tiles[y*width+x] in wanted for x,y in ahead):
                continue
        if kind=='BACKGROUND':
            # Crossing a small hole can land back on road. Only expect
            # destruction when there is sustained void along the approach.
            ahead=[(tx+dx*d,ty+dy*d) for d in range(1,17)]
            if not all(0<=x<width and 0<=y<height and tiles[y*width+x] in wanted for x,y in ahead):
                continue
        return dict(x=(tx-dx*3)*8+4,y=(ty-dy*3)*8+4,heading=heading,speed=0x180,
                    target_x=tx*8+4,target_y=ty*8+4,target_tile=tiles[index])
    return None


def nearest_checkpoint(ram,x,y):
    # D00 identifies a segment, whose origin is at 11FE/13FE and whose
    # endpoint is at 1200/1400. Picking only the nearest endpoint can seed
    # the following segment and give a dash plate the wrong approach angle.
    def distance(i):
        x0,y0=word(ram,0x11fe+i*2),word(ram,0x13fe+i*2)
        dx,dy=word(ram,0x1200+i*2)-x0,word(ram,0x1400+i*2)-y0
        length=dx*dx+dy*dy
        ratio=max(0,min(1,((x-x0)*dx+(y-y0)*dy)/length)) if length else 0
        return (x-x0-ratio*dx)**2+(y-y0-ratio*dy)**2
    return min(range(ram[0xad]+1),key=distance)


def summarize(trace):
    return dict(distance=max(abs(word(r,0xb70)-word(trace[0],0xb70))+abs(word(r,0xb90)-word(trace[0],0xb90)) for r in trace),
                min_health=min(health(r) for r in trace),max_health=max(health(r) for r in trace),
                max_speed=max(word(r,0xb20) for r in trace),max_height=max(word(r,0xbc0) for r in trace),
                airborne_frames=sum(bool(r[0xd51]&128) for r in trace),death=any(r[0xc3]&64 for r in trace),
                tiles=sorted({r[0xcd0] for r in trace}),repair_frames=sum(bool(r[0xc7]&16) for r in trace))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'stock', 'packs-root', 'out'):
        p.add_argument('--'+key, type=Path, required=True)
    p.add_argument('--pack', action='append', required=True)
    p.add_argument('--course', action='append', help='Optional course IDs to limit the run')
    p.add_argument('--kind',action='append',choices=('PIT','BARRIER','JUMP','DASH','BACKGROUND','WALL','fuzz'),
                   help='Limit a focused follow-up to specific interaction types')
    p.add_argument('--seed', type=int, default=106082)
    p.add_argument('--geometry-root', type=Path, help='Optional independent source geometry for negative controls')
    p.add_argument('--explore', action='store_true', help='Capture one course and heading calibration')
    a = p.parse_args()
    a.build, a.stock, a.packs_root, a.out = (v.resolve() for v in (a.build, a.stock, a.packs_root, a.out))
    a.out.mkdir(parents=True, exist_ok=False)
    original = hashlib.sha256(a.stock.read_bytes()).hexdigest()
    clean = {k:v for k,v in os.environ.items() if not k.startswith(('FZERO_', 'SNESRECOMP_', 'SDL_', 'LNG_'))}
    env = dict(clean, FZERO_PACKS_DIR=str(a.packs_root), FZERO_PACK_LOADER='1',
               FZERO_BS_CARS='0', FZERO_BS_TRACKS='0', FZERO_CGP_CARS='0',
               FZERO_CGP_REBALANCE='0', FZERO_DELUXE_DATA='embedded', FZERO_RULES='',
               FZERO_TRACK_PACKS=str(a.out/'legacy-packs'), SNESRECOMP_SAVE_ROOT='save', FZERO_ASPECT='21:9')
    report = dict(seed=a.seed, packs={}, checks=[], failures=[], skipped=[])

    def run(folder, label, frames, setting):
        ram, trace, capture = (folder/(label+suffix) for suffix in ('.ram', '.trace', '.ppm'))
        e = dict(env, SNESRECOMP_WRAM_DUMP=str(ram), FZERO_TEST_WRAM_TRACE=str(trace), SNESRECOMP_FRAME_DUMP=str(capture))
        e.update(setting)
        proc = subprocess.run([str(a.build/'FZeroSNESRecompHeadless.exe'), str(a.stock), str(frames)], cwd=folder, env=e,
                              capture_output=True, text=True, timeout=180)
        log = proc.stdout+proc.stderr
        (folder/(label+'.log')).write_text(log, encoding='utf-8')
        assert proc.returncode == 0 and 'fzero_native: PASS' in log, (label, log[-3000:])
        data = trace.read_bytes()
        assert len(data) == frames*RAM_FRAME
        return ram.read_bytes(), [data[n*RAM_FRAME:(n+1)*RAM_FRAME] for n in range(frames)]

    def approach(folder, case):
        entries = []
        def write(address, value, size=2):
            entries.append(f'0 0 {address:x} {(value & ((1 << (size*8))-1)).to_bytes(size, "little").hex()}')
        for address,key in ((0xb70,'x'),(0xb90,'y'),(0xbd0,'heading'),(0xbe0,'heading'),(0xb20,'speed')):
            write(address, case[key])
        for address in (0xb80,0xba0,0xb30,0xb40,0xb50,0xb60,0xbb0,0xbc0,0xc20,0xcc0,0xc00):
            write(address,0)
        write(0xc9,0x300)
        write(0xd51,0,1)
        write(0xc3,0,1)
        write(0xd00,case['checkpoint'],1)
        # Teleport the complete checkpoint prerequisite. Leaving cached gates
        # at the starting grid makes dense/overlapping paths select unrelated
        # angles before the car reaches an otherwise valid dash plate.
        checkpoint=case['checkpoint']
        previous=checkpoint-1 if checkpoint else ram[0xad]
        for index,xaddr,yaddr,aaddr,faddr in ((checkpoint,0x1080,0x108e,0x109d,0x10aa),
                                            (previous,0x108c,0x109a,0x10a9,0x10b6)):
            write(xaddr,word(ram,0x1200+index*2));write(yaddr,word(ram,0x1400+index*2))
            write(aaddr,ram[0x1600+index],1);write(faddr,ram[0x1700+index],1)
        write(0xc5,ram[0x1600+checkpoint],1)
        path = folder/(case['label']+'.events')
        path.write_text('\n'.join(entries)+'\n',encoding='ascii')
        return path

    for pack_id in a.pack:
        manifest = json.loads((a.packs_root/pack_id/'courses.json').read_text(encoding='utf-8'))
        report['packs'][pack_id] = manifest['name']
        courses = {c['id']:c for c in manifest['courses']}
        for cup in manifest['cups']:
            for ordinal, ident in enumerate(cup['courses']):
                if a.course and ident not in a.course:
                    continue
                course = courses[ident]
                required=set(manifest.get('requires',[]))|set(course.get('requires',[]))
                for module in manifest.get('mechanics',[])+course.get('mechanics',[]):
                    required.update(json.loads((a.packs_root/pack_id/module).read_text())['requires'])
                fall_down=bool(required & {'fall-down','rainbow-road'})
                folder = a.out/pack_id/ident
                folder.mkdir(parents=True)
                width,height,tiles,terrain = source_map((a.geometry_root or a.packs_root)/pack_id/course['source'])
                state = folder/'start.sav'
                setting = dict(FZERO_CUP=pack_id+'/'+cup['id'], FZERO_TEST_COURSE=str(ordinal))
                ram,_ = run(folder,'start',1450,dict(setting,SNESRECOMP_INPUT_SCRIPT=ROUTE,
                            FZERO_STATE_SAVE=str(state),FZERO_TEST_SAVE_FRAME='1400'))
                assert ram[0x54:0x56] == b'\2\3', (pack_id,ident,'not racing')
                print(pack_id,ident,course['name'],'start',word(ram,0xb70),word(ram,0xb90),'tile',ram[0xcd0],flush=True)
                if a.explore:
                    for heading in range(0,0x8000,0x1000):
                        case=dict(label=f'heading-{heading:04x}',x=word(ram,0xb70),y=word(ram,0xb90),heading=heading,speed=0x300,checkpoint=ram[0xd00])
                        final,trace = run(folder,case['label'],40,dict(setting,FZERO_STATE_LOAD=str(state),FZERO_TEST_WRAM_SCRIPT=str(approach(folder,case)),SNESRECOMP_INPUT_SCRIPT='0-39:1'))
                        print(case['label'],'delta',word(final,0xb70)-case['x'],word(final,0xb90)-case['y'],
                              'sample',[(r[0xcd0],r[0xc7],word(r,0xc9),r[0xd51],r[0xc3]) for r in trace[:3]],flush=True)
                    return
                rng = random.Random(f'{a.seed}:{pack_id}:{ident}')
                cases=[]
                for kind in ('PIT','BARRIER','JUMP','DASH','BACKGROUND')+(('WALL',) if fall_down else ()):
                    location=terrain_approach(kind,width,height,tiles,terrain,rng)
                    if a.kind and kind not in a.kind:continue
                    if location:
                        cases.append(dict(location,label=kind.lower(),kind=kind))
                    else:
                        report['skipped'].append(dict(pack=pack_id,course=ident,kind=kind,reason='No suitable authored approach'))
                if not a.kind or 'fuzz' in a.kind:
                    cases.append(dict(label='fuzz',kind='fuzz',x=word(ram,0xb70),y=word(ram,0xb90),
                                      heading=word(ram,0xbd0),speed=0x180))
                for case in cases:
                    case['checkpoint']=nearest_checkpoint(ram,case['x'],case['y'])
                    buttons='0-119:1'
                    frames=120
                    if case['kind']=='PIT':
                        buttons='0-11:1,12-119:2'
                    if case['kind']=='fuzz':
                        frames=240
                        # Always accelerate; deterministic short steering,
                        # braking and shoulder bursts exercise actual inputs.
                        choices=(0,0,0,64,128,1024,2048,2)
                        buttons=','.join(f'{n}-{n+29}:{1|rng.choice(choices)}' for n in range(0,frames,30))
                    check=dict(pack=pack_id,course=ident,name=course['name'],kind=case['kind'],approach=case,inputs=buttons)
                    try:
                        final,trace=run(folder,case['label'],frames,dict(setting,FZERO_STATE_LOAD=str(state),
                                      FZERO_TEST_WRAM_SCRIPT=str(approach(folder,case)),SNESRECOMP_INPUT_SCRIPT=buttons))
                        metrics=summarize(trace)
                        if case['kind']=='DASH' and not any(r[0xd51]&0x30 for r in trace):
                            # Native dash direction comes from the course's
                            # current checkpoint. Observe that value, then
                            # retry a matching cardinal approach using input.
                            contact=next((r for r in trace if r[0xcd0]==case['target_tile']),None)
                            target_index=(case['target_y']//8)*width+case['target_x']//8
                            # Crossings can select another checkpoint on a
                            # different side of the same pad. Try the observed
                            # angle and a bounded set of safe cardinal entries.
                            directions=((contact[0xc5]<<8,) if contact else ())+(0,0x3000,0x6000,0x9000)
                            for direction in dict.fromkeys(directions):
                                location=terrain_approach('DASH',width,height,tiles,terrain,rng,direction,target_index)
                                if not location:
                                    continue
                                case.update(location,label=f'dash-aligned-{direction:04x}')
                                case['checkpoint']=nearest_checkpoint(ram,case['x'],case['y'])
                                final,trace=run(folder,case['label'],frames,dict(setting,FZERO_STATE_LOAD=str(state),
                                    FZERO_TEST_WRAM_SCRIPT=str(approach(folder,case)),SNESRECOMP_INPUT_SCRIPT=buttons))
                                metrics=summarize(trace)
                                if any(r[0xd51]&0x30 for r in trace):
                                    break
                            # Some plates have no safe entry in the native
                            # checkpoint direction at this location. Try a
                            # bounded set of other authored approaches. Only
                            # pose/checkpoint prerequisites change; terrain,
                            # boost flags and outcomes still come from physics.
                            for attempt in range(6):
                                if any(r[0xd51]&0x30 for r in trace):break
                                direction=(contact[0xc5]<<8) if contact else None
                                location=terrain_approach('DASH',width,height,tiles,terrain,rng,direction)
                                if not location:break
                                case.update(location,label=f'dash-other-{attempt}')
                                case['checkpoint']=nearest_checkpoint(ram,case['x'],case['y'])
                                final,trace=run(folder,case['label'],frames,dict(setting,FZERO_STATE_LOAD=str(state),
                                    FZERO_TEST_WRAM_SCRIPT=str(approach(folder,case)),SNESRECOMP_INPUT_SCRIPT=buttons))
                                metrics=summarize(trace)
                                contact=next((r for r in trace if r[0xcd0]==case['target_tile']),None)
                        check.update(metrics)
                        assert metrics['distance']>=8, 'Car never moved'
                        if case['kind']!='fuzz':
                            assert case['target_tile'] in metrics['tiles'], 'Approach missed intended terrain'
                        if case['kind']=='PIT':
                            assert metrics['repair_frames'] and metrics['max_health']>0x300, 'Repair strip did not refill energy'
                        elif case['kind']=='JUMP':
                            assert any(r[0xcd0]==case['target_tile'] and r[0xd50]&64 for r in trace), 'Jump terrain not sampled'
                            assert metrics['airborne_frames'] and metrics['max_height']>0x800, 'Jump pad did not launch the car'
                        elif case['kind']=='DASH':
                            assert any(r[0xd51]&0x30 for r in trace), 'Dash plate did not activate boost'
                        elif case['kind']=='BARRIER':
                            if fall_down:
                                assert metrics['min_health']==0x300, 'Fall-down barrier unexpectedly damaged the car'
                            else:
                                assert metrics['min_health']<0x300, 'Boundary did not damage the car'
                        elif case['kind']=='WALL':
                            assert metrics['death'], 'Fall-down wall crossing did not reach the void'
                            dead=next(r for r in trace if r[0xc3]&64)
                            assert dead[0xcf]==6 and dead[0x1075]==0, 'Expected the falling death animation'
                        elif case['kind']=='BACKGROUND':
                            if fall_down:
                                assert metrics['death'], 'Fall-down void did not trigger destruction'
                            else:
                                # Native grounded background contact damages
                                # and redirects the car. Wind/walls can return
                                # it to road before its energy runs out; unlike
                                # Illusion, contact is not an instant death.
                                assert metrics['death'] or (metrics['min_health']<0x300 and
                                    any(r[0xcc0]&128 for r in trace)), 'No native off-course damage/destruction response'
                                check['off_course_response']='destruction' if metrics['death'] else 'native damage and redirection'
                        report['checks'].append(check)
                        print(ident,case['kind'],'PASS',metrics,flush=True)
                    except (AssertionError,subprocess.TimeoutExpired) as error:
                        check['error']=str(error)
                        report['failures'].append(check)
                        print(ident,case['kind'],'FAIL',error,flush=True)
                (a.out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert hashlib.sha256(a.stock.read_bytes()).hexdigest() == original, 'Private stock ROM modified'
    (a.out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if report['failures']:
        raise SystemExit(f"{len(report['failures'])} driving checks need review; see {a.out/'validation.json'}")


if __name__ == '__main__':
    main()
