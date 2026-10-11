"""Translate verified Fuzee/native resources to the existing typed exporter.

No donor instructions are run or installed. The original editor documents the
road dictionaries and chip pool; unchanged native consumers supply presentation.
Unknown consumers fail before a pack is published, just like FZEdit imports.
"""
import hashlib
import struct

from audit_track_metadata import SONGS, span
from audit_intro_font import atlas
from fuzee_course_format import decode, region_resources, variant_blocks, word
from native_course_format import native_terrain_properties, decode_native_horizon
from reconstruct_fzedit_course import ALPHABET


NAME_LOADER = bytes.fromhex(
    'ad f5 0c ae ff 0c 0a a8 b9 42 ac 85 00 b9 43 ac 85 01 '
    'a0 00 b1 00 99 80 04 f0 03 c8 80 f6 8a f0 1f b9 7f 04 '
    '1a d0 19 a9 fe 99 7f 04 ad f5 0c c9 07 d0 0d 8a 29 01 '
    'd0 08 99 81 04 a9 fe 99 80 04')
MUSIC_POSITION = bytes.fromhex(
    'e2 30 ad f5 0c c9 03 90 0f c9 06 f0 0b ae ff 0c f0 06 '
    '18 6d ff 0c 69 04 8d d8 0a 28 60')
MUSIC_LOADER = bytes.fromhex('08 e2 30 ae d8 0a bf 71 9e 03 0a 0a 0a 7f 71 9e 03')


def music_table(target):
    """Read the two matching long operands of the native SPC selector."""
    code = span(target, 0x00f7e0, len(MUSIC_LOADER))
    if (code[:7] != MUSIC_LOADER[:7] or code[10:14] != MUSIC_LOADER[10:14]
            or code[7:10] != code[14:17]):
        raise ValueError('Unsupported Fuzee native SPC selector')
    address = int.from_bytes(code[7:10], 'little')
    span(target, address, 15)
    return address


def native_name(target, setting):
    """Apply the verified native I/II/III suffix procedure, not file guesses."""
    if span(target, 0x00abcb, len(NAME_LOADER)) != NAME_LOADER:
        raise ValueError('Unsupported Fuzee course-name consumer')
    venue, variation = setting & 15, (setting >> 4) - 12
    address = word(span(target, 0x00ac42 + venue * 2, 2))
    text = bytearray(span(target, address, 128).split(b'\0', 1)[0])
    if (len(text) < 6 or text[1:3] != b'\x53\x01' or text[4:6] != b'\x1b\xff'
            or len(text) > 124):
        raise ValueError('Unsupported Fuzee course-name encoding')
    if variation and text[-1] == 0xff:
        text[-1] = 0xfe
        if venue == 7 and not variation & 1:
            text.append(0xfe)
    alphabet = dict(zip(ALPHABET, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'))
    alphabet.update({0x80 + i: str(i) for i in range(10)})
    alphabet.update({0xff: ' ', 0xfe: 'I', 0x1b: '.', 0x28: "'", 0xa9: '?'})
    try:
        display = ''.join(alphabet[code] for code in text[6:]).strip()
    except KeyError:
        display = None  # The existing review form asks for unknown glyph names.
    return bytes(text) + bytes(128 - len(text)), display


def music_position(setting):
    venue, variation = setting & 15, (setting >> 4) - 12
    return venue + variation + 4 if venue >= 3 and venue != 6 and variation else venue


def probe_metadata(target):
    audit, maps = decode(target)
    if span(target, 0x009fbd, len(MUSIC_POSITION)) != MUSIC_POSITION:
        raise ValueError('Unsupported Fuzee native SPC selector')
    songs = music_table(target)
    tracks = []
    settings = span(target, 0x02e129, 15)
    for course, setting in zip(audit['courses'][:15], settings):
        slot = course['slot']
        _, name = native_name(target, setting)
        index = music_position(setting)
        song = span(target, songs + index, 1)[0]
        if song >= len(SONGS):
            raise ValueError('Invalid Fuzee native SPC theme')
        row = dict(slot=slot, order_table_position=slot, setting=setting,
                   venue=course['venue'], variant=course['variant'], donor_music_index=index,
                   spc_selector_byte=song * 9, spc_index=song, stock_spc_theme=SONGS[song])
        if name:
            row['name'] = name
        else:
            row['name_error'] = 'The native intro uses a glyph without a known text label.'
        tracks.append(row)
    # Practice is another selector for these courses, not seven extra courses.
    for course in audit['courses'][15:]:
        if not any(course['map'] == gp['map'] and course['checkpoints'] == gp['checkpoints']
                   for gp in audit['courses'][:15]):
            raise ValueError('Separate Fuzee practice-only routes need another adapter')
    return dict(status='recognized-resource-metadata', loader_family='fuzee-0.04',
                executable_compatibility_verified=False, cup_labels_verified=False,
                intro_font_verified=False, music_table=f'{songs:06x}',
                internal_resource_count=15, published_course_count=15,
                source_order_prefix=list(range(15)), tracks=tracks,
                count_evidence='Verified Fuzee fifteen GP entries; seven practice aliases',
                decoded_resource_inventory=dict(gp_entries=15, practice_entries=7,
                    unique_road_variants=len(maps),
                    checkpoint_sections=sum(len(c['checkpoints']) for c in audit['courses']),
                    decoded_fields=audit['decoded_fields'], remaining_fields=audit['remaining_fields']))


def packed_grid(rows):
    """Invert the typed 16->18-byte decoder, retaining every unaligned offset.

    Fuzee already stores runtime words. The typed exporter expands eight packed
    words plus their two low bits into nine runtime words. Packing the ninth
    word as those bits makes the extracted dictionary identical to Fuzee's.
    """
    data = rows + bytes(-len(rows) % 18)
    if len(data) > 0x9000:
        raise ValueError('Fuzee rows exceed the runtime address window')
    output = bytearray()
    for at in range(0, len(data), 18):
        values = struct.unpack_from('<9H', data, at)
        if any(v > 0x3fff for v in values[:8]):
            raise ValueError('Fuzee chip definition exceeds the typed pointer range')
        output.extend(struct.pack('<8H', *(v * 4 | ((values[8] >> (i * 2)) & 3)
                                           for i, v in enumerate(values[:8]))))
    return bytes(output)


def normalized_pool_rows(target, rows):
    """Preserve editor chip pixels while fitting the runtime's bounded pool.

    Native chip definitions can overlap and reach beyond the FZEdit pool size.
    Reindex the used four-byte definitions; animated mine pointers stay native.
    Neither row sharing nor independently mutable mine rows is changed.
    """
    pool, quads, offsets, normalized = bytearray(), {}, {}, bytearray()
    for (offset,) in struct.iter_unpack('<H', rows):
        if 0x3ff0 <= offset <= 0x3ffc and offset % 4 == 0:
            replacement = offset
        else:
            if offset >= 0x3ff0:
                raise ValueError('Invalid Fuzee animated chip pointer')
            if offset not in offsets:
                quad = bytes(span(target, 0x0cc380 + offset, 4))
                if quad not in quads:
                    overlap = next((n for n in (3, 2, 1) if len(pool) >= n and pool[-n:] == quad[:n]), 0)
                    quads[quad] = len(pool) - overlap
                    pool.extend(quad[overlap:])
                offsets[offset] = quads[quad]
            replacement = offsets[offset]
        normalized.extend(struct.pack('<H', replacement))
    if len(pool) > 0x2400:
        raise ValueError('Fuzee chip definitions exceed the runtime pool')
    return bytes(pool) + bytes(0x2400 - len(pool)), bytes(normalized)


def minimap(target, address):
    """Decode native tiles 224..255 into the same 2bpp minimap upload."""
    output, seen = bytearray(0x200), set()
    for _ in range(257):
        header, first = span(target, address, 2)
        if not header:
            if seen != set(range(224, 256)):
                raise ValueError('Incomplete Fuzee minimap tiles')
            return bytes(output)
        count, mode = header & 63, header >> 6
        if not count or first < 224 or first + count > 256 or mode == 3:
            raise ValueError('Unsupported Fuzee minimap stream')
        address += 2
        for tile in range(first, first + count):
            if tile in seen:
                raise ValueError('Duplicate Fuzee minimap tile')
            seen.add(tile)
            length = (32, 8, 16)[mode]
            data = span(target, address, length)
            output[(tile - 224) * 16:(tile - 223) * 16] = bytes(16) if mode == 1 else data[:16]
            address += length
    raise ValueError('Unterminated Fuzee minimap stream')


def infer_layout(target, stock, probe):
    """Build private, data-only tables for the existing bounded exporter."""
    audit, _ = decode(target)
    # Comparison excludes tables and graphics themselves: those are the donor's
    # assets, not stock replacements. Only their consumers must be understood.
    for address, size, label in ((0x00a0b0, 93, 'road graphics'),
            (0x00a10d, 0x26, 'road palette/gradient'),
            (0x039717, 0xd3, 'compressed minimap'),
            (0x039806, 140, 'compressed horizon'),
            (0x00c1cf, 130, 'palette cycles'), (0x00d9c8, 0x3c, 'shortcut dispatch'),
            (0x00d146, 80, 'intro glyphs')):
        if span(target, address, size) != span(stock, address, size):
            raise ValueError(f'Unsupported Fuzee {label} consumer')
    sky = span(stock, 0x00a48c, 0xcc)
    # Direct JSLs replace stock PHK/JSR trampolines without changing decoding.
    direct_sky = sky.replace(bytes.fromhex('4b 20 fc 9e'), bytes.fromhex('22 06 98 03'))
    if span(target, 0x00a48c, 0xcc) not in (sky, direct_sky):
        raise ValueError('Unsupported Fuzee horizon DMA consumer')
    if span(target, 0x00a561, 7) != span(stock, 0x00a561, 7):
        raise ValueError('Unsupported Fuzee horizon DMA descriptor')
    mini_loader = span(target, 0x008895, 42)
    direct_mini = span(stock, 0x008895, 42).replace(
        bytes.fromhex('4b 20 f8 9e'), bytes.fromhex('22 17 97 03'))
    if (mini_loader not in (span(stock, 0x008895, 42), direct_mini)
            or span(target, 0x009ef8, 4) != span(stock, 0x009ef8, 4)):
        raise ValueError('Unsupported Fuzee minimap pointer consumer')
    # Verify the relocated checkpoint/opponent consumers. Fuzee changes only
    # the checkpoint bank and the two route-table words in this routine.
    path_loader = bytearray(span(target, 0x00d609, 0x8b))
    for address, value in ((0x00d60c, b'\x10'), (0x00d626, b'\x1e\x80'), (0x00d648, b'\x00\x80')):
        if path_loader[address - 0x00d609:address - 0x00d609 + len(value)] != value:
            raise ValueError('Unsupported Fuzee checkpoint locator')
        path_loader[address - 0x00d609:address - 0x00d609 + len(value)] = span(stock, address, len(value))
    if path_loader != span(stock, 0x00d609, 0x8b):
        raise ValueError('Unsupported Fuzee checkpoint/opponent consumer')
    terrain = native_terrain_properties(target, stock)
    layout = dict(format=['fzero-course-1'], count=['15'])
    evidence = dict(method='Verified Fuzee dictionaries and native data consumers',
                    opponents_policy='Donor native explosive-opponent frequencies by original GP position',
                    unsupported_global_code_executed=False, normalized_resources={})
    normalized = bytearray(target)
    resources = {}
    def add(data, basis, bank_aligned=False):
        offset = (len(normalized) + 15) & ~15
        if bank_aligned or offset % 0x8000 + len(data) > 0x8000:
            offset = (offset + 0x7fff) & ~0x7fff
        address = (offset // 0x8000 << 16) | 0x8000 | (offset & 0x7fff)
        normalized.extend(bytes(offset - len(normalized)))
        normalized.extend(data)
        if len(normalized) > 0x3f0000:
            raise ValueError('Normalized Fuzee resources exceed the LoROM address window')
        evidence['normalized_resources'][f'{address:06x}'] = dict(bytes=len(data),
            sha256=hashlib.sha256(data).hexdigest(), basis=basis)
        return address
    def entry(key, data):
        resources.setdefault(key, []).append(data)
    def resource(key, data, basis):
        entry(key, add(data, basis).to_bytes(3, 'little'))
    terrain_address = add(terrain, 'Verified native tile classifiers')
    sky_address = int.from_bytes(span(target, 0x00a563, 3), 'little')
    pool_cache, sky_cache = {}, {}
    for row in probe['tracks']:
        slot, setting, venue = row['slot'], row['setting'], row['venue']
        variant = row['variant']
        base, blocks, rows = region_resources(target, venue)
        indices = variant_blocks(target, venue, variant, base)
        if venue not in pool_cache:
            pool_cache[venue] = normalized_pool_rows(target, rows)
        pool, rows = pool_cache[venue]
        resource('pools', pool, 'Reindexed author chip definitions; animated pointers retained')
        block_address = add(indices + blocks, 'Exact Fuzee block dictionary and selected replacement indices')
        packed = packed_grid(rows)
        grid_address = add(packed, 'Inverse typed packing of the Fuzee runtime dictionary')
        entry('maps', block_address.to_bytes(3, 'little') + struct.pack('<H', (512 + len(blocks)) // 16)
              + grid_address.to_bytes(3, 'little') + struct.pack('<H', len(packed) // 16))
        common = span(target, 0x0c8000, 208 * 33)
        gfx_address = word(span(target, 0x00a450 + venue * 2, 2)) | 0x0c0000
        resource('graphics', common + span(target, gfx_address, 48 * 33), 'Verified native common/venue road tiles')
        resource('palettes', span(target, 0x0f0000 | word(span(target, 0x00a465 + venue * 2, 2)), 0xe0),
                 'Donor native venue palette')
        entry('gradients', span(target, 0x00a47b + venue, 1))
        resource('sky_graphics', span(target, sky_address, 0x2000), 'Donor native horizon DMA')
        for key, delta, size in (('sky_back', 0, 0x700), ('sky_front', 2, 0x540)):
            pointer = 0x0f8000 + word(span(target, 0x00b19d + venue * 4 + delta, 2))
            if (pointer, size) not in sky_cache:
                sky_cache[pointer, size] = decode_native_horizon(target, pointer, size)
            resource(key, sky_cache[pointer, size], 'Decoded donor native horizon tilemap')
        index = row['donor_music_index']
        pointer = 0x0f8000 + word(span(target, 0x0fc25c + index * 2, 2))
        address = add(minimap(target, pointer), 'Decoded donor native minimap, including tile placement')
        entry('minimaps', (address - 0x8000).to_bytes(3, 'little'))
        entry('map_positions', span(target, 0x0fc27a + index, 1) + b'\0'
              + span(target, 0x0fc289 + index, 1) + b'\0')
        entry('terrain', terrain_address.to_bytes(3, 'little'))
        entry('paths', (0x100000 | word(span(target, 0x108000 + slot * 2, 2))).to_bytes(3, 'little'))
        name, _ = native_name(target, setting)
        resource('names', name, 'Native course intro and verified variation suffix')
        entry('settings', bytes([setting]))
        entry('music', bytes([row['spc_selector_byte']]))
        entry('opponents', bytes(span(target, 0x02fbda + level * 15 + slot, 1)[0] for level in range(3)))
        penalty = span(target, 0x00dac6 + slot, 1)[0]
        if penalty % 2 or penalty > 12 or span(target, word(span(target, 0x00dab8 + penalty, 2)), 1) != b'\x60':
            raise ValueError('Active Fuzee hard-coded shortcut penalties need another adapter')
        resource('shortcuts', b'\0\x80', 'Donor shortcut dispatch resolves to RTS for this course')
    for key, values in resources.items():
        layout[key] = [f'{add(b"".join(values), "Normalized " + key + " table"):06x}']
    # Reproduce the stock palette cycler as a data list for each venue.
    cycles = bytearray(30)
    for row in probe['tracks']:
        offsets = [] if row['venue'] in (1, 5, 8) else [0x60, 0x80, 0xa0]
        if row['venue'] in (0, 4):
            offsets.append(0x90)
        struct.pack_into('<H', cycles, row['slot'] * 2, 0x8000 + len(cycles))
        cycles.extend(struct.pack('<' + 'H' * (len(offsets) + 1), *offsets, 0x8000))
    layout['palette_cycles'] = [f'{add(cycles, "Verified native palette-cycle procedure", bank_aligned=True):06x}']
    # Native glyphs can be imported as artwork, while their consumer stays stock.
    native_font, donor_font = atlas(stock), atlas(target)
    used = {code for row in probe['tracks'] for code in native_name(target, row['setting'])[0][6:].split(b'\0', 1)[0]}
    for code in sorted(used - {0xff}):
        lookup = 0x8e if code == 0xfe else code
        for tile in (lookup, lookup + 16):
            if tile not in donor_font or tile not in native_font:
                raise ValueError('Missing Fuzee native intro tile')
        if tuple(donor_font[t][2] for t in (lookup, lookup + 16)) != tuple(native_font[t][2] for t in (lookup, lookup + 16)):
            if donor_font[lookup][1] != 2 or donor_font[lookup + 16][1] != 2:
                raise ValueError('Unsupported Fuzee changed intro tile compression')
            layout.setdefault('intro_glyph', []).append(f'{lookup:02x}|{donor_font[lookup][0]:06x}|{donor_font[lookup+16][0]:06x}')
    evidence['mechanics'] = ['Native terrain resources; donor vehicle/collision/game-wide code is excluded']
    evidence['practice_policy'] = 'Seven verified aliases share the fifteen imported GP courses and records'
    return bytes(normalized), layout, evidence
