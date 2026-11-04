"""Reconstruct standalone course packs from reviewed IPS/BPS inputs.

Huckmine uses the preserved author project; the remaining courses are recovered
as editable projects. No source ROM or executable patch is included in the result.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import struct
import shutil
import tempfile
import zipfile
from parse_track_pack import donor, fields
from pack_manifest import write_index
from reconstruct_fzedit_course import reconstruct
from course_tool_paths import ROOT

def export_pack(stock, result, base, root, exporter, priority=0, native_resources=None,
                credit_bytes=None, source_label='reviewed patch extraction; not original FZEdit project'):
    """Export one reviewed donor using the same identities as shipped packs."""
    base, root, exporter = Path(base), Path(root), Path(exporter).resolve()
    data = fields(base.with_suffix('.ini'))
    ident = data['id'][0]
    _export_pack(stock, result, base, root, exporter, priority, ident, data, native_resources,
                 credit_bytes, source_label)
    return json.loads((root/'extraction.json').read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--exporter', type=Path, default=ROOT/'build/FZeroExportCourses.exe')
    args = parser.parse_args()
    stock = args.stock.read_bytes()
    for priority, ident in enumerate(('astra-front','bower-league','cgp','max-league')):
        base = ROOT/'assets/track-packs'/ident
        data = fields(base.with_suffix('.ini'))
        result = donor(stock, base.with_suffix('.ips').read_bytes())
        assert hashlib.sha256(result).hexdigest() == data['target_sha256'][0]
        export_pack(stock, result, base, args.out/ident, args.exporter, priority)
        print(f'{ident}: {len(data["track"])} editable course ZIPs exported', flush=True)


def _export_pack(stock, result, base, root, exporter, priority, ident, data, native_resources,
                 credit_bytes, source_label):
    (root/'courses').mkdir(parents=True, exist_ok=True)
    manifest = dict(format=1, id=ident, name=data['name'][0], author=data['author'][0],
                    order=priority*10, cups=[], courses=[])
    for row in data['cup']:
        cid, name, _ = row.split('|')
        manifest['cups'].append(dict(id=cid, name=name, courses=[]))
    hashes = {}
    with tempfile.TemporaryDirectory() as temp:
        rom = Path(temp)/'donor.sfc'
        rom.write_bytes(result)
        for row in data['track']:
            tid, name, cup, slot = row.split('|')
            path = root/'courses'/f'{tid}.fzc'
            run = subprocess.run([str(exporter),str(rom),str(base.with_suffix('.layout')),slot,str(path)],
                                 check=True, text=True, capture_output=True, timeout=60,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            hashes[tid] = run.stdout.strip()
            if native_resources is not None:
                (Path(native_resources)/f'{tid}.fzc').write_bytes(path.read_bytes())
            resource=path.read_bytes();sections={};at=9
            while at<len(resource):
                key,size=struct.unpack_from('<HH',resource,at);at+=4
                sections[key]=resource[at:at+size];at+=size
            metadata=sections[17]
            entry=dict(id=tid,name=name,source=f'courses/{tid}.fzc',
                       requires=[label for bit,label in [(1,'grip-magnets'),(2,'up-magnets'),(4,'rainbow-road'),(8,'soft-horizon-fog')] if metadata[16]&bit])
            if metadata[17]: entry['music']=dict(spc=metadata[18]//9)
            if metadata[19]: entry.setdefault('music',{}).update(soundtrack=sections[16].split(b'\0')[0].decode(),track=metadata[19])
            manifest['courses'].append(entry)
            next(c for c in manifest['cups'] if c['id']==cup)['courses'].append(tid)
    (root/'music').mkdir(exist_ok=True)
    if ident in ('cgp','astra-front'):
        prefixes = ['cgp','F-Zero CGP P1'] if ident=='cgp' else ['F-ZERO Astra Front']
        manifest['soundtracks'] = [dict(id=ident,prefix=p,directory='music',primary=ident=='cgp') for p in prefixes]
    elif data.get('soundtrack_prefix'):
        manifest['soundtracks'] = [dict(id=ident,prefix=p,directory='music',primary=False)
                                  for p in data['soundtrack_prefix']]
    if ident == 'cgp':
        manifest['menu_music'] = json.loads((ROOT/'assets/music/cgp-menu.json').read_text())
    elif data.get('menu_music'):
        manifest['menu_music'] = dict(row.split('|', 1) for row in data['menu_music'])
    title = ROOT/'assets/track-packs/presentation'/f'{ident}.ips'
    if title.exists():
        from inspect_bs_deluxe import apply_ips
        image = apply_ips(stock, title.read_bytes())
        (root/'title.fzt').write_bytes(b'FZTITLE\1\0'+image[0x66c00:0x68000]+image[0x7c2e0:0x7c360])
        manifest['titles'] = [dict(id=ident,name='Community Grand Prix' if ident=='cgp' else 'MAX League',source='title.fzt')]
    # Reusable, pack-owned mechanic modules. Per-course modules add to the
    # shared requirements; the runtime never installs donor ASM globally.
    common=set.intersection(*(set(c['requires']) for c in manifest['courses']))
    def mechanic_module(label, requirements):
        path=root/'mechanics'/f'{label}.json'
        path.parent.mkdir(exist_ok=True)
        module=dict(format=1,id=label,engine='fzero-course-v1',requires=sorted(requirements))
        path.write_text(json.dumps(module,indent=2)+'\n',encoding='utf-8')
        return path.relative_to(root).as_posix()
    if common:
        appearance=common&{'soft-horizon-fog'}
        terrain=common-appearance
        manifest['mechanics']=[]
        if terrain: manifest['mechanics'].append(mechanic_module('terrain',terrain))
        if appearance: manifest['mechanics'].append(mechanic_module('horizon',appearance))
    for course in manifest['courses']:
        extra=set(course['requires'])-common
        course['requires']=[] # Explicitly replace embedded extraction metadata.
        if extra:
            course['mechanics']=[mechanic_module(course['id'],extra)]
    source_projects = {}
    credits={'astra-front':'Astra-Front-credits.txt','bower-league':'Bower-League-credits.txt',
             'cgp':'CGP-credits.txt','max-league':'MAX-League-credits.txt'}
    if credit_bytes is None:
        credit_bytes = (ROOT/'assets/track-packs'/credits[ident]).read_bytes()
    extracted_huckmine = None
    if ident == 'cgp':
        example = ROOT/'examples/huckmine'
        audit = json.loads((example/'source.json').read_text())
        project = (example/'huckmine.zip').read_bytes()
        if hashlib.sha256(project).hexdigest() != audit['archive_sha256']:
            raise ValueError('Preserved Huckmine source archive changed')
        (root/'courses/huckmine.zip').write_bytes(project)
        (root/'courses/huckmine.fzc').unlink()
        next(c for c in manifest['courses'] if c['id']=='huckmine')['source']='courses/huckmine.zip'
        extracted_huckmine = hashes['huckmine']
        hashes['huckmine'] = audit['course_hash']
        source_projects['huckmine'] = audit
    for course in manifest['courses']:
        source = root/course['source']
        if source.suffix == '.fzc':
            archive = source.with_suffix('.zip')
            source_projects[course['id']] = reconstruct(source, archive,
                ident+'-'+course['id'], course['name'], manifest['author'], credit_bytes)
            course['source'] = archive.relative_to(root).as_posix()
            source.unlink()
        # Archive identity survives a user renaming the public ZIP filename.
        with zipfile.ZipFile(root/course['source']) as project:
            course['source_id'] = json.loads(project.read('pack.json'))['id']
    write_index(root, manifest)
    (root/'CREDITS.txt').write_bytes(credit_bytes)
    audit = dict(source=source_label,
                 donor_sha256=hashlib.sha256(result).hexdigest(),record_hashes=hashes)
    if source_projects:
        audit.update(source_projects=source_projects, extracted_huckmine_hash=extracted_huckmine)
    (root/'extraction.json').write_text(json.dumps(audit,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
