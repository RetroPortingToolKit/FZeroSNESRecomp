"""ROM-free conversion boundaries; native corpus validation is run separately."""
from copy import deepcopy
import io
import json
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import convert_course_content as converter
from make_ips import make_ips
from parse_track_pack import fields


def archive_bytes(members):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, content in members:
            if isinstance(name, str):
                info = zipfile.ZipInfo(name)
                info.filename = name  # Write the unsafe spelling literally on Windows.
                info.compress_type = zipfile.ZIP_DEFLATED
                name = info
            archive.writestr(name, content)
    return stream.getvalue()


def bps_literal(source, target):
    def number(value):
        output = bytearray()
        while True:
            byte = value & 127; value >>= 7
            if not value:
                return bytes(output + bytes([byte | 128]))
            output.append(byte); value -= 1
    stream = b'BPS1'+number(len(source))+number(len(target))+number(0)
    stream += number(((len(target)-1) << 2) | 1)+target
    stream += struct.pack('<II', zlib.crc32(source), zlib.crc32(target))
    return stream+struct.pack('<I', zlib.crc32(stream))


def pcm(frames=8, loop=0):
    return b'MSU1'+struct.pack('<I', loop)+bytes(frames*4)


def metadata_donor():
    """Two reordered resources exercise generic consumers without a profile."""
    rom = bytearray(0x100000)
    def put(address, value):
        offset = ((address & 0x7f0000) >> 1) | (address & 0x7fff)
        rom[offset:offset+len(value)] = value
    def pointer(address):
        return address.to_bytes(3, 'little')
    put(0x00f7e0, bytes.fromhex('08 e2 30 c2 10 ae 59 10 bf')+pointer(0x109100)+bytes.fromhex('e2 10 ea ea ea'))
    put(0x10824d, bytes.fromhex('8a c2 30 29 ff 00 8d 5b 10 bb bf')+pointer(0x109110)+bytes.fromhex('29 ff 00 8d 59 10 20'))
    put(0x10845b, bytes.fromhex('ad 59 10 0a 0a aa bf')+pointer(0x109126)+bytes.fromhex('8d d9 0a bf')+pointer(0x109128)+bytes.fromhex('8d db 0a'))
    put(0x10846f, bytes.fromhex('ad 59 10 0a 6d 59 10 aa 8b bf')+pointer(0x109120)+bytes.fromhex('a8 e2 20 bf')+pointer(0x109122)+b'\x48')
    put(0x10839b, bytes.fromhex('da c2 30 ad 59 10 0a 6d 59 10 aa bf')+pointer(0x109140)+bytes.fromhex('85 00 bf')+pointer(0x109141)+bytes.fromhex('85 01 e2 30 fa'))
    put(0x109100, bytes([9, 72])); put(0x109110, bytes([1, 0]))
    put(0x109140, pointer(0x109200)+pointer(0x109300))
    put(0x109200, bytes.fromhex('10 53 01 82 1b ff')+bytes([converter.ALPHABET[9]])+b'\0')
    put(0x109300, bytes.fromhex('10 53 01 82 1b ff')+bytes([converter.ALPHABET[16]])+b'\0')
    put(0x02c26a, bytes.fromhex('a5 46 29 07 c9 06 d0 11 a5 53 a6 58 d0 08 a5 90 0a 0a 65 90 65 53 18 69 0c 60'))
    return bytes(rom)


def inferable_donor():
    rom=bytearray(metadata_donor())
    def put(address,value):
        offset=((address&0x7f0000)>>1)|(address&0x7fff)
        rom[offset:offset+len(value)]=value
    def ptr(address,width=3): return address.to_bytes(width,'little')
    operations=[
        ('settings','ae 59 10 bf','5c 1f 9f 00',None,0),
        ('palettes','ad 59 10 0a 6d 59 10 aa bf','85 00 bf','85 01 a0 de 00',1),
        ('pools','ad 59 10 0a 6d 59 10 aa a9 00 80 8d 00 43 a9 00 24 8d 05 43 bf','8d 02 43 e2 20 bf','8d 04 43',2),
        ('graphics','ad 59 10 0a 6d 59 10 aa bf','85 04 e2 20 bf','85 06 a9 00 22 9b 82 10',2),
        ('paths','ad 59 10 0a 6d 59 10 aa bf','85 30 bf','85 31 64 33 5c 4e d6 00',1),
        ('sky_graphics','ad 59 10 0a 6d 59 10 aa a9 01 18 8d 00 43 bf','8d 02 43 bf','8d 03 43 a9 00 20 8d 05 43',1),
        ('sky_back','a9 01 18 8d 00 43 bf','85 00 8d 02 43 bf','e2 10 aa 8e 04 43 a9 00 07',2),
        ('sky_front','a9 60 71 8d 16 21 bf','85 00 8d 02 43 bf','e2 10 aa 8e 04 43 a9 40 05',2),
        ('terrain','8b c2 30 da 9b ad 59 10 0a 6d 59 10 aa bf','18 79 d0 0c a8 e2 20 bf','fa 48 ab e0 00 00',2),
        ('gradients','ae 59 10 bf','85 9c 5c 22 a1 00',None,0),
        ('shortcuts','8b e2 20 bf','48 ab c2 20 bf','aa bd 00 00 30 3f af 6d 10 00',-2),
        ('maps','bf','85 26 bf','85 22 bf',-3)]
    for index,(key,prefix,middle,tail,delta) in enumerate(operations):
        address=0x109500+index*0x20
        code=bytes.fromhex(prefix)+ptr(address)+bytes.fromhex(middle)
        if tail: code+=ptr(address+delta)+bytes.fromhex(tail)
        put(0x00a000+index*0x40,code)
    put(0x00b000,bytes.fromhex('c2 30 8b 4b ab ad 59 10 0a aa bc')+ptr(0x9700,2)+bytes.fromhex('be 00 00 30 1c'))
    put(0x108484,bytes.fromhex('a9 80 8d 15 21'))
    put(0x00d146,bytes.fromhex('08 e2 20 c9 bb b0 31 eb 29 7f eb aa bf 50 81 10 48 bf 95 80 10 85 00 f0 03 20 93 d1 fa f0'))
    for code in (0x6c,0x6d):
        put(0x108095+code,bytes([code]));put(0x108150+code,bytes([code+16]))
    put(0x02fbda,bytes(range(45)))
    return bytes(rom)


def older_metadata_donor(inline=False):
    """Synthetic older FZEdit layout: relocated consumers, venue SPC music."""
    rom=bytearray(0x100000)
    def put(address,value):
        offset=((address&0x7f0000)>>1)|(address&0x7fff)
        rom[offset:offset+len(value)]=value
    def ptr(address):return address.to_bytes(3,'little')
    entry=0x109000
    put(0x009f08,b'\x5c'+ptr(entry))
    put(entry,bytes.fromhex('08 e2 30 a5 58 d0 0a a5 90 0a 0a 65 90 65 53 80 02 a5 53 a8 a2 00 c9 05 30 06 38 e9 05 e8 80 f6')+
        bytes.fromhex('8a c2 30 29 ff 00 8d 5b 10 bb bf')+ptr(0x109700)+bytes.fromhex('29 ff 00 8d 59 10 20'))
    put(0x009f1b,b'\x5c'+ptr(0x109050))
    put(0x109050,bytes.fromhex('ae 59 10 bf')+ptr(0x109720)+bytes.fromhex('5c 1f 9f 00'))
    put(0x109080,bytes.fromhex('da c2 20 ad 59 10 0a 6d 59 10 aa bf')+ptr(0x109740)+
        bytes.fromhex('85 00 e2 20 bf')+ptr(0x109742)+bytes.fromhex('85 02 fa 5c dd ab 00'))
    mini=0x109400
    if inline:
        code=bytes.fromhex('a9 0c 00 8d d9 0a a9 d3 00 8d db 0a ad 59 10 0a 6d 59 10 aa bf')
        mini=0x109100+len(code)+3+5+3+7+15
        code+=ptr(mini)+bytes.fromhex('a8 8b e2 20 bf')+ptr(mini+2)+bytes.fromhex('48 ab a9 80 8d 15 21')
        code+=bytes.fromhex('a2 00 5e 8e 16 21 a2 1f 00 22 77 9e 03 ab 6b')
        put(0x109100,code)
        put(mini+6,bytes.fromhex('ae 59 10 bf')+ptr(0x109780)+bytes.fromhex('85 9c 5c 22 a1 00'))
    else:
        put(0x109100,bytes.fromhex('ad 59 10 0a 6d 59 10 aa bf')+ptr(mini)+
            bytes.fromhex('a8 8b e2 20 bf')+ptr(mini+2)+bytes.fromhex('48 ab a9 80 8d 15 21'))
        put(0x109200,bytes.fromhex('ad 59 10 0a 0a aa bf')+ptr(mini+6)+
            bytes.fromhex('8d d9 0a bf')+ptr(mini+8)+bytes.fromhex('8d db 0a'))
    put(mini,ptr(0x109800)*2)
    put(0x109700,bytes([1,0]));put(0x109720,bytes([0xc3,0xe3]))
    put(0x109740,ptr(0x109900)+ptr(0x109a00))
    put(0x109900,bytes.fromhex('10 53 01 82 1b ff 64 28 00'))
    put(0x109a00,bytes.fromhex('10 53 01 82 1b ff 65 00'))
    table=0x039e68
    put(0x00f7e0,bytes.fromhex('08 e2 30 ae d8 0a bf')+ptr(table)+bytes.fromhex('0a 0a 0a 7f')+ptr(table))
    put(table,bytes(list(range(9))+[7]+[0]*5))
    put(0x009fb5,bytes.fromhex('e2 30 ad f5 0c c9 03 90 0f c9 06 f0 0b ae ff 0c f0 06 18 6d ff 0c 69 04 8d d8 0a 28 60'))
    return bytes(rom)


class ConversionBoundaries(unittest.TestCase):
    def test_native_terrain_keeps_repair_jumps_and_boundaries_without_mine_bitmap(self):
        # The caller verifies the private USA stock digest. These ROM-free
        # buffers exercise preservation and rejection of altered consumers.
        stock = bytes(0x80000)
        properties = converter.native_terrain_properties(stock, stock)
        self.assertEqual(properties[0x200+0xb9], 16)  # Repair strip.
        self.assertEqual(properties[0x100+0xba], 64)  # Jump pad.
        self.assertEqual(properties[0x100+0xc3], 16)  # Dash plate.
        self.assertEqual(properties[0x300+0x20], 16)  # Barrier.
        self.assertEqual(properties[0x300+0x69], 32)  # Wall/main path.
        self.assertEqual(properties[0x300+0xd0], 128)  # Off-course void.
        self.assertEqual(properties[0x100+0xa6], 8)  # Native down-pull magnet.
        self.assertEqual(properties[0x300+0x90], 0)  # Safe road.
        for address in (0x008e36, 0x039187, 0x009c9a, 0x00eb90):
            changed = bytearray(stock)
            changed[((address & 0x7f0000) >> 1) | (address & 0x7fff)] = 1
            with self.subTest(address=address), self.assertRaisesRegex(ValueError, 'terrain classifier'):
                converter.native_terrain_properties(changed, stock)

    def test_explicit_other_game_readme_explains_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'other-game.zip'
            source.write_bytes(archive_bytes([('hack.sfc', bytes(0x8000)), ('readme.txt',
                b'Apply the IPS patch to an unmodified copy of the US/EU Maximum Velocity rom.')]))
            target, report = converter.read_submission(source)
            self.assertIsNone(target)
            self.assertIn('Game Boy Advance', report['reason'])
            self.assertEqual(report['declared_patch_target']['evidence_member'], 'readme.txt')

    def test_legacy_loader_reports_decoder_requirement(self):
        rom = bytearray(0x100000)
        classic = bytes.fromhex('08 e2 30 a9 0f a6 58 d0 06 a5 90 0a 0a 65 90 '
                                '18 65 53 aa bf 29 e1 02 8d de 0a 29 0f 8d f5 0c a8')
        rom[0x1f08:0x1f08+len(classic)] = classic
        rom[0x77e0:0x77f1] = bytes.fromhex('08 e2 30 ae d8 0a bf 71 9e 03 0a 0a 0a 7f 71 9e 03')
        result = converter.probe_fzedit_metadata(rom)
        self.assertEqual(result['loader_family'], 'legacy-stock')
        self.assertIn('new resource decoder', result['unsupported_reason'])
        self.assertNotEqual(result['status'], 'recognized-resource-metadata')

    def test_fuzee_loader_reports_decoded_inventory_without_allowing_import(self):
        from test_fuzee_course_format import fixture
        rom = fixture()
        rom[0x77e0:0x77f1] = bytes.fromhex('08 e2 30 ae d8 0a bf 71 9e 03 0a 0a 0a 7f 71 9e 03')
        result = converter.probe_fzedit_metadata(rom)
        self.assertEqual(result['loader_family'], 'fuzee-0.04')
        self.assertEqual(result['status'], 'recognized-incomplete-resource-layout')
        self.assertEqual(result['decoded_resource_inventory']['gp_entries'], 15)
        self.assertEqual(result['decoded_resource_inventory']['practice_entries'], 7)
        self.assertEqual(result['decoded_resource_inventory']['unique_road_variants'], 3)
        self.assertIn('complete course conversion is not supported', result['unsupported_reason'])
        self.assertFalse(result['executable_compatibility_verified'])

    def test_malformed_fuzee_does_not_get_a_usable_inventory(self):
        from test_fuzee_course_format import fixture, put
        rom = fixture()
        rom[0x77e0:0x77f1] = bytes.fromhex('08 e2 30 ae d8 0a bf 71 9e 03 0a 0a 0a 7f 71 9e 03')
        put(rom, 0x11fff0, b'\xff')
        result = converter.probe_fzedit_metadata(rom)
        self.assertEqual(result['loader_family'], 'fuzee-0.04')
        self.assertIn('block index', result['structural_error'])
        self.assertNotIn('decoded_resource_inventory', result)
        self.assertNotEqual(result['status'], 'recognized-resource-metadata')

    def test_table_msu_selector_follows_gp_order_and_events(self):
        rom = bytearray(metadata_donor())
        def put(address, data):
            offset = ((address & 0x7f0000) >> 1) | (address & 0x7fff)
            rom[offset:offset+len(data)] = data
        put(0x02c26a, bytes(26))
        selector = bytes.fromhex('a5 46 29 0f c9 06 f0 01 60 a5 58 f0 08 a5 53 aa bf '
                                 'd0 c2 02 60 a5 f2 18 65 53 aa bf d0 c2 02 60')
        put(0x02c2a9, selector)
        put(0x02c23b, bytes.fromhex('20 a9 c2 8d 04 20 9c 05 20'))
        put(0x008bf3, bytes.fromhex('a5 90 0a 0a 65 90 85 f2'))
        put(0x02c2d0, bytes([17, 11]))  # Not arithmetic or resource-slot order.
        probe = converter.probe_fzedit_metadata(rom)
        self.assertEqual([row['msu_track'] for row in probe['tracks']], [17, 11])
        self.assertEqual(probe['msu_selector']['events']['title'], 4)
        self.assertEqual(probe['msu_selector']['events']['lost-life'], 3)
        for address, bad in ((0x02c23b, b'\xea'), (0x008bf3, b'\xea'),
                             (0x02c2a9+28, b'\xd1'), (0x02c2d0, b'\xff')):
            offset = ((address & 0x7f0000) >> 1) | (address & 0x7fff)
            old = rom[offset]; put(address, bad)
            self.assertFalse(converter.probe_msu_selector(rom, 2)['recognized'])
            put(address, bytes([old]))

    def test_ips_bps_equivalence_and_zip_pair_selection(self):
        source, target = bytes(0x8000), b'A'+bytes(0x7fff)
        ips, bps = make_ips(source, target), bps_literal(source, target)
        with tempfile.TemporaryDirectory() as temp, patch('parse_track_pack.STOCK_SHA256', converter.digest(source)):
            path = Path(temp)/'pair.zip'
            path.write_bytes(archive_bytes([('course.ips', ips), ('course.bps', bps)]))
            result, report = converter.read_submission(path, source)
            self.assertEqual(result, target)
            self.assertEqual(report['patch_member'], 'course.bps')
            self.assertTrue(report['bps_crcs_verified'])
            self.assertEqual(len(report['archive_patch_targets']), 2)
            self.assertEqual(path.read_bytes(), archive_bytes([('course.ips', ips), ('course.bps', bps)]))

    def test_bps_crc_failure_and_missing_stock_are_rejected(self):
        source = bytes(0x8000)
        corrupt = bytearray(bps_literal(source, b'A'+source[1:])); corrupt[-1] ^= 1
        with tempfile.TemporaryDirectory() as temp, patch('parse_track_pack.STOCK_SHA256', converter.digest(source)):
            path = Path(temp)/'course.bps'; path.write_bytes(corrupt)
            with self.assertRaisesRegex(ValueError, 'CRC'):
                converter.read_submission(path, source)
            with self.assertRaisesRegex(ValueError, 'requires --stock'):
                converter.read_submission(path)

    def test_distinct_zip_targets_need_explicit_selection(self):
        source = bytes(0x8000)
        a, b = make_ips(source, b'A'+source[1:]), make_ips(source, b'B'+source[1:])
        data = archive_bytes([('a.ips', a), ('b.ips', b)])
        with patch('parse_track_pack.STOCK_SHA256', converter.digest(source)):
            with self.assertRaisesRegex(ValueError, 'distinct'):
                converter.select_patch(converter.zip_patches(data), source)
            selected, _ = converter.select_patch(converter.zip_patches(data, 'b.ips'), source)
            self.assertEqual(selected[1], 'b.ips')

    def test_archive_paths_links_and_case_aliases(self):
        for name in ('../track.ips', '/track.ips', 'C:/track.ips', 'folder\\track.ips', './track.ips', 'bad./track.ips'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'unsafe'):
                converter.zip_patches(archive_bytes([(name, b'PATCHEOF')]))
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            converter.zip_patches(archive_bytes([('track.ips', b'PATCHEOF'), ('TRACK.IPS', b'PATCHEOF')]))
        link = zipfile.ZipInfo('track.ips'); link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        with self.assertRaisesRegex(ValueError, 'link'):
            converter.zip_patches(archive_bytes([(link, b'PATCHEOF')]))

    def test_archive_member_and_expansion_bounds(self):
        data = archive_bytes([('course.ips', b'PATCHEOF'), ('readme.txt', bytes(100))])
        with patch.object(converter, 'ZIP_TOTAL_LIMIT', 100), self.assertRaisesRegex(ValueError, 'expanded'):
            converter.zip_patches(data)
        with patch.object(converter, 'ZIP_MEMBER_LIMIT', 1), self.assertRaisesRegex(ValueError, 'too many'):
            converter.zip_patches(data)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'large'; path.write_bytes(bytes(9))
            with self.assertRaisesRegex(ValueError, 'exceeds'):
                converter.read_bounded(path, 8)

    def test_disk_zip_streams_rom_and_only_audio_headers(self):
        raw = bytes(512)+bytes(0x8000)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'submission.zip'
            path.write_bytes(archive_bytes([('hack.smc', raw), ('hack-10.pcm', pcm(10000)),
                                           ('tools/optional.asm', b'not executed'), ('INSTRUCTIONS.txt', b'author notes')]))
            opened = []
            original = zipfile.ZipFile.open
            def observe(archive, entry, *args, **kwargs):
                opened.append(entry.filename if isinstance(entry, zipfile.ZipInfo) else entry)
                return original(archive, entry, *args, **kwargs)
            with patch.object(converter, 'read_bounded', side_effect=AssertionError('ZIP buffered whole')), \
                 patch.object(zipfile.ZipFile, 'open', observe):
                target, report = converter.read_submission(path)
            self.assertEqual(target, raw[512:])
            self.assertEqual(report['removed_copier_header_bytes'], 512)
            self.assertEqual(report['donor_member'], 'hack.smc')
            self.assertEqual(report['audio_inventory']['pcm_count'], 1)
            self.assertTrue(report['audio_inventory']['members'][0]['header_valid'])
            self.assertEqual(report['ignored_members'], ['tools/optional.asm', 'INSTRUCTIONS.txt'])
            self.assertNotIn('tools/optional.asm', opened)
            self.assertIn('INSTRUCTIONS.txt', opened)  # Bounded platform hints only.

    def test_reviewed_msu_companion_is_not_a_second_game_revision(self):
        stock = bytes(0x8000)
        game = b'G' + bytes(0x7fff)
        addon = b'PATCHEOF'
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'with-music.zip'
            source.write_bytes(archive_bytes([('game.sfc', game), ('fzedit-msu.ips', addon)]))
            with patch.dict(converter.MSU_COMPANION_PATCHES, {converter.digest(addon): 'Reviewed test add-on'}), \
                    patch.object(converter, 'donor', return_value=stock), \
                    patch.object(converter, 'probe_msu_selector', return_value={'recognized': False}):
                target, report = converter.read_submission(source, stock)
                self.assertEqual(target, game)
                self.assertEqual(report['donor_member'], 'game.sfc')
                self.assertEqual(report['companion_patches'][0]['member'], 'fzedit-msu.ips')
                source.write_bytes(archive_bytes([('game.sfc', game), ('fzedit-msu.ips', addon + b'changed')]))
                target, report = converter.read_submission(source, stock)
                self.assertIsNone(target)  # The familiar filename does not identify an add-on.
                self.assertIn('different donor revisions', report['reason'])

    def test_repeated_race_tables_keep_resources_music_and_cycle_bank(self):
        rom = bytearray(0x20000)
        def put(address, data):
            offset = ((address & 0x7f0000) >> 1) | (address & 0x7fff)
            rom[offset:offset + len(data)] = data
        layout = dict(count=['3'], maps=['009000'], music=['009100'],
                      opponents=['009200'], palette_cycles=['009300'])
        put(0x009000, bytes(range(30)))
        put(0x009100, bytes([0, 9, 18]))
        put(0x009200, bytes(range(9)))
        put(0x009300, bytes.fromhex('10 93 14 93 18 93'))
        for slot in range(3):
            put(0x009310 + slot * 4, struct.pack('<HH', 0x20 + slot * 16, 0x8000))
        probe = dict(tracks=[dict(slot=slot, order_table_position=i, name=f'Race {i}', msu_track=10+i)
                             for i, slot in enumerate((2, 0, 2))])
        evidence = dict(opponents_policy='Recognized donor per-resource opponent table')
        expanded, result, rows = converter.expand_repeated_races(bytes(rom), layout, probe, bytes(rom), evidence)
        read = lambda key, size: converter.span(expanded, int(result[key][0], 16), size)
        self.assertEqual(read('maps', 30), bytes(range(20, 30)) + bytes(range(10)) + bytes(range(20, 30)))
        self.assertEqual(read('music', 3), bytes([18, 0, 18]))
        self.assertEqual(read('opponents', 9), bytes([6, 7, 8, 0, 1, 2, 6, 7, 8]))
        self.assertEqual([row['slot'] for row in rows['tracks']], [0, 1, 2])
        self.assertEqual([row['source_slot'] for row in rows['tracks']], [2, 0, 2])
        self.assertEqual([row['msu_track'] for row in rows['tracks']], [10, 11, 12])
        report = dict(input_name='game.zip', target_sha256='a'*64, input_sha256='b'*64)
        original_form = converter.review_form(report, probe)
        self.assertEqual(original_form, converter.review_form(report, rows))
        self.assertEqual(len({field['id'] for field in original_form['fields']}), len(original_form['fields']))
        converter.review_values(report, original_form, None)
        cycles = int(result['palette_cycles'][0], 16)
        for index, value in enumerate((0x40, 0x20, 0x40)):
            address = (cycles & 0xff0000) | int.from_bytes(converter.span(expanded, cycles+index*2, 2), 'little')
            self.assertEqual(converter.span(expanded, address, 4), struct.pack('<HH', value, 0x8000))
        with self.assertRaisesRegex(ValueError, 'outside'):
            converter.expand_repeated_races(bytes(rom), layout,
                dict(tracks=[dict(slot=3), dict(slot=3)]), bytes(rom), evidence)
        self.assertEqual(evidence['repeated_race_resources']['unique_resources'], 2)
        self.assertEqual(layout['maps'], ['009000'])  # No caller table is rewritten.
        put(0x02fbda, bytes(range(45)))
        fallback, tables, _ = converter.expand_repeated_races(bytes(rom), layout, probe, bytes(rom),
            dict(opponents_policy='Original engine explosive-opponent frequencies by source GP position'))
        self.assertEqual(converter.span(fallback, int(tables['opponents'][0], 16), 9),
                         bytes([0, 15, 30, 1, 16, 31, 2, 17, 32]))

    def test_metadata_probe_accepts_bounded_repeated_resources(self):
        rom = bytearray(metadata_donor())
        rom[0x81110:0x81112] = bytes([1, 1])
        report = converter.probe_fzedit_metadata(rom)
        self.assertEqual(report['status'], 'recognized-resource-metadata')
        self.assertEqual(report['source_order_prefix'], [1, 1])
        rom[0x81111] = 2
        self.assertEqual(converter.probe_fzedit_metadata(rom)['status'], 'recognized-metadata-locators')

    def test_ambiguous_zip_publishes_report_and_member_resolves(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'different.zip'
            source.write_bytes(archive_bytes([('a.sfc', bytes(0x8000)), ('b.sfc', b'B'+bytes(0x7fff))]))
            with patch('subprocess.run', side_effect=AssertionError('Ambiguous donor invoked code')):
                report = converter.convert(source, root/'review')
            self.assertEqual(report['status'], 'review-required')
            self.assertIn('different donor revisions', report['reason'])
            self.assertEqual(len(report['archive_donor_candidates']), 2)
            self.assertNotIn('target_sha256', report)
            target, selected = converter.read_submission(source, patch_member='b.sfc')
            self.assertEqual(target, b'B'+bytes(0x7fff))
            self.assertEqual(selected['donor_member'], 'b.sfc')

    def test_reviewed_donor_does_not_hide_another_unknown_revision(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/'mixed.zip'
            path.write_bytes(archive_bytes([('known.sfc', bytes(0x8000)), ('new.sfc', b'B'+bytes(0x7fff))]))
            profile = ROOT/'assets/track-packs/bower-league.ini'
            with patch.object(converter, 'reviewed_profile', side_effect=lambda data: profile if data[0] == 0 else None):
                target, report = converter.read_submission(path)
            self.assertIsNone(target)
            self.assertIn('different donor revisions', report['reason'])

    def test_audio_only_archive_has_clear_review_reason(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'music.zip'
            source.write_bytes(archive_bytes([('music-1.pcm', pcm()), ('README.md', b'music notes')]))
            report = converter.convert(source, root/'review')
            self.assertIn('audio-only', report['reason'])
            self.assertEqual(report['status'], 'review-required')
            self.assertEqual(report['audio_inventory']['mapped_pcm_count'], 0)
            self.assertFalse((root/'review/music').exists())

    def test_zip_compressed_file_and_path_collision_bounds(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/'bound.zip'; source.write_bytes(archive_bytes([('hack.sfc', bytes(0x8000))]))
            with patch.object(converter, 'ZIP_ARCHIVE_LIMIT', 1), self.assertRaisesRegex(ValueError, 'Compressed ZIP'):
                converter.read_submission(source)
            with patch.object(converter, 'ZIP_FILE_LIMIT', 1), self.assertRaisesRegex(ValueError, 'expanded'):
                converter.read_submission(source)
            with patch.object(converter, 'ZIP_DIRECTORY_LIMIT', 1), self.assertRaisesRegex(ValueError, 'central directory'):
                converter.read_submission(source)
            with patch.object(converter, 'IMAGE_LIMIT', 0x7fff), self.assertRaisesRegex(ValueError, '32 KiB'):
                converter.decode_donor(bytes(0x8000), '.sfc', None)
        with self.assertRaisesRegex(ValueError, 'collision'):
            converter.zip_patches(archive_bytes([('folder', b'file'), ('folder/hack.ips', b'PATCHEOF')]))

    def audio_pack(self, root, members):
        source = root/'music.zip'; source.write_bytes(archive_bytes(members))
        pack = root/'pack'; pack.mkdir()
        index = dict(courses=[dict(id='a', source='courses/a.zip', music=dict(track=10)),
                              dict(id='b', source='courses/b.zip', music=dict(track=11))],
                     soundtracks=[dict(prefix='reviewed')], menu_music=dict(title='music/reviewed-4.pcm'))
        (pack/'courses.json').write_text(json.dumps(index))
        with zipfile.ZipFile(source) as archive:
            _, _, audio = converter.zip_inventory(archive)
        report = dict(audio_inventory=audio, donor_member='hack.ips', warnings=[])
        return source, pack, report

    def test_reviewed_audio_maps_course_and_menu_without_guessing(self):
        with tempfile.TemporaryDirectory() as temp:
            source, pack, report = self.audio_pack(Path(temp), [('reviewed-10.pcm', pcm()),
                ('nested/b.pcm', pcm(loop=2)), ('reviewed-4.pcm', pcm(16)),
                ('reviewed-99.pcm', pcm()), ('other-11.pcm', pcm()), ('notes/song.ogg', b'unsupported')])
            before = source.read_bytes()
            converter.copy_archive_audio(source, report, pack)
            self.assertEqual((pack/'music/a.pcm').read_bytes(), pcm())
            self.assertEqual((pack/'music/b.pcm').read_bytes(), pcm(loop=2))
            self.assertEqual((pack/'music/reviewed-4.pcm').read_bytes(), pcm(16))
            self.assertEqual(report['audio_inventory']['mapped_pcm_count'], 3)
            self.assertEqual(report['audio_inventory']['unmapped_pcm_count'], 2)
            self.assertEqual({p.name for p in (pack/'music').iterdir()}, {'a.pcm', 'b.pcm', 'reviewed-4.pcm'})
            self.assertTrue(any('unchanged ZIP' in warning for warning in report['warnings']))
            self.assertTrue(any('other audio' in warning for warning in report['warnings']))
            self.assertEqual(source.read_bytes(), before)

    def test_ambiguous_audio_skips_disputed_file_and_keeps_other_music(self):
        with tempfile.TemporaryDirectory() as temp:
            source, pack, report = self.audio_pack(Path(temp), [('reviewed-10.pcm', pcm()),
                ('a.pcm', pcm(16)), ('reviewed-11.pcm', pcm())])
            converter.copy_archive_audio(source, report, pack)
            self.assertFalse((pack/'music/a.pcm').exists())
            self.assertEqual((pack/'music/b.pcm').read_bytes(), pcm())
            self.assertEqual(report['audio_inventory']['mapped_pcm_count'], 1)
            self.assertTrue(any('multiple matching' in warning for warning in report['warnings']))
            self.assertEqual(len([item for item in report['audio_inventory']['members'] if 'mapping_reason' in item]), 2)

    def test_corrupt_audio_warns_without_partial_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            info = zipfile.ZipInfo('reviewed-10.pcm'); info.compress_type = zipfile.ZIP_STORED
            source, pack, report = self.audio_pack(root, [(info, pcm(20000)), ('reviewed-11.pcm', pcm())])
            with zipfile.ZipFile(source) as archive:
                entry = archive.getinfo('reviewed-10.pcm')
                offset = entry.header_offset+30+len(entry.filename.encode())+len(entry.extra)+entry.file_size-1
            corrupt = bytearray(source.read_bytes()); corrupt[offset] ^= 1; source.write_bytes(corrupt)
            converter.copy_archive_audio(source, report, pack)
            self.assertFalse((pack/'music/a.pcm').exists())
            self.assertEqual((pack/'music/b.pcm').read_bytes(), pcm())
            self.assertFalse(list(pack.rglob('*.part')))
            self.assertEqual(report['audio_inventory']['mapped_pcm_count'], 1)
            self.assertIn('CRC', report['audio_inventory']['members'][0]['copy_error'])
            self.assertTrue(any('Courses are available' in warning for warning in report['warnings']))

    def test_invalid_pcm_header_is_inventory_only(self):
        with tempfile.TemporaryDirectory() as temp:
            source, pack, report = self.audio_pack(Path(temp), [('reviewed-10.pcm', pcm(loop=9)),
                ('reviewed-11.pcm', b'not PCM'), ('reviewed-4.pcm', pcm(16))])
            converter.copy_archive_audio(source, report, pack)
            self.assertEqual(report['audio_inventory']['mapped_pcm_count'], 1)
            self.assertEqual(report['audio_inventory']['unmapped_pcm_count'], 2)
            self.assertFalse((pack/'music/a.pcm').exists())

    def test_small_corrupt_pcm_header_does_not_reject_archive_donor(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)/'small-corrupt.zip'
            info = zipfile.ZipInfo('hack-10.pcm'); info.compress_type = zipfile.ZIP_STORED
            source.write_bytes(archive_bytes([('hack.sfc', bytes(0x8000)), (info, pcm())]))
            with zipfile.ZipFile(source) as archive:
                entry = archive.getinfo('hack-10.pcm')
                offset = entry.header_offset+30+len(entry.filename.encode())+len(entry.extra)+8
            raw = bytearray(source.read_bytes()); raw[offset] ^= 1; source.write_bytes(raw)
            target, report = converter.read_submission(source)
            self.assertEqual(target, bytes(0x8000))
            recording = report['audio_inventory']['members'][0]
            self.assertFalse(recording['header_valid'])
            self.assertIn('CRC', recording['header_error'])

    def test_duplicate_audio_publication_budget_keeps_courses(self):
        with tempfile.TemporaryDirectory() as temp:
            source, pack, report = self.audio_pack(Path(temp), [('reviewed-10.pcm', pcm())])
            index = json.loads((pack/'courses.json').read_text())
            index['courses'][1]['music']['track'] = 10
            (pack/'courses.json').write_text(json.dumps(index))
            with patch.object(converter, 'ZIP_TOTAL_LIMIT', len(pcm())+1):
                converter.copy_archive_audio(source, report, pack)
            self.assertFalse((pack/'music').exists())
            self.assertTrue((pack/'courses.json').is_file())
            self.assertEqual(report['audio_inventory']['mapped_pcm_count'], 0)
            self.assertTrue(any('publication limit' in warning for warning in report['warnings']))

    def test_generic_metadata_probe_is_evidence_not_qualification(self):
        donor = metadata_donor(); report = converter.probe_fzedit_metadata(donor)
        self.assertEqual(report['status'], 'recognized-resource-metadata')
        self.assertEqual(report['internal_resource_count'], 2)
        self.assertEqual(report['source_order_prefix'], [1, 0])
        self.assertEqual([row['name'] for row in report['tracks']], ['Q', 'J'])
        self.assertEqual([row['spc_index'] for row in report['tracks']], [8, 1])
        self.assertEqual([row['msu_track'] for row in report['tracks']], [12, 13])
        self.assertFalse(report['executable_compatibility_verified'])
        self.assertFalse(report['cup_labels_verified'])
        changed = bytearray(donor); changed[0x77e0] = 0
        self.assertEqual(converter.probe_fzedit_metadata(changed)['status'], 'unrecognized-loader')
        changed = bytearray(donor); changed[0x1426a] = 0
        report = converter.probe_fzedit_metadata(changed)
        self.assertFalse(report['msu_selector']['recognized'])
        self.assertFalse(any('msu_track' in row for row in report['tracks']))

    def test_older_fzedit_relocated_consumers_preserve_venue_variant_music(self):
        donor=older_metadata_donor()
        report=converter.probe_fzedit_metadata(donor)
        self.assertEqual(report['status'],'recognized-resource-metadata')
        self.assertEqual(report['loader_family'],'fzedit-older')
        self.assertEqual(report['source_order_prefix'],[1,0])
        self.assertEqual([row['spc_index'] for row in report['tracks']],[7,3])
        self.assertEqual([row['donor_music_index'] for row in report['tracks']],[9,3])
        self.assertEqual([row['name'] for row in report['tracks']],["B","A'"])
        changed=bytearray(donor);changed[0x1f08]=0
        self.assertEqual(converter.probe_fzedit_metadata(changed)['status'],'unrecognized-loader')
        changed=bytearray(donor);changed[0x19e68+9]=255
        self.assertEqual(converter.probe_fzedit_metadata(changed)['status'],'unrecognized-loader')
        changed=bytearray(donor);changed[0x81800:0x81810]=donor[0x81050:0x81060]
        self.assertEqual(converter.probe_fzedit_metadata(changed)['status'],'unrecognized-loader')

    def test_older_inline_minimap_count_requires_exact_resource_boundaries(self):
        donor=older_metadata_donor(inline=True)
        report=converter.probe_fzedit_metadata(donor)
        self.assertEqual(report['internal_resource_count'],2)
        self.assertEqual(report['minimap_constants'],[12,211])
        changed=bytearray(donor)
        offset=(int(report['minimap_table'],16)&0x7fff)+0x80000
        changed[offset-1]=0
        self.assertEqual(converter.probe_fzedit_metadata(changed)['status'],'unrecognized-loader')

    def test_native_horizon_rle_decodes_all_modes_and_rejects_bad_lengths(self):
        rom=bytearray(0x8000)
        stream=bytes.fromhex('00 11 09 22 02 33 02 07 fc')
        rom[:len(stream)]=stream
        expected=bytes.fromhex('11 18 22 18 22 18 33 18 33 18 33 18 80 1d 80 1d')
        self.assertEqual(converter.decode_native_horizon(rom,0x008000,len(expected)),expected)
        for size in (len(expected)-2,len(expected)+2):
            with self.assertRaises(ValueError):converter.decode_native_horizon(rom,0x008000,size)
        rom[len(stream)-1]=0
        with self.assertRaises(ValueError):converter.decode_native_horizon(rom,0x008000,len(expected))

    def test_inference_reads_consumers_and_explicit_canonical_opponents(self):
        rom=inferable_donor(); probe=converter.probe_fzedit_metadata(rom)
        font={code:(0x0f9000+code*16,2,bytes(16)) for code in (0x6c,0x7c,0x6d,0x7d)}
        with patch.object(converter,'atlas',return_value=font):
            normalized,layout,evidence=converter.infer_fzedit_layout(rom,rom,probe)
        self.assertEqual(layout['pools'],['109540'])
        self.assertEqual(layout['maps'],['10965d'])
        self.assertEqual(layout['palette_cycles'],['109700'])
        self.assertEqual(layout['opponents'],['208000'])
        self.assertEqual(normalized[:len(rom)],rom)
        self.assertEqual(converter.span(normalized,0x208000,6),bytes([1,16,31,0,15,30]))
        self.assertFalse(evidence['unsupported_global_code_executed'])
        damaged=bytearray(rom);damaged[0x2040]=0
        with patch.object(converter,'atlas',return_value=font),self.assertRaisesRegex(ValueError,'palettes'):
            converter.infer_fzedit_layout(damaged,rom,probe)
        feature=bytearray(rom)
        masks=((0x98b1,'29 04'),(0x98bc,'29 f4'),(0x98ce,'29 14'),(0x98f1,'29 f4'),(0x98f9,'89 04'))
        for address,opcodes in masks:
            feature[address&0x7fff:(address&0x7fff)+2]=bytes.fromhex(opcodes)
        with patch.object(converter,'atlas',return_value=font):
            _,declared,evidence=converter.infer_fzedit_layout(feature,rom,probe)
        self.assertEqual(declared['require'],['all|grip-magnets'])
        self.assertIn('five recognized',evidence['mechanics'][0])

    def test_recognized_cup_menu_reads_bounded_words_and_partial_names(self):
        rom=bytearray(metadata_donor())
        def put(address,value):
            offset=((address&0x7f0000)>>1)|(address&0x7fff)
            rom[offset:offset+len(value)]=value
        put(0x03897c,bytes.fromhex('c2 20 a9 70 05 8d 20 04 bf a0 97 10 8d 22 04 a2 10 8e 24 04 a9 1a 00 8d 26 04'))
        put(0x1097a0,bytes.fromhex('a4 97 be 97'))
        for address,codes in ((0x1097a4,[0xaf,0xd4,0xd5,0xd3,0xaf]),(0x1097be,[0xc7,0xd3,0xd0,0xce,0xd5])):
            put(address,b''.join(bytes([code,8]) for code in codes+[0xff]*(13-len(codes))))
        menu=converter.probe_cup_menu(rom)
        self.assertEqual(menu['cup_count'],2)
        self.assertEqual([row['name'] for row in menu['labels']],['ASTRA','FRONT'])
        put(0x1097a0,bytes.fromhex('a5 97 be 97'))
        self.assertIsNone(converter.probe_cup_menu(rom))

    def form_fixture(self):
        probe=dict(tracks=[dict(slot=i,name=f'Extracted course {i}',spc_index=i%10) for i in range(10)])
        report=dict(target_sha256='a'*64,input_sha256='b'*64,input_name='source.zip')
        return report,probe,converter.review_form(report,probe)

    def test_review_form_uses_extracted_names_and_simple_cup_choices(self):
        report,probe,form=self.form_fixture()
        self.assertEqual(len(form['fields']),14)
        self.assertEqual(form['fields'][2]['value'],'Cup 1')
        assignments=[field for field in form['fields'] if field['type']=='choice']
        self.assertEqual([field['value'] for field in assignments],['cup-1']*5+['cup-2']*5)
        self.assertTrue(all('description' not in field for field in assignments))
        self.assertIn('Extracted course 0',assignments[0]['label'])
        self.assertIn('1–5',form['description'])
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'answers.json'
            path.write_text(json.dumps(dict(target_sha256=report['target_sha256'],input_sha256=report['input_sha256'],
                values=dict(pack_name='My courses',cup_1_name='First',cup_2_name='Second',cup_for_slot_0='cup-2',cup_for_slot_5='cup-1'))))
            values=converter.review_values(report,form,path)
        data=converter.inferred_manifest(report,probe,values)
        self.assertEqual(data['cup'],['cup-1|First|0','cup-2|Second|1'])
        self.assertEqual(data['track'][0],'course-1|Extracted course 0|cup-2|0')
        original_identity=data['id'];values['pack_name']='Renamed'
        self.assertEqual(converter.inferred_manifest(report,probe,values)['id'],original_identity)

    def test_review_answers_reject_stale_hashes_invalid_fields_and_cup_overflow(self):
        report,_,form=self.form_fixture()
        valid=dict(target_sha256=report['target_sha256'],input_sha256=report['input_sha256'],values={})
        cases=[]
        for key in ('target_sha256','input_sha256'):
            item=deepcopy(valid);item[key]='c'*64;cases.append(item)
        for values in ({'arbitrary_layout':'108000'},{'pack_name':'bad\nname'},{'author':'é'*49},
                       {'pack_name':'x'*257},{'pack_name':42},{'cup_for_slot_0':'missing'},
                       {'cup_for_slot_0':'cup-2'}):
            item=deepcopy(valid);item['values']=values;cases.append(item)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'answers.json'
            for item in cases:
                path.write_text(json.dumps(item))
                with self.subTest(item=item),self.assertRaises(ValueError):
                    converter.review_values(report,form,path)

    def test_inferred_review_lifecycle_and_structural_failure_are_distinct(self):
        report,probe,_=self.form_fixture()
        report['input_name']='new.sfc'
        probe.update(status='recognized-resource-metadata',internal_resource_count=10,source_order_prefix=list(range(10)))
        layout=dict(format=['fzero-course-1'],count=['10'])
        evidence=dict(opponents_policy='Canonical opponents explicitly chosen')
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'new.sfc';source.write_bytes(bytes(0x8000))
            tool=root/'native.exe';tool.write_bytes(b'not executed')
            def export(*args,**kwargs):
                pack=Path(args[3]);pack.mkdir(parents=True);(pack/'courses.json').write_text('{}')
                return dict(record_hashes={})
            with patch.object(converter,'read_stock',return_value=bytes(0x8000)), \
                 patch.object(converter,'read_submission',side_effect=lambda *args:(bytes(0x8000),deepcopy(report))), \
                 patch.object(converter,'reviewed_profile',return_value=None), \
                 patch.object(converter,'probe_fzedit_metadata',return_value=probe), \
                 patch.object(converter,'infer_fzedit_layout',return_value=(bytes(0x8000),layout,evidence)), \
                 patch.object(converter,'export_pack',side_effect=export), \
                 patch.object(converter,'inspect_roundtrip',side_effect=lambda *args:dict(byte_exact_to_donor_courses=[],record_hashes={})):
                self.assertEqual(converter.main([str(source),'--stock',str(source),'--out',str(root/'review'),
                    '--exporter',str(tool),'--inspector',str(tool)]),3)
                pending=json.loads((root/'review/conversion-report.json').read_text())
                self.assertEqual(pending['status'],'needs-input')
                self.assertEqual({item.name for item in (root/'review').iterdir()},{'conversion-report.json','REVIEW.txt'})
                answers=root/'answers.json';answers.write_text(json.dumps(dict(target_sha256=report['target_sha256'],
                    input_sha256=report['input_sha256'],values={})))
                completed=converter.convert(source,root/'pack',stock=source,exporter=tool,inspector=tool,answers=answers)
                self.assertEqual(completed['status'],'converted')
                self.assertTrue((root/'pack/courses.json').is_file())
                with patch.object(converter,'export_pack',side_effect=ValueError('Invalid checkpoint resource')):
                    failed=converter.convert(source,root/'failed',stock=source,exporter=tool,inspector=tool)
                self.assertEqual(failed['status'],'review-required')
                self.assertIn('checkpoint',failed['structural_error'])
                self.assertNotIn('review',failed)
            self.assertEqual(source.read_bytes(),bytes(0x8000))

    def test_unknown_rom_is_report_only_and_exit_two(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'new-hack.sfc'
            raw = bytes(512)+bytes(0x8000); source.write_bytes(raw)
            output = root/'review'
            with patch('subprocess.run', side_effect=AssertionError('Unknown donor invoked code')):
                self.assertEqual(converter.main([str(source), '--out', str(output)]), 2)
            self.assertEqual(source.read_bytes(), raw)
            self.assertEqual({p.name for p in output.iterdir()}, {'conversion-report.json', 'REVIEW.txt'})
            report = json.loads((output/'conversion-report.json').read_text())
            self.assertEqual(report['status'], 'review-required')
            self.assertEqual(report['removed_copier_header_bytes'], 512)
            self.assertFalse(report['source_rom_exported'])
            self.assertFalse(report['donor_code_executed'])

    def test_existing_output_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)/'existing'; out.mkdir(); (out/'sentinel').write_bytes(b'owner content')
            with self.assertRaisesRegex(ValueError, 'Refusing'):
                converter.convert(Path(temp)/'missing.ips', out)
            self.assertEqual((out/'sentinel').read_bytes(), b'owner content')

    def test_failed_known_export_publishes_no_partial_pack(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'known.sfc'; source.write_bytes(bytes(0x8000))
            tool = root/'native.exe'; tool.write_bytes(b'not executed')
            out = root/'not-published'
            profile = ROOT/'assets/track-packs/bower-league.ini'
            workspaces = []
            def fail_export(*args, **kwargs):
                workspaces.append(Path(args[3]).parents[1])
                raise ValueError('Invalid course resource')
            with patch.object(converter, 'read_stock', return_value=bytes(0x8000)), \
                 patch.object(converter, 'reviewed_profile', return_value=profile), \
                 patch.object(converter, 'audit_metadata', return_value={}), \
                 patch.object(converter, 'export_pack', side_effect=fail_export):
                with self.assertRaisesRegex(ValueError, 'Invalid course'):
                    converter.convert(source, out, stock=source, exporter=tool, inspector=tool)
            self.assertFalse(out.exists())
            self.assertEqual(len(workspaces), 1)
            self.assertFalse(workspaces[0].exists())
            self.assertEqual(source.read_bytes(), bytes(0x8000))

    def check_publication(self, root, *, fail_copy=False):
        source = root/'known.sfc'; source.write_bytes(bytes(0x8000))
        tool = root/'native.exe'; tool.write_bytes(b'not executed')
        out = root/('nested-install-stage-'+'x'*80)/'mods/.imports/123456789012345-0/converted'
        profile = ROOT/'assets/track-packs/bower-league.ini'
        workspaces = []
        def export(*args, **kwargs):
            pack = Path(args[3]); pack.mkdir(parents=True)
            (pack/'pack.json').write_bytes(b'complete validated pack')
            workspaces.append(pack.parents[1])
            return dict(record_hashes={})
        def inspect(inspector, temp, pack, *args):
            # Model the native loader's deepest cache member path. A workspace
            # under this destination reproduces the Windows failure (>260).
            suffix = Path('mods/packs/.cache/sources')/('a'*64)/'brutal-wind-1_Horizon_Tilemap.gif'
            if len(str(temp/suffix)) >= 260:
                raise ValueError('Cannot create cached asset: Windows path limit')
            return dict(native_loader_passed=True)
        copy = converter.shutil.copytree
        def copy_pack(source, dest):
            if fail_copy:
                Path(dest).mkdir(); (Path(dest)/'partial').write_bytes(b'incomplete copy')
                raise OSError('Publication disk is full')
            return copy(source, dest)
        with patch.object(converter, 'read_stock', return_value=bytes(0x8000)), \
             patch.object(converter, 'reviewed_profile', return_value=profile), \
             patch.object(converter, 'audit_metadata', return_value={}), \
             patch.object(converter, 'export_pack', side_effect=export), \
             patch.object(converter, 'inspect_roundtrip', side_effect=inspect), \
             patch.object(converter.shutil, 'copytree', side_effect=copy_pack):
            if fail_copy:
                with self.assertRaisesRegex(OSError, 'disk is full'):
                    converter.convert(source, out, stock=source, exporter=tool, inspector=tool)
                self.assertFalse(out.exists())
            else:
                report = converter.convert(source, out, stock=source, exporter=tool, inspector=tool)
                self.assertEqual(report['status'], 'converted')
                self.assertEqual((out/'pack.json').read_bytes(), b'complete validated pack')
        self.assertFalse(workspaces[0].exists())
        self.assertFalse(list(out.parent.glob('.fzc-publish-*')))
        self.assertEqual(source.read_bytes(), bytes(0x8000))

    def test_deep_destination_keeps_native_cache_below_windows_path_limit(self):
        with tempfile.TemporaryDirectory() as temp:
            self.check_publication(Path(temp))

    def test_publication_copy_failure_leaves_no_partial_pack(self):
        with tempfile.TemporaryDirectory() as temp:
            self.check_publication(Path(temp), fail_copy=True)

    def test_duplicate_and_unsafe_registry_identities(self):
        data = fields(ROOT/'assets/track-packs/bower-league.ini')
        converter.validate_identities(data)
        variants = []
        duplicate = deepcopy(data); duplicate['cup'].append(duplicate['cup'][0]); variants.append(duplicate)
        duplicate = deepcopy(data); duplicate['track'].append(duplicate['track'][0]); variants.append(duplicate)
        unsafe = deepcopy(data); unsafe['id'] = ['../pack']; variants.append(unsafe)
        missing = deepcopy(data); missing['track'][0] = missing['track'][0].replace('|bower|', '|absent|'); variants.append(missing)
        slot = deepcopy(data); slot['track'][0] = slot['track'][0][:-1]+slot['track'][1][-1]; variants.append(slot)
        for data in variants:
            with self.subTest(data=data), self.assertRaises(ValueError):
                converter.validate_identities(data)

    def test_unknown_diff_report_is_bounded(self):
        summary = converter.changed_summary(bytes(1000), bytes([1, 0])*500)
        self.assertEqual(summary['changed_stock_bytes'], 500)
        self.assertEqual(summary['changed_range_count'], 500)
        self.assertEqual(len(summary['changed_ranges']), 128)
        self.assertTrue(summary['changed_ranges_truncated'])

    def test_native_presentation_changes_fail_even_with_same_record_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary); pack = temp/'packs/example'; pack.mkdir(parents=True)
            baseline = temp/'native'; baseline.mkdir(); (baseline/'course.fzc').write_bytes(b'course')
            (pack/'courses.json').write_text(json.dumps(dict(id='example', courses=[dict(id='course')])))
            record_hash = '0'*64
            def inspect(*args, **kwargs):
                dump = temp/'roundtrip'; (dump/'example--course.fzc').write_bytes(b'changed title glyph')
                return subprocess.CompletedProcess(args[0], 0, f'example/course {record_hash}\n', '')
            with patch('subprocess.run', side_effect=inspect), self.assertRaisesRegex(ValueError, 'native course fields'):
                converter.inspect_roundtrip('inspector.exe', temp, pack, baseline, dict(record_hashes={'course': record_hash}))


if __name__ == '__main__':
    unittest.main()
