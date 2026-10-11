"""ROM-free coverage of the legacy-to-editable resource adapter."""
import struct
import unittest

from test_fuzee_course_format import fixture, put
import fuzee_course_import as adapter
from native_course_format import native_terrain_properties
from convert_course_content import probe_fzedit_metadata
from reconstruct_fzedit_course import ALPHABET


def metadata_fixture():
    rom = fixture()
    put(rom, 0x009fbd, adapter.MUSIC_POSITION)
    put(rom, 0x00f7e0, adapter.MUSIC_LOADER)
    put(rom, 0x039e71, bytes(range(9)) + bytes(6))
    put(rom, 0x00abcb, adapter.NAME_LOADER)
    put(rom, 0x00ac42, struct.pack('<9H', *([0xae00] * 9)))
    put(rom, 0x00ae00, bytes.fromhex('10 53 01 81 1b ff') + bytes([ALPHABET[0], 0xfe, 0xff, 0]))
    return rom


class FuzeeImport(unittest.TestCase):
    def test_relocated_music_table_preserves_actual_theme(self):
        rom = metadata_fixture()
        code = bytearray(adapter.MUSIC_LOADER)
        code[7:10] = code[14:17] = bytes.fromhex('00 92 03')
        put(rom, 0x00f7e0, code)
        put(rom, 0x039200, bytes([4] * 15))
        report = probe_fzedit_metadata(rom)
        self.assertEqual(report['status'], 'recognized-resource-metadata')
        self.assertEqual(report['music_table'], '039200')
        self.assertEqual(report['tracks'][0]['spc_selector_byte'], 36)
        self.assertEqual(report['internal_resource_count'], 15)

    def test_mismatched_music_operands_and_changed_arithmetic_rejected(self):
        for offset in (14, 10):
            with self.subTest(offset=offset):
                rom = metadata_fixture()
                code = bytearray(adapter.MUSIC_LOADER)
                code[offset] ^= 1
                put(rom, 0x00f7e0, code)
                with self.assertRaisesRegex(ValueError, 'SPC selector'):
                    adapter.probe_metadata(rom)

    def test_native_music_variation_positions(self):
        self.assertEqual([adapter.music_position(c) for c in (0xe0, 0xc3, 0xe3, 0xc7, 0xd7, 0xe7, 0xe6)],
                         [0, 3, 9, 7, 12, 13, 6])

    def test_native_name_suffix_keeps_mute_city_variations(self):
        rom = metadata_fixture()
        self.assertEqual([adapter.native_name(rom, c)[1] for c in (0xc7, 0xd7, 0xe7)],
                         ['AI', 'AII', 'AIII'])
        put(rom, 0x00abcb, b'\xea')
        with self.assertRaisesRegex(ValueError, 'name consumer'):
            adapter.native_name(rom, 0xc7)

    def test_practice_only_route_is_not_silently_discarded(self):
        rom = metadata_fixture()
        put(rom, 0x10801e, b'\x00\x98')
        with self.assertRaises(ValueError):
            adapter.probe_metadata(rom)

    def test_inverse_grid_preserves_ninth_word_and_unaligned_offsets(self):
        rows = struct.pack('<18H', *range(0x101, 0x109), 0xabcd,
                           *range(0x201, 0x209), 0x246e)
        packed = adapter.packed_grid(rows)
        expanded = bytearray()
        for at in range(0, len(packed), 16):
            words = struct.unpack_from('<8H', packed, at)
            expanded.extend(struct.pack('<9H', *(w >> 2 for w in words),
                                        sum((w & 3) << (i * 2) for i, w in enumerate(words))))
        self.assertEqual(expanded, rows)
        with self.assertRaisesRegex(ValueError, 'pointer range'):
            adapter.packed_grid(struct.pack('<9H', 0x4000, *([0] * 8)))
        with self.assertRaisesRegex(ValueError, 'address window'):
            adapter.packed_grid(bytes(0x9001))

    def test_pool_reindex_keeps_pixel_quads_and_animated_pointers(self):
        rom = fixture()
        put(rom, 0x0cc380 + 0x246e, b'\x11\x22\x33\x44')
        put(rom, 0x0cc380 + 0x2470, b'\x33\x44\x55\x66')
        rows = struct.pack('<5H', 0x246e, 0x2470, 0x246e, 0x3ff0, 0x3ffc)
        pool, changed = adapter.normalized_pool_rows(rom, rows)
        offsets = struct.unpack('<5H', changed)
        self.assertEqual(pool[offsets[0]:offsets[0]+4], b'\x11\x22\x33\x44')
        self.assertEqual(pool[offsets[1]:offsets[1]+4], b'\x33\x44\x55\x66')
        self.assertEqual(offsets[0], offsets[2])
        self.assertEqual(offsets[3:], (0x3ff0, 0x3ffc))
        with self.assertRaisesRegex(ValueError, 'animated'):
            adapter.normalized_pool_rows(rom, struct.pack('<H', 0x3ff1))

    def test_minimap_places_complete_tiles_and_rejects_partial_or_duplicate(self):
        rom = fixture()
        tiles = bytes(range(256)) * 2
        put(rom, 0x0fd000, bytes([0xa0, 224]) + tiles + bytes(2))
        self.assertEqual(adapter.minimap(rom, 0x0fd000), tiles)
        put(rom, 0x0fd000, bytes([0x81, 224]) + bytes(16) + bytes(2))
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            adapter.minimap(rom, 0x0fd000)
        put(rom, 0x0fd000, bytes([0x81, 224]) + bytes(16) + bytes([0x81, 224]) + bytes(18))
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            adapter.minimap(rom, 0x0fd000)
        put(rom, 0x0fd000, bytes([0xe0, 224]))
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            adapter.minimap(rom, 0x0fd000)

    def test_equivalent_classifier_call_keeps_terrain_but_modified_tests_fail(self):
        stock = bytearray(0x40000)
        put(stock, 0x008eaa, bytes.fromhex('4b 62 03 00 5c 87 91 03 60'))
        donor = bytearray(stock)
        put(donor, 0x008eaa, bytes.fromhex('22 87 91 03 60 ea ea ea ea'))
        self.assertEqual(native_terrain_properties(donor, stock), native_terrain_properties(stock, stock))
        for address in (0x008e36, 0x039187, 0x009c9a, 0x00eb90):
            with self.subTest(address=address):
                changed = bytearray(donor)
                put(changed, address, b'\x01')
                with self.assertRaisesRegex(ValueError, 'terrain classifier'):
                    native_terrain_properties(changed, stock)


if __name__ == '__main__':
    unittest.main()
