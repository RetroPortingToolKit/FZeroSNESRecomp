"""Import the reviewed Astra Velocity P1 revision as course resources only.

The supplied IPS is private input. The retained patch contains reachable course
data, never the donor's global vehicles, executable patches or settings.
"""
import argparse
import hashlib
import json
from pathlib import Path

from audit_track_metadata import audit_metadata, span
from course_tool_paths import ROOT, native_tool
from inspect_bs_deluxe import STOCK_SHA256
from make_ips import make_ips, apply_ips
from pack_course_resources import pack_resources
from parse_track_pack import donor, fields

PATCH_SHA256 = '5d55032be261f8d9239d4b0ab9e2202476608642f9ac5531a12c6adf4ed5c184'
SOURCE_SHA256 = '795548cb23e48731ca5c34aed551165515e195f03db249e710fa0abe241a54f8'
CUPS = ('Eclipse', 'Oblivion', 'Vector', 'Apex')
NAMES = ('Big Blue', 'Death Wind', 'Mars', 'Red Canyon', 'Huckmine',
         'Candany', 'Mute City I', 'Brutal Wind I', 'Marine City', 'Fire Field',
         'Zone Zero', 'Sand Ocean', 'Metal Field', 'Vulcanom', 'Death City',
         'Mute City  II', 'White Woods', 'Brutal Wind II', 'Synobazz', 'Illusion')
SLOTS = (0, 9, 1, 2, 3, 4, 5, 7, 17, 13, 8, 6, 12, 18, 11, 14, 15, 16, 19, 10)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify_mechanics(source, stock):
    """Exact reviewed consumers: Illusion without Rainbow's gravity patch."""
    expected = {
        0x008976: '22 ad 8f 10', 0x0098f5: '5c d2 8f 10',
        0x00ba20: '22 09 90 10 ea', 0x00e70b: '5c 17 90 10 ea',
        # One-based GP position 20, terminator. The loader compares league*5+race+1.
        0x108fab: '14 00',
        0x108fad: '64 74 64 e8 a5 90 0a 0a 65 90 65 53 1a 85 00 a2 ff e8 bf ab 8f 10 f0 09 c5 00 d0 f5 9c 75 10 a9 ff 8d df 0a 6b',
        0x108fd2: 'ad df 0a d0 14 a5 00 89 10 d0 0a a5 00 89 20 f0 10 5c f9 98 00 5c 25 99 00 a5 00 29 8c 30 06 f0 12 5c 2d 99 00 a9 40 04 c3 a9 06 85 cf ad 40 0d 8d 38 0f 5c 67 99 00',
        0x109009: 'ad df 0a d0 06 ad c0 0c 29 e0 6b a9 00 6b',
        0x109017: 'e0 00 d0 05 ad df 0a d0 09 a9 20 3c c0 0c 5c 10 e7 00 5c 16 e7 00',
        0x009c55: '5c ca 8d 10 ea ea',
        0x009c6b: '5c d7 8d 10 ea ea',
        0x108dba: '07 00 08 00 05 00 90 01 00 02 20 01',
        0x108dca: 'c5 29 90 f8 c9 00 c0 b0 f3 5c 5b 9c 00',
        0x108dd7: 'bd d0 0c c9 b6 00 f0 12 c9 cc 00 90 05 c9 d0 00 90 08 bd b0 0b e9 60 00 80 45 a9 00 70 38 fd c0 0b b0 03 a9 00 00 eb 29 ff 00 4a 4a 8d 02 42 e0 00 f0 05 a9 02 00 80 07 ad 10 0b 29 0c 00 4a 5a 9b da aa bf ba 8d 10 8d 03 42 b9 b0 0b 38 e5 14 6d 16 42 df c0 8d 10 30 04 bf c0 8d 10 fa 7a 5c 71 9c 00',
    }
    for address, code in expected.items():
        value = bytes.fromhex(code)
        if span(source, address, len(value)) != value:
            raise ValueError(f'Unreviewed Illusion instructions at {address:06x}')
    if span(source, 0x0091e6, 5) != span(stock, 0x0091e6, 5):
        raise ValueError('Illusion must retain normal gravity')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock', type=Path, required=True)
    p.add_argument('--patch', type=Path, required=True)
    p.add_argument('--out', type=Path, default=ROOT/'assets/track-packs')
    p.add_argument('--exporter', type=Path, default=native_tool('FZeroExportCourses.exe'))
    p.add_argument('--inspector', type=Path, default=native_tool('FZeroInspectPacks.exe'))
    p.add_argument('--packs', type=Path, help='Optional editable pack output (new directory)')
    a = p.parse_args()
    stock = a.stock.read_bytes()
    if len(stock) == 0x80200:
        stock = stock[512:]
    patch = a.patch.read_bytes()
    if digest(stock) != STOCK_SHA256 or digest(patch) != PATCH_SHA256:
        raise ValueError('Unreviewed stock/patch revision')
    source = donor(stock, patch)
    if digest(source) != SOURCE_SHA256:
        raise ValueError('Unexpected patched revision')
    verify_mechanics(source, stock)
    from convert_course_content import probe_fzedit_metadata, infer_fzedit_layout, inspect_roundtrip
    from export_runtime_packs import export_pack
    import tempfile
    probe = probe_fzedit_metadata(source)
    if tuple(probe['source_order_prefix']) != SLOTS:
        raise ValueError('Authored course order changed')
    expected_names = [' '.join(n.upper().split()) for n in NAMES]
    if [' '.join(t['name'].split()) for t in probe['tracks']] != expected_names:
        raise ValueError('Authored course names changed')
    # Decode the fixed-width small-font menu records, keeping original cup labels.
    alphabet = {0xaf:'A',0xbf:'B',0xc4:'C',0xc6:'E',0xca:'I',0xcd:'L',
                0xce:'N',0xd0:'O',0xd1:'P',0xd3:'R',0xd4:'S',0xd5:'T',0xd7:'V',0xd9:'X',0xff:' '}
    labels = [''.join(alphabet[int(w,16)&255] for w in row['tile_words']).strip()
              for row in probe['donor_cup_menu']['labels']]
    if labels != [c.upper() for c in CUPS]:
        raise ValueError('Authored cup labels changed')
    normalized, layout, evidence = infer_fzedit_layout(source,stock,probe)
    if normalized != source:
        raise ValueError('This revision must not need synthesized course resources')
    # The donor's Illusion ASM is identical to CGP's collision/wall rules,
    # without Rainbow_Gravity.asm. The five magnet masks are recognized above.
    layout['require'] = ['all|grip-magnets', 'all|up-magnets', '10|fall-down']
    layout['msu_source'] = ['astra-velocity']
    layout['msu'] = [f'{slot}|{10+i}' for i,slot in enumerate(SLOTS)]
    target, ranges = pack_resources(stock, source, layout, SLOTS)
    data = dict(format=['1'], id=['astra-velocity'], name=['F-Zero Astra Velocity'],
                author=['MF9_05 / Worthy'], adapter=['fzero-course-v1'],
                source_sha256=[STOCK_SHA256], target_sha256=[digest(target)],
                alternate_target_sha256=[SOURCE_SHA256],
                cup=[f'{c.lower()}|{c}|{i}' for i,c in enumerate(CUPS)],
                track=[f'{"-".join(name.lower().split())}|{name}|{CUPS[i//5].lower()}|{SLOTS[i]}' for i,name in enumerate(NAMES)])
    if audit_metadata(source,layout,data)['tracks'] != audit_metadata(target,layout,data)['tracks']:
        raise ValueError('Packed metadata differs from the source')
    packed_patch = make_ips(stock,target)
    if apply_ips(stock,packed_patch) != target:
        raise ValueError('Resource patch round trip mismatch')
    a.out.mkdir(parents=True,exist_ok=True)
    base = a.out/'astra-velocity'
    for suffix, values in (('.ini',data),('.layout',layout)):
        base.with_suffix(suffix).write_bytes(''.join(f'{k}={v}\n' for k,rows in values.items() for v in rows).encode('utf-8'))
    base.with_suffix('.ips').write_bytes(packed_patch)
    if a.packs:
        if a.packs.exists():
            raise ValueError('Editable pack destination must be new')
        with tempfile.TemporaryDirectory(prefix='fzav-') as temp:
            temp=Path(temp); baseline=temp/'native'; baseline.mkdir()
            pack=temp/'packs/astra-velocity'
            extraction=export_pack(stock,target,base,pack,a.exporter,4,native_resources=baseline)
            validation=inspect_roundtrip(a.inspector,temp,pack,baseline,extraction)
            import shutil
            shutil.copytree(pack,a.packs)
        report=dict(input_sha256=PATCH_SHA256,source_sha256=SOURCE_SHA256,
                    packed_sha256=digest(target),patch_sha256=digest(packed_patch),
                    cups=labels,tracks=probe['tracks'],inference=evidence,
                    mechanics=dict(common=['grip-magnets','up-magnets'],illusion=['fall-down'],gravity='stock'),
                    validation=validation,retained_bytes=sum(e-s for s,e in ranges))
        (a.packs/'import-validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Astra Velocity: 4 cups / 20 courses; Illusion fall-down with normal gravity')


if __name__ == '__main__':
    main()
