"""Convert IPS/BPS or hacked ROM resources to an editable course pack.

Reviewed revisions retain their established exports. Recognized FZEdit loaders
are validated without a digest whitelist, then request labels/cup confirmation.
Unsupported resource layouts return a ROM-free report and exit code 2.
Use package_fzedit_course.py when original FZEdit source is available.
"""
import argparse
from collections import Counter
import hashlib
import io
import json
import lzma
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tempfile
import zipfile
import zlib

from audit_track_metadata import audit_metadata, GLYPHS, SONGS, span
from audit_intro_font import atlas
from course_tool_paths import ROOT, native_tool
from export_runtime_packs import export_pack
from fuzee_course_format import decode as decode_fuzee, recognized as recognized_fuzee
from native_course_format import native_terrain_properties, decode_native_horizon
from fuzee_course_import import probe_metadata as probe_fuzee_metadata, infer_layout as infer_fuzee_layout
from import_astra_front import SOURCE_SHA256 as ASTRA_SOURCE_SHA256
from inspect_bs_deluxe import STOCK_SHA256
from parse_track_pack import donor, fields, recognize
from reconstruct_fzedit_course import ALPHABET

IMAGE_LIMIT = 16 * 1024 * 1024
INPUT_LIMIT = 32 * 1024 * 1024
ZIP_ARCHIVE_LIMIT = 2 * 1024 * 1024 * 1024
ZIP_TOTAL_LIMIT = 2 * 1024 * 1024 * 1024
ZIP_FILE_LIMIT = 256 * 1024 * 1024
ZIP_MEMBER_LIMIT = 4096
ZIP_DIRECTORY_LIMIT = 8 * 1024 * 1024
ROM_SUFFIXES = ('.sfc', '.smc', '.rom', '.fig')
PATCH_SUFFIXES = ('.ips', '.bps')
AUDIO_SUFFIXES = ('.pcm', '.msu', '.wav', '.flac', '.mp3', '.ogg')
KNOWN_IDS = ('astra-front', 'bower-league', 'cgp', 'max-league')
ID_PATTERN = re.compile(r'[a-z0-9][a-z0-9-]{0,46}\Z')

REVIEW = """The course resources could not be decoded and validated. No pack was created.

1. Keep the original submission and author's credits. Confirm its required
   stock revision and record the input/target SHA-256 from conversion-report.json.
2. Ask for the original FZEdit project first. It avoids reverse engineering.
3. Otherwise review the donor's course loader and ASM in the headless Ghidra
   registry. Identify bounded data tables and their consumers; file differences
   alone do not identify courses or prove that mechanics are supported.
4. Review authored cup labels, course names, race order, SPC song selection,
   intro glyphs, title layout, and course-specific behavior. Reuse only reviewed
   grip-magnets/up-magnets/rainbow-road capabilities. New behavior needs a native
   adapter and tests. No donor executable code is installed by this converter.
5. Follow mods/PARSE_MANIFEST.md to qualify a typed layout and exact revision.
   Add a supported resource decoder and tests, or a reviewed registry profile
   when a special adapter is needed. Recognized FZEdit layouts can instead use
   the in-game review form without a pre-registered digest. Rerun into a new
   output directory. Arbitrary executable behavior is never imported.
6. Test GP, Practice, every cup transition, native SPC fallback, stock/other-pack
   isolation and records/save reload. Structural extraction is not a full race.

See CONVERSION.md and mods/PARSE_MANIFEST.md. Keep patched ROMs private.
"""


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_bounded(path, limit):
    with Path(path).open('rb') as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f'Input exceeds {limit:,} bytes: {path}')
    return data


def strip_header(data):
    return (data[512:], 512) if len(data) % 1024 == 512 else (data, 0)


def read_stock(path):
    data, _ = strip_header(read_bounded(path, IMAGE_LIMIT + 512))
    if digest(data) != STOCK_SHA256:
        raise ValueError('Expected the original F-Zero USA stock ROM')
    return data


def hash_file(path, limit):
    result, count = hashlib.sha256(), 0
    with Path(path).open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            count += len(chunk)
            if count > limit:
                raise ValueError(f'Input exceeds {limit:,} bytes: {path}')
            result.update(chunk)
    return result.hexdigest(), count


def bounded_zip(source):
    """Bound directory allocation before ZipFile loads its member objects."""
    with Path(source).open('rb') as stream:
        stream.seek(0, 2)
        if stream.tell() > ZIP_ARCHIVE_LIMIT:
            raise ValueError('Compressed ZIP exceeds the 2 GiB input limit')
        # The same stdlib reader used by ZipFile scans at most the 64 KiB tail
        # plus fixed ZIP64 records. Its directory size is checked before open.
        end = zipfile._EndRecData(stream)
        if end is None:
            raise zipfile.BadZipFile('ZIP end-of-directory record is missing')
        if end[zipfile._ECD_SIZE] > ZIP_DIRECTORY_LIMIT:
            raise ValueError('ZIP central directory exceeds the 8 MiB metadata limit')
        if end[zipfile._ECD_ENTRIES_TOTAL] > ZIP_MEMBER_LIMIT:
            raise ValueError('ZIP contains too many members')
    return zipfile.ZipFile(source)


def zip_inventory(archive):
    """Validate metadata without inflating recordings or incidental tools."""
    entries = archive.infolist()
    if len(entries) > ZIP_MEMBER_LIMIT:
        raise ValueError('ZIP contains too many members')
    total, names, audio = 0, {}, []
    for entry in entries:
        raw = entry.orig_filename
        path = PurePosixPath(raw)
        parts = raw.rstrip('/').split('/')
        if (not raw or len(raw.encode('utf-8')) > 240 or '\x00' in raw or path.is_absolute() or '\\' in raw
                or ':' in raw or any(part in ('', '.', '..') for part in parts)
                or any(part.endswith((' ', '.')) for part in parts)
                or stat.S_ISLNK(entry.external_attr >> 16) or entry.external_attr & 0x400):
            raise ValueError('ZIP contains an unsafe member path or link')
        key = raw.rstrip('/').casefold()
        if key in names:
            raise ValueError('ZIP contains duplicate member names')
        names[key] = entry.is_dir()
        total += entry.file_size
        if entry.file_size > ZIP_FILE_LIMIT or total > ZIP_TOTAL_LIMIT:
            raise ValueError('ZIP expanded data exceeds the input limit')
        if entry.flag_bits & 1:
            raise ValueError('Encrypted ZIP members are unsupported')
        extension = path.suffix.lower()
        if not entry.is_dir() and extension in AUDIO_SUFFIXES:
            item = dict(member=entry.filename, bytes=entry.file_size,
                        format=extension[1:], zip_crc32=f'{entry.CRC:08x}', mapping='not-installed')
            if extension == '.pcm':
                header = b''
                try:
                    with archive.open(entry) as stream:
                        header = stream.read(8)
                except (zipfile.BadZipFile, OSError, RuntimeError, NotImplementedError,
                        EOFError, lzma.LZMAError, zlib.error) as error:
                    item['header_error'] = str(error)
                frames = (entry.file_size-8)//4 if entry.file_size >= 8 else 0
                loop = int.from_bytes(header[4:8], 'little')
                item.update(header_valid=(len(header) == 8 and header[:4] == b'MSU1'
                            and entry.file_size >= 8 and (entry.file_size-8) % 4 == 0
                            and (loop < frames or loop == 0)), frames=frames, loop_frame=loop)
            audio.append(item)
    for key in names:
        for parent in PurePosixPath(key).parents:
            if str(parent) in names and not names[str(parent)]:
                raise ValueError('ZIP contains a file/directory path collision')
    return entries, dict(members=len(entries), expanded_bytes=total), dict(
        members=audio, pcm_count=sum(item['format'] == 'pcm' for item in audio),
        msu_count=sum(item['format'] == 'msu' for item in audio),
        bytes=sum(item['bytes'] for item in audio), mapped_pcm_count=0,
        validation='PCM headers checked; full ZIP CRC checked only when a recording is copied',
        original_archive_preserved=True)


def read_zip_member(archive, entry, limit):
    if entry.file_size > limit:
        raise ValueError(f'ZIP donor exceeds {limit:,} bytes: {entry.filename}')
    with archive.open(entry) as stream:
        data = stream.read(limit + 1)
    if len(data) > limit or len(data) != entry.file_size:
        raise ValueError('ZIP donor exceeds the input limit')
    return data


def zip_patches(data, member=None):
    """Compatibility helper for extracted patch callers; disk ZIPs stream below."""
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries, _, _ = zip_inventory(archive)
        patches = [entry for entry in entries if not entry.is_dir()
                   and PurePosixPath(entry.filename).suffix.lower() in PATCH_SUFFIXES]
        if member is not None:
            patches = [entry for entry in patches if entry.filename == member]
        if not patches or (member is not None and len(patches) != 1) or len(patches) > 8:
            raise ValueError('Select exactly one IPS/BPS with --patch-member; '
                             'original FZEdit ZIPs use package_fzedit_course.py')
        return [(read_zip_member(archive, entry, INPUT_LIMIT), entry.filename) for entry in patches]


def select_patch(candidates, stock):
    if len(candidates) == 1:
        return candidates[0], []
    if stock is None:
        raise ValueError('Automatic ZIP patch selection requires --stock')
    valid = []
    for patch, name in candidates:
        try:
            target = donor(stock, patch)
            profile = reviewed_profile(target)
            valid.append(dict(member=name, target_sha256=digest(target),
                              profile=fields(profile)['id'][0] if profile else None))
        except ValueError:
            continue
    reviewed = [entry for entry in valid if entry['profile']]
    choices = reviewed or valid
    identities = {entry['profile'] if reviewed else entry['target_sha256'] for entry in choices}
    if not choices or len(identities) != 1:
        raise ValueError('ZIP has distinct or unusable donor revisions; select --patch-member')
    selected = min(choices, key=lambda entry: (not entry['member'].lower().endswith('.bps'), entry['member']))
    return next(candidate for candidate in candidates if candidate[1] == selected['member']), valid


def decode_donor(data, extension, stock):
    if data.startswith((b'PATCH', b'BPS1')):
        if stock is None:
            raise ValueError('IPS/BPS conversion requires --stock with the original USA ROM')
        target = donor(stock, data)
        report = dict(input_kind='bps' if data.startswith(b'BPS1') else 'ips',
                      patch_sha256=digest(data), bps_crcs_verified=data.startswith(b'BPS1'))
    elif extension in ROM_SUFFIXES:
        target, header = strip_header(data)
        report = dict(input_kind='rom', removed_copier_header_bytes=header)
    else:
        raise ValueError('Expected IPS, BPS, a patch ZIP, or a hacked .sfc/.smc ROM')
    if not 0x8000 <= len(target) <= IMAGE_LIMIT:
        raise ValueError('Donor image must be between 32 KiB and 16 MiB')
    report.update(target_sha256=digest(target), target_bytes=len(target))
    return target, report


def read_zip_submission(source, stock, member):
    report = dict(input_name=source.name, warnings=[])
    with bounded_zip(source) as archive:
        entries, report['archive_inventory'], report['audio_inventory'] = zip_inventory(archive)
        candidates = [entry for entry in entries if not entry.is_dir()
                      and PurePosixPath(entry.filename).suffix.lower() in PATCH_SUFFIXES + ROM_SUFFIXES]
        report['ignored_members'] = [entry.filename for entry in entries if not entry.is_dir()
            and PurePosixPath(entry.filename).suffix.lower() not in PATCH_SUFFIXES + ROM_SUFFIXES + AUDIO_SUFFIXES]
        if member is not None:
            candidates = [entry for entry in candidates if entry.filename == member]
            if len(candidates) != 1:
                raise ValueError('--member must identify one exact IPS/BPS/ROM member')
        valid, errors = [], []
        if len(candidates) <= 8:
            for entry in candidates:
                extension = PurePosixPath(entry.filename).suffix.lower()
                limit = IMAGE_LIMIT + 512 if extension in ROM_SUFFIXES else INPUT_LIMIT
                try:
                    target, metadata = decode_donor(read_zip_member(archive, entry, limit), extension, stock)
                    profile = reviewed_profile(target)
                    valid.append(dict(member=entry.filename, profile=fields(profile)['id'][0] if profile else None,
                                      **metadata))
                except ValueError as error:
                    errors.append(dict(member=entry.filename, reason=str(error)))
        report['archive_donor_candidates'] = valid
        report['archive_candidate_errors'] = errors
        # IPS has no source-platform checksum. Use an explicit patching
        # statement in the author's README only as a rejection hint, never as
        # permission to import data or choose one of multiple SNES targets.
        other_game = None
        readmes = [entry for entry in entries if not entry.is_dir()
                   and PurePosixPath(entry.filename).suffix.lower() in ('.txt', '.md')
                   and entry.file_size <= 65536]
        for entry in readmes[:4]:
            notes = read_zip_member(archive, entry, 65536).decode('utf-8', errors='replace')
            if re.search(r'apply\s+the\s+IPS\s+patch\s+to\s+an\s+unmodified\s+copy\s+of\s+'
                         r'(?:the\s+)?(?:US/EU\s+)?Maximum\s+Velocity\s+rom', notes, re.IGNORECASE):
                other_game = dict(platform='Game Boy Advance', game='F-Zero: Maximum Velocity',
                                  evidence_member=entry.filename)
        if other_game:
            report['declared_patch_target'] = other_game
        if not candidates:
            report['reason'] = ('No IPS/BPS or ROM donor was found. Ready packs and FZEdit projects '
                                'use the native importer; audio-only archives need an existing course pack.')
            selected = None
        elif len(candidates) > 8:
            report['reason'] = 'The archive has more than eight donor candidates; select an exact --member.'
            selected = None
        elif not valid:
            report['reason'] = 'No donor member could be applied or validated against the selected stock ROM.'
            selected = None
        elif len({entry['profile'] or entry['target_sha256'] for entry in valid}) != 1:
            report['reason'] = 'The archive contains different donor revisions; select an exact --member before conversion.'
            selected = None
        else:
            selected = min(valid, key=lambda item: (item['input_kind'] != 'bps', item['input_kind'] == 'rom', item['member']))
        if other_game and not any(entry['profile'] for entry in valid):
            report['reason'] = ('The author\'s README says this patch is for F-Zero: Maximum Velocity '
                                'on Game Boy Advance. This importer supports SNES F-Zero course data; '
                                'GBA courses cannot be installed here. No courses or recordings were installed.')
            selected = None
        if selected is not None:
            entry = archive.getinfo(selected['member'])
            extension = PurePosixPath(entry.filename).suffix.lower()
            limit = IMAGE_LIMIT + 512 if extension in ROM_SUFFIXES else INPUT_LIMIT
            target, metadata = decode_donor(read_zip_member(archive, entry, limit), extension, stock)
            report.update(metadata, donor_member=entry.filename)
            if extension in PATCH_SUFFIXES:
                report['patch_member'] = entry.filename
            if len(valid) > 1:
                report['archive_patch_targets'] = valid  # Existing report consumers.
            if errors:
                report['warnings'].append('Some donor members could not be validated; their reasons are listed in the report.')
        else:
            target = None
    report['input_sha256'], report['input_bytes'] = hash_file(source, ZIP_ARCHIVE_LIMIT)
    return target, report


def read_submission(source, stock=None, patch_member=None):
    source = Path(source)
    with source.open('rb') as stream:
        signature = stream.read(4)
    if source.suffix.lower() == '.zip' or signature == b'PK\x03\x04':
        target, report = read_zip_submission(source, stock, patch_member)
    else:
        if patch_member is not None:
            raise ValueError('--patch-member requires a ZIP submission')
        raw = read_bounded(source, INPUT_LIMIT)
        target, report = decode_donor(raw, source.suffix.lower(), stock)
        report.update(input_name=source.name, input_sha256=digest(raw), input_bytes=len(raw))
    if stock is not None:
        report['stock_sha256'] = digest(stock)
    return target, report


def reviewed_profile(target):
    known = recognize(target)
    if known is None and digest(target) == ASTRA_SOURCE_SHA256:
        known = ROOT/'assets/track-packs/astra-front.ini'
    if known is not None and fields(known)['id'][0] in KNOWN_IDS:
        return known
    return None


def probe_cup_menu(target):
    """Read the recognized 13-tile native menu table, without inventing glyphs."""
    loader = span(target,0x03897c,32)
    prefix=bytes.fromhex('c2 20 a9 70 05 8d 20 04 bf')
    if loader[:len(prefix)] != prefix or loader[12:21] != bytes.fromhex('8d 22 04 a2 10 8e 24 04 a9'):
        return None
    table=int.from_bytes(loader[9:12],'little')
    first=(table&0xff0000)|int.from_bytes(span(target,table,2),'little')
    distance=first-table
    if distance%2 or not 1 <= distance//2 <= 32:
        return None
    labels=[]; alphabet={0xaf:'A',0xd4:'S',0xd5:'T',0xd3:'R',0xc7:'F',0xd0:'O',0xce:'N',0xff:' '}
    for i in range(distance//2):
        pointer=(table&0xff0000)|int.from_bytes(span(target,table+i*2,2),'little')
        if pointer != first+i*26:
            return None
        encoded=span(target,pointer,26)
        row=dict(tile_words=[f'{int.from_bytes(encoded[j:j+2],"little"):04x}' for j in range(0,26,2)])
        if all(code in alphabet for code in encoded[::2]):
            row['name']=''.join(alphabet[code] for code in encoded[::2]).strip()
        labels.append(row)
    return dict(pointer_table=f'{table:06x}',cup_count=len(labels),labels=labels,
                evidence='Recognized 13-word transfer; adjacent pointer table and fixed-width label records')


def probe_msu_selector(target, races):
    """Recognize the selector that feeds MSU track registers, without running ASM."""
    events = {'countdown': 1, 'ready': 2, 'lost-life': 3, 'title': 4, 'select': 5, 'ending': 7}
    # Existing FZEdit arithmetic selector. Only the immediate base may vary.
    standard = bytes.fromhex('a5 46 29 07 c9 06 d0 11 a5 53 a6 58 d0 08 '
                             'a5 90 0a 0a 65 90 65 53 18 69 0a 60')
    try:
        code = span(target, 0x02c26a, len(standard))
        if code[:24] == standard[:24] and code[25:] == standard[25:]:
            tracks = list(range(code[24], code[24]+races))
            if all(0 < track < 256 for track in tracks):
                return dict(recognized=True, kind='arithmetic', base_track=code[24],
                            formula=f'{code[24]} + 5 * cup + race', tracks=tracks, events=events)
        # Table selector: Practice uses race; GP uses the stock 5*cup offset.
        # Require both loads to use the same table and a caller that writes the
        # returned byte to $2004, then clears the high track byte at $2005.
        pattern = (re.escape(bytes.fromhex('a5 46 29 0f c9 06 f0 01 60 a5 58 f0 08 a5 53 aa bf'))
                   + b'(.{3})' + re.escape(bytes.fromhex('60 a5 f2 18 65 53 aa bf'))
                   + b'(.{3})' + b'\x60')
        bank = span(target, 0x028000, 0x8000)
        matches = []
        for match in re.finditer(pattern, bank, re.DOTALL):
            selector = 0x8000+match.start()
            caller = b'\x20'+selector.to_bytes(2, 'little')+bytes.fromhex('8d 04 20 9c 05 20')
            if match[1] != match[2] or caller not in bank:
                continue
            if span(target, 0x008bf3, 8) != bytes.fromhex('a5 90 0a 0a 65 90 85 f2'):
                continue
            table = int.from_bytes(match[1], 'little')
            tracks = list(span(target, table, races))
            if all(0 < track < 255 for track in tracks):
                matches.append(dict(recognized=True, kind='table', table=f'{table:06x}',
                                    selector=f'02{selector:04x}', tracks=tracks, events=events))
        if len(matches) == 1:
            return matches[0]
    except ValueError:
        pass
    return dict(recognized=False)


def find_consumer(target, prefix, middle, tail=None, delta=0):
    """One bounded FZEdit consumer; relocated code is not a new data format."""
    expression = re.escape(bytes.fromhex(prefix))+b'(.{3})'+re.escape(bytes.fromhex(middle))
    if tail is not None:
        expression += b'(.{3})'+re.escape(bytes.fromhex(tail))
    matches = []
    for match in re.finditer(expression, span(target,0x108000,0x8000),re.DOTALL):
        address = int.from_bytes(match[1],'little')
        if tail is not None and int.from_bytes(match[2],'little') != address+delta:
            continue
        span(target,address,1)
        matches.append((address,0x108000+match.start(),0x108000+match.end()))
    if len(matches) != 1:
        raise ValueError(f'Expected one FZEdit metadata consumer, found {len(matches)}')
    return matches[0]


def probe_older_fzedit(target):
    """Older FZEdit loaders retain venue SPC selection and relocate routines."""
    order, consumer, _ = find_consumer(target,'8a c2 30 29 ff 00 8d 5b 10 bb bf',
                                      '29 ff 00 8d 59 10 20')
    entry = consumer-32
    if (span(target,0x009f08,4) != b'\x5c'+entry.to_bytes(3,'little') or
            span(target,entry,32) != bytes.fromhex('08 e2 30 a5 58 d0 0a a5 90 0a 0a 65 90 65 53 '
                '80 02 a5 53 a8 a2 00 c9 05 30 06 38 e9 05 e8 80 f6')):
        raise ValueError('Unrecognized FZEdit order entry point')
    settings, settings_consumer, _ = find_consumer(target,'ae 59 10 bf','5c 1f 9f 00')
    if span(target,0x009f1b,4) != b'\x5c'+settings_consumer.to_bytes(3,'little'):
        raise ValueError('Unrecognized FZEdit settings hook')
    names, _, _ = find_consumer(target,'da c2 20 ad 59 10 0a 6d 59 10 aa bf',
                              '85 00 e2 20 bf','85 02 fa 5c dd ab 00',2)
    try:
        mini, _, mini_end = find_consumer(target,'ad 59 10 0a 6d 59 10 aa 8b bf',
            'a8 e2 20 bf','48 a9 80 8d 15 21',2)
    except ValueError:
        mini, _, mini_end = find_consumer(target,'ad 59 10 0a 6d 59 10 aa bf',
            'a8 8b e2 20 bf','48 ab a9 80 8d 15 21',2)
    try:
        positions, _, _ = find_consumer(target,'ad 59 10 0a 0a aa bf','8d d9 0a bf','8d db 0a',2)
        distance=positions-mini
        count_evidence='Adjacent minimap and position tables, three-byte minimap stride'
        constants=None
    except ValueError:
        # Earliest loader embeds all minimap pointers immediately after RTL,
        # followed by the gradient routine. These boundaries establish count.
        position_pattern=(b'\xa9(.{2})'+re.escape(bytes.fromhex('8d d9 0a a9'))+b'(.{2})'+
                          re.escape(bytes.fromhex('8d db 0a ad 59 10 0a 6d 59 10 aa bf'))+
                          mini.to_bytes(3,'little'))
        matches=list(re.finditer(position_pattern,span(target,0x108000,0x8000),re.DOTALL))
        if len(matches)!=1 or span(target,mini_end,15) != bytes.fromhex('a2 00 5e 8e 16 21 a2 1f 00 22 77 9e 03 ab 6b'):
            raise ValueError('Unrecognized inline minimap table')
        _, gradient_consumer, _=find_consumer(target,'ae 59 10 bf','85 9c 5c 22 a1 00')
        if mini != mini_end+15:
            raise ValueError('Inline minimap table is not adjacent to its transfer')
        distance=gradient_consumer-mini
        positions=None
        constants=[int.from_bytes(matches[0][i],'little') for i in (1,2)]
        count_evidence='Inline minimap pointer table bounded by transfer RTL and gradient consumer'
    if distance%3 or not 1 <= distance//3 <= 128:
        raise ValueError('Invalid FZEdit metadata table boundaries')
    count=distance//3
    span(target,mini,count*3)
    source_order=list(span(target,order,count))
    if len(set(source_order))!=count or any(slot>=count for slot in source_order):
        raise ValueError('Invalid FZEdit published order')
    selector=span(target,0x00f7e0,17)
    if (selector[:7]!=bytes.fromhex('08 e2 30 ae d8 0a bf') or
            selector[10:14]!=bytes.fromhex('0a 0a 0a 7f') or selector[7:10]!=selector[14:17] or
            bytes.fromhex('e2 30 ad f5 0c c9 03 90 0f c9 06 f0 0b ae ff 0c f0 06 '
                          '18 6d ff 0c 69 04 8d d8 0a 28 60') not in target[:0x8000]):
        raise ValueError('Unsupported venue music selector')
    music=int.from_bytes(selector[7:10],'little')
    songs=span(target,music,15)
    tracks=[]
    alphabet=dict(zip(ALPHABET,'ABCDEFGHIJKLMNOPQRSTUVWXYZ'));alphabet.update(GLYPHS)
    alphabet.update({0x80+i:str(i) for i in range(10)})
    # FZEdit's alphabet_8x16.txt also names these native punctuation glyphs.
    alphabet.update({0x28:"'",0x38:'"',0x1b:'.',0x2a:'(',0x3a:')',0x2b:'^',0xa9:'?'})
    for position,slot in enumerate(source_order):
        setting=span(target,settings+slot,1)[0]
        venue,variant=setting&15,(setting>>4)-12
        if venue>=9 or not 0<=variant<=2:
            raise ValueError('Unsupported venue/variant settings')
        music_index=venue
        if venue>=3 and venue!=6 and variant:
            music_index+=variant+4
        song=songs[music_index]
        if song>=len(SONGS):
            raise ValueError('Unsupported venue SPC song')
        pointer=int.from_bytes(span(target,names+slot*3,3),'little')
        encoded=span(target,pointer,128).split(b'\0',1)[0]
        row=dict(slot=slot,order_table_position=position,spc_selector_byte=song*9,
                 spc_index=song,stock_spc_theme=SONGS[song],donor_music_index=music_index)
        if len(encoded)<6 or encoded[1:6]!=bytes.fromhex('53 01 82 1b ff'):
            raise ValueError('Unsupported FZEdit intro name encoding')
        try: row['name']=''.join(alphabet[code] for code in encoded[6:]).strip()
        except KeyError as error: row['name_error']=f'Unreviewed intro glyph {error.args[0]:02x}'
        tracks.append(row)
    menu=probe_cup_menu(target)
    races=menu['cup_count']*5 if menu else count
    if races!=count:
        raise ValueError('Published cup count differs from resource order')
    msu=probe_msu_selector(target,races)
    if msu['recognized']:
        for row,number in zip(tracks,msu['tracks']):row['msu_track']=number
    result=dict(status='recognized-resource-metadata',loader_family='fzedit-older',
        executable_compatibility_verified=False,cup_labels_verified=False,intro_font_verified=False,
        music_table=f'{music:06x}',music_selector_kind='venue',settings_table=f'{settings:06x}',
        order_table=f'{order:06x}',name_table=f'{names:06x}',minimap_table=f'{mini:06x}',
        position_table=f'{positions:06x}' if positions else None,minimap_constants=constants,
        internal_resource_count=count,published_course_count=races,count_evidence=count_evidence,
        source_order_prefix=source_order,tracks=tracks,msu_selector=msu)
    if menu:result['donor_cup_menu']=menu
    return result


def probe_fzedit_metadata(target):
    """Decode recognized data consumers; this does not qualify other donor ASM."""
    result = dict(status='unrecognized-loader', executable_compatibility_verified=False,
                  cup_labels_verified=False, intro_font_verified=False)
    try:
        classic = bytes.fromhex('08 e2 30 a9 0f a6 58 d0 06 a5 90 0a 0a 65 90 '
                                '18 65 53 aa bf 29 e1 02 8d de 0a 29 0f 8d f5 0c a8')
        if (span(target, 0x009f08, len(classic)) == classic
                and (recognized_fuzee(target) or span(target, 0x00f7e0, 17) == bytes.fromhex(
                    '08 e2 30 ae d8 0a bf 71 9e 03 0a 0a 0a 7f 71 9e 03'))):
            if recognized_fuzee(target):
                result.update(status='recognized-incomplete-resource-layout', loader_family='fuzee-0.04',
                    unsupported_reason='This hack uses the Fuzee course format. Its road and checkpoint '
                        'layout is identified, but complete course conversion is not supported yet. '
                        'See CONVERSION.md for the current decoder limits.')
                try:
                    audit, maps = decode_fuzee(target)
                    result['decoded_resource_inventory'] = dict(gp_entries=15, practice_entries=7,
                        unique_road_variants=len(maps),
                        checkpoint_sections=sum(len(course['checkpoints']) for course in audit['courses']),
                        decoded_fields=audit['decoded_fields'], remaining_fields=audit['remaining_fields'])
                    metadata = probe_fuzee_metadata(target)
                    result.update(metadata)
                    result.pop('unsupported_reason', None)
                except ValueError as error:
                    result['structural_error'] = str(error)
                return result
            result.update(loader_family='legacy-stock',
                unsupported_reason='This hack uses the older F-Zero course format, which the importer cannot '
                    'decode yet. Course labels alone cannot resolve it. Original FZEdit projects can be '
                    'imported if the author has them; otherwise this format needs a new resource decoder.')
            return result
        selector = span(target, 0x00f7e0, 17)
        order = span(target, 0x10824d, 21)
        if (selector[:9] != bytes.fromhex('08 e2 30 c2 10 ae 59 10 bf')
                or selector[12:] != bytes.fromhex('e2 10 ea ea ea')
                or order[:11] != bytes.fromhex('8a c2 30 29 ff 00 8d 5b 10 bb bf')
                or order[14:] != bytes.fromhex('29 ff 00 8d 59 10 20')):
            try:
                return probe_older_fzedit(target)
            except ValueError as error:
                result['alternate_loader_error']=str(error)
            return result
        music_table = int.from_bytes(selector[9:12], 'little')
        order_table = int.from_bytes(order[11:14], 'little')
        result.update(status='recognized-metadata-locators', music_table=f'{music_table:06x}',
                      order_table=f'{order_table:06x}')
        positions = span(target, 0x10845b, 20)
        minimaps = span(target, 0x10846f, 21)
        names = span(target, 0x10839b, 26)
        if (positions[:7] != bytes.fromhex('ad 59 10 0a 0a aa bf')
                or positions[10:14] != bytes.fromhex('8d d9 0a bf')
                or positions[17:] != bytes.fromhex('8d db 0a')
                or minimaps[:10] != bytes.fromhex('ad 59 10 0a 6d 59 10 aa 8b bf')
                or minimaps[13:17] != bytes.fromhex('a8 e2 20 bf') or minimaps[-1] != 0x48
                or names[:12] != bytes.fromhex('da c2 30 ad 59 10 0a 6d 59 10 aa bf')
                or names[15:18] != bytes.fromhex('85 00 bf')
                or names[21:] != bytes.fromhex('85 01 e2 30 fa')):
            return result
        position_table = int.from_bytes(positions[7:10], 'little')
        minimap_table = int.from_bytes(minimaps[10:13], 'little')
        name_table = int.from_bytes(names[12:15], 'little')
        if (int.from_bytes(positions[14:17], 'little') != position_table+2
                or int.from_bytes(minimaps[17:20], 'little') != minimap_table+2
                or int.from_bytes(names[18:21], 'little') != name_table+1):
            return result
        distance = position_table-minimap_table
        if (position_table >> 16 != minimap_table >> 16 or distance % 3
                or not 1 <= distance//3 <= 128):
            return result
        count = distance//3
        span(target, minimap_table, count*3); span(target, position_table, count*4)
        menu=probe_cup_menu(target)
        races=menu['cup_count']*5 if menu else count
        if menu:
            result['donor_cup_menu']=menu
        if not 1 <= races <= 128:
            return result
        source_order = list(span(target, order_table, races))
        if len(set(source_order)) != races or any(slot >= count for slot in source_order):
            result['order_note'] = 'Internal resource count does not establish the published GP subset.'
            return result
        music = span(target, music_table, count)
        result['msu_selector'] = msu = probe_msu_selector(target, races)
        alphabet = dict(zip(ALPHABET, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'))
        alphabet.update(GLYPHS)
        alphabet.update({0x80+i: str(i) for i in range(10)})
        tracks = []
        for position, slot in enumerate(source_order):
            pointer = int.from_bytes(span(target, name_table+slot*3, 3), 'little')
            encoded = span(target, pointer, 128).split(b'\0', 1)[0]
            row = dict(slot=slot, order_table_position=position, spc_selector_byte=music[slot])
            if len(encoded) < 6 or encoded[1:6] != bytes.fromhex('53 01 82 1b ff'):
                row['name_error'] = 'Unrecognized intro-name encoding'
            else:
                try:
                    row['name'] = ''.join(alphabet[code] for code in encoded[6:]).strip()
                except KeyError as error:
                    row['name_error'] = f'Unreviewed intro glyph {error.args[0]:02x}'
            if music[slot] <= 81 and music[slot] % 9 == 0:
                row.update(spc_index=music[slot]//9, stock_spc_theme=SONGS[music[slot]//9])
            if msu['recognized']:
                row['msu_track'] = msu['tracks'][position]
            tracks.append(row)
        result.update(status='recognized-resource-metadata', internal_resource_count=count,
            published_course_count=races,
            count_evidence='Recognized adjacent minimap/position tables, three-byte minimap stride',
            name_table=f'{name_table:06x}', source_order_prefix=source_order, tracks=tracks,
            limitations=['Published cup labels/count and font artwork need review.',
                         'SPC selector indices do not prove unchanged SPC sound data.',
                         'Course resources and other ASM/mechanics have not been qualified.'])
    except ValueError as error:
        result['probe_error'] = str(error)
    return result


def infer_fzedit_layout(target, stock, probe):
    """Recognize bounded data consumers; no donor instructions are executed."""
    if probe.get('status') != 'recognized-resource-metadata':
        raise ValueError('The course order/resource count could not be identified safely')
    if probe.get('loader_family') == 'fuzee-0.04':
        return infer_fuzee_layout(target, stock, probe)
    count = probe['internal_resource_count']
    layout = dict(format=['fzero-course-1'], count=[str(count)],
                  names=[probe['name_table']], music=[probe['music_table']])
    evidence = dict(method='recognized FZEdit data consumers and native bounded extraction', tables={})
    normalized = target
    def synthesize(key, data, basis):
        nonlocal normalized
        offset=(len(normalized)+0x7fff)&~0x7fff
        address=((offset//0x8000)<<16)|0x8000
        normalized+=bytes(offset-len(normalized))+data
        span(normalized,address,len(data))
        layout[key]=[f'{address:06x}']
        evidence.setdefault('normalized_resources',{})[key]=dict(address=f'{address:06x}',
            bytes=len(data),sha256=digest(data),basis=basis)
        return address
    regions = ((0, target[:0x8000]), (0x80000, target[0x80000:0x88000]))
    def pointer(key, prefix, middle, tail=None, delta=0, width=3, optional=False):
        expression = re.escape(bytes.fromhex(prefix))+b'(.{'+str(width).encode()+b'})'+re.escape(bytes.fromhex(middle))
        if tail is not None:
            expression += b'(.{'+str(width).encode()+b'})'+re.escape(bytes.fromhex(tail))
        matches = []
        for base, region in regions:
            for match in re.finditer(expression, region, re.DOTALL):
                address = int.from_bytes(match[1], 'little')
                if tail is not None and int.from_bytes(match[2], 'little') != address+delta:
                    continue
                if width == 2:
                    address |= 0x100000
                try:
                    span(target, address, 1)
                except ValueError:
                    continue
                matches.append((address, base+match.start()))
        addresses = {entry[0] for entry in matches}
        if len(addresses) != 1:
            if optional and not addresses:
                return None
            raise ValueError(f'{key}: expected one recognized table consumer, found {len(addresses)}')
        address = matches[0][0]
        layout[key] = [f'{address:06x}']
        evidence['tables'][key] = dict(address=f'{address:06x}',
            consumer_file_offsets=sorted({entry[1] for entry in matches}))
        return address
    pointer('settings', 'ae 59 10 bf', '5c 1f 9f 00')
    pointer('palettes', 'ad 59 10 0a 6d 59 10 aa bf', '85 00 bf', '85 01 a0 de 00', 1)
    pointer('pools', 'ad 59 10 0a 6d 59 10 aa a9 00 80 8d 00 43 a9 00 24 8d 05 43 bf',
            '8d 02 43 e2 20 bf', '8d 04 43', 2)
    if pointer('graphics', 'ad 59 10 0a 6d 59 10 aa bf', '85 04 e2 20 bf',
               '85 06 a9 00 22 9b 82 10', 2,optional=True) is None:
        pointer('graphics', 'ad 59 10 0a 6d 59 10 aa bf', '85 04 e2 20 bf', '85 06 a9 00 5c df a0 00', 2)
    flags=pointer('_graphics_flags','c2 30 ad 5b 10 0a aa bf',
        'd0 0f a9 00 80 85 04 e2 20 a9 0c 85 06 5c c5 a0 00',optional=True)
    if flags is not None:
        layout.pop('_graphics_flags')
        if any(not int.from_bytes(span(target,flags+row['order_table_position']//5*2,2),'little')
               for row in probe['tracks']):
            raise ValueError('Mixed native/custom road graphics need another decoder')
        evidence['graphics_policy']='Every published cup selects the decoded custom graphics table'
    if pointer('paths', 'ad 59 10 0a 6d 59 10 aa bf', '85 30 bf',
               '85 31 64 33 5c 4e d6 00', 1,optional=True) is None:
        address=pointer('paths','ad 59 10 0a 6d 59 10 aa bf','5c 4a d6 00')
        bank_address=pointer('path_bank','ad 59 10 0a 6d 59 10 aa bf',
                             'e2 30 48 ab a9 00 48 5c 0f d6 00')
        if bank_address!=address+2:
            raise ValueError('Checkpoint bank and word consumers disagree')
        layout.pop('path_bank')
    sky=pointer('sky_graphics', 'ad 59 10 0a 6d 59 10 aa a9 01 18 8d 00 43 bf',
                '8d 02 43 bf', '8d 03 43 a9 00 20 8d 05 43', 1,optional=True)
    if sky is not None:
        pointer('sky_back', 'a9 01 18 8d 00 43 bf', '85 00 8d 02 43 bf', 'e2 10 aa 8e 04 43 a9 00 07', 2)
        pointer('sky_front', 'a9 60 71 8d 16 21 bf', '85 00 8d 02 43 bf', 'e2 10 aa 8e 04 43 a9 40 05', 2)
    else:
        # Early FZEdit projects keep the native sky loader. Verify every
        # consumer/decoder before reading its tables; never use stock artwork
        # as a guess for a custom routine that we do not understand.
        if (span(target,0x00a48c,0xcc)!=span(stock,0x00a48c,0xcc) or
                span(target,0x039806,140)!=span(stock,0x039806,140) or
                span(target,0x00a561,2)!=bytes.fromhex('01 18') or
                span(target,0x00a566,2)!=bytes.fromhex('00 20')):
            raise ValueError('Unsupported native horizon consumer')
        dma=int.from_bytes(span(target,0x00a563,3),'little')
        span(target,dma,0x2000)
        synthesize('sky_graphics',dma.to_bytes(3,'little')*count,'Recognized native horizon DMA descriptor')
        for key,delta,size in [('sky_back',0,0x700),('sky_front',2,0x540)]:
            pointers=[];decoded={}
            for slot in range(count):
                venue=span(target,int(layout['settings'][0],16)+slot,1)[0]&15
                if venue>=9:raise ValueError('Invalid native horizon venue')
                offset=int.from_bytes(span(target,0x00b19d+venue*4+delta,2),'little')
                if offset not in decoded:
                    data=decode_native_horizon(target,0x0f8000+offset,size)
                    decoded[offset]=synthesize(f'_{key}_{offset:04x}',data,'Decoded donor native horizon tilemap')
                    layout.pop(f'_{key}_{offset:04x}')
                address=decoded[offset]
                pointers.append(address.to_bytes(3,'little'))
            synthesize(key,b''.join(pointers),'Recognized donor venue horizon pointer table')
    terrain=pointer('terrain', '8b c2 30 da 9b ad 59 10 0a 6d 59 10 aa bf',
                    '18 79 d0 0c a8 e2 20 bf', 'fa 48 ab e0 00 00', 2,optional=True)
    if terrain is None:
        flags,consumer,_=find_consumer(target,'ad 5b 10 0a aa bf',
            'd0 0a ad f5 0c 29 ff 00 5c 15 a3 00 5c 49 a3 00')
        if span(target,0x00a30f,4)!=b'\x5c'+consumer.to_bytes(3,'little'):
            raise ValueError('Unrecognized native terrain bypass')
        for row in probe['tracks']:
            if not int.from_bytes(span(target,flags+(row['order_table_position']//5)*2,2),'little'):
                raise ValueError('Mixed native/extended terrain needs another decoder')
        address=synthesize('_terrain_data',native_terrain_properties(target,stock),
                           'Verified donor native tile classifiers translated to FZEdit properties; mine bitmap bypass is separate')
        layout.pop('_terrain_data')
        synthesize('terrain',address.to_bytes(3,'little')*count,'Recognized per-cup native terrain bypass')
    pointer('gradients', 'ae 59 10 bf', '85 9c 5c 22 a1 00')
    shortcut=pointer('shortcuts', '8b e2 20 bf', '48 ab c2 20 bf',
                     'aa bd 00 00 30 3f af 6d 10 00', -2,optional=True)
    if shortcut is None:
        shortcut=pointer('shortcuts', '8b e2 20 bf', '48 ab c2 20 bf',
                         'aa bd 00 00 30 3b ad 6d 10', -2,optional=True)
    if shortcut is None:
        if span(target,0x00d9c8,1)!=b'\x60':
            raise ValueError('Unrecognized shortcut resource consumer')
        empty=synthesize('empty_shortcuts',b'\0\x80','Donor shortcut entry point immediately returns')
        layout.pop('empty_shortcuts')
        synthesize('shortcuts',empty.to_bytes(3,'little')*count,'Donor disables shortcut penalties')
    else:
        layout['shortcuts'] = [f'{shortcut-2:06x}']
        evidence['tables']['shortcuts']['address']=layout['shortcuts'][0]
    pointer('palette_cycles', 'c2 30 8b 4b ab ad 59 10 0a aa bc', 'be 00 00 30 1c', width=2)
    if pointer('minimaps', 'ad 59 10 0a 6d 59 10 aa 8b bf', 'a8 e2 20 bf',
               '48 a9 80 8d 15 21', 2,optional=True) is None:
        pointer('minimaps', 'ad 59 10 0a 6d 59 10 aa bf', 'a8 8b e2 20 bf', '48 ab a9 80 8d 15 21', 2)
    if probe.get('minimap_constants') is not None:
        synthesize('map_positions',b''.join(value.to_bytes(2,'little') for value in probe['minimap_constants'])*count,
                   'Exact constant X/Y loads in the recognized donor minimap consumer')
    else:
        pointer('map_positions', 'ad 59 10 0a 0a aa bf', '8d d9 0a bf', '8d db 0a', 2)
    # The map loader reads grid rows at table+3 before the block pointer.
    pointer('maps', 'bf', '85 26 bf', '85 22 bf', -3)
    layout['maps'] = [f'{int(layout["maps"][0],16)-3:06x}']
    evidence['tables']['maps']['address']=layout['maps'][0]
    opponents = pointer('opponents', 'ad 59 10 0a 6d 59 10 65 02 aa bf', 'e2 30 8d 66 10 6b', optional=True)
    if opponents is None:
        # FZEdit projects without opponent overrides retain original GP rules.
        # Present these as resource-slot triples for the native typed decoder.
        data = bytearray(count*3)
        for position, slot in enumerate(probe['source_order_prefix']):
            for level in range(3):
                data[slot*3+level] = span(stock, 0x02fbda+level*15+position%15, 1)[0]
        offset = (len(normalized)+0x7fff) & ~0x7fff
        address = ((offset//0x8000)<<16)|0x8000
        normalized = normalized+bytes(offset-len(normalized))+data
        span(normalized, address, len(data))
        layout['opponents'] = [f'{address:06x}']
        evidence['opponents_policy'] = ('Original engine explosive-opponent frequencies by source GP position; '
                                        'donor global opponent changes are excluded')
        evidence['synthesized_data'] = dict(address=f'{address:06x}', bytes=len(data), sha256=digest(data))
    else:
        evidence['opponents_policy'] = 'Recognized donor per-resource opponent table'
    if probe.get('music_selector_kind')=='venue':
        music=bytearray(count)
        for row in probe['tracks']:music[row['slot']]=row['spc_selector_byte']
        synthesize('music',bytes(music), 'Recognized donor venue/variant index and SPC upload selector; no filename guesses')
    # Preserve changed, used intro glyph artwork through the known remapper.
    extended_font=span(target, 0x00d146, 30) == bytes.fromhex(
            '08 e2 20 c9 bb b0 31 eb 29 7f eb aa bf 50 81 10 48 '
            'bf 95 80 10 85 00 f0 03 20 93 d1 fa f0')
    if not extended_font and span(target,0x00d146,80)!=span(stock,0x00d146,80):
        raise ValueError('The intro font resource remapper is unsupported')
    native_font, donor_font = atlas(stock), atlas(target)
    if not extended_font and donor_font!=native_font:
        raise ValueError('Changed native intro font needs a supported decoder')
    used = set()
    for row in probe['tracks']:
        address = int.from_bytes(span(target, int(layout['names'][0],16)+row['slot']*3,3),'little')
        used.update(span(target,address,128).split(b'\0',1)[0][6:])
    for code in sorted(used-{0xff}):
        if not extended_font:
            continue # Original native remapper and atlas were checked byte for byte.
        lookup = 0x8e if code == 0xfe else code
        top = span(target, 0x108095+lookup,1)[0]
        bottom = span(target, 0x108150+lookup,1)[0]
        if any(tile not in donor_font or donor_font[tile][1] != 2 for tile in (top,bottom)):
            raise ValueError('The donor intro font uses unsupported compression or blank/flip semantics')
        original = native_font.get(lookup), native_font.get(lookup+16)
        if any(item is None for item in original) or tuple(donor_font[tile][2] for tile in (top,bottom)) != tuple(item[2] for item in original):
            layout.setdefault('intro_glyph',[]).append(f'{code:02x}|{donor_font[top][0]:06x}|{donor_font[bottom][0]:06x}')
    evidence['intro_glyph_overrides'] = layout.get('intro_glyph',[])
    masks = ((0x0098b1,'29 04'),(0x0098bc,'29 f4'),(0x0098ce,'29 14'),(0x0098f1,'29 f4'),(0x0098f9,'89 04'))
    if all(span(target,address,2) == bytes.fromhex(opcodes) for address,opcodes in masks):
        layout.setdefault('require',[]).append('all|grip-magnets')
        evidence['mechanics'] = ['grip-magnets: five recognized native damage-mask instruction changes']
    else:
        evidence['mechanics'] = ['Base typed terrain resources; additional donor ASM is excluded']
    evidence['unsupported_global_code_executed'] = False
    return normalized, layout, evidence


def review_form(report, probe):
    tracks = probe['tracks']; cups = (len(tracks)+4)//5
    stem = PurePosixPath(report.get('donor_member', report['input_name'])).stem
    fields = [dict(id='pack_name', label='Pack name', type='text', value=stem.encode('utf-8')[:95].decode('utf-8','ignore')),
              dict(id='author', label='Course author', type='text', value='Unknown author')]
    options = [dict(value=f'cup-{i+1}',label=f'Cup {i+1}') for i in range(cups)]
    for i in range(cups):
        names = ', '.join(row.get('name', f'Course {row["slot"]+1}') for row in tracks[i*5:i*5+5])
        donor_label=probe.get('donor_cup_menu',{}).get('labels',[])
        donor_label=donor_label[i].get('name') if i<len(donor_label) else None
        origin='Extracted donor menu label.' if donor_label else 'Suggested user label; source menu label not decoded.'
        fields.append(dict(id=f'cup_{i+1}_name', label=f'Cup {i+1} name', type='text', value=donor_label or f'Cup {i+1}',
                           description=f'{origin} Initial courses: {names}'))
    for position,row in enumerate(tracks):
        if 'name' not in row:
            fields.append(dict(id=f'name_slot_{row["slot"]}', label=f'Course {position+1} name', type='text',
                               value=f'Course {position+1}', description=row.get('name_error','Name needs review')))
        fields.append(dict(id=f'cup_for_slot_{row["slot"]}', label=row.get('name',f'Course {position+1}')+' cup',
                           type='choice', value=f'cup-{position//5+1}', options=options))
    return dict(fields=fields, courses=tracks, target_sha256=report['target_sha256'], input_sha256=report['input_sha256'],
                description='Confirm the pack labels and cup assignments. Each cup needs 1–5 courses; '
                'source race order is kept within each cup. Courses use supported course rules; '
                'donor vehicles, global gameplay changes, menus and executable code are excluded. '
                'Extraction does not certify complete race playability.')


def read_review_answers(report, answers):
    data = json.loads(read_bounded(answers, 128*1024).decode('utf-8'))
    if (not isinstance(data,dict) or data.get('target_sha256') != report.get('target_sha256')
            or data.get('input_sha256') != report.get('input_sha256') or 'target_sha256' not in report):
        raise ValueError('Review answers belong to a different source or target; select the source again')
    return data


def review_values(report, form, answers):
    values = {field['id']: field['value'] for field in form['fields']}
    if answers is not None:
        data = read_review_answers(report,answers)
        provided = data.get('values')
        if not isinstance(provided,dict) or any(key not in values for key in provided):
            raise ValueError('Review answers contain unknown fields')
        if any(not isinstance(value,str) or len(value)>256 for value in provided.values()):
            raise ValueError('Review answers must be strings of at most 256 characters')
        values.update(provided)
    for field in form['fields']:
        value = values[field['id']]
        if field['type'] == 'choice':
            if value not in {option['value'] for option in field['options']}:
                raise ValueError(f'Invalid choice for {field["label"]}')
        elif (not value.strip() or len(value.encode('utf-8'))>95 or any(ord(char)<32 or char=='|' for char in value)):
            raise ValueError(f'{field["label"]} needs 1–95 UTF-8 bytes of single-line text')
        values[field['id']] = value.strip()
    cup_ids = [option['value'] for option in next(field for field in form['fields'] if field['type']=='choice')['options']]
    assignments = Counter(values[f'cup_for_slot_{row["slot"]}'] for row in form['courses'])
    if any(not 1 <= assignments[cup] <= 5 for cup in cup_ids):
        raise ValueError('Each cup must have 1–5 courses; adjust the course assignments')
    return values


def inferred_manifest(report, probe, values):
    identity = 'custom-'+report['target_sha256'][:24]
    data = dict(format=['1'],id=[identity],name=[values['pack_name']],author=[values['author']],
                adapter=['fzero-course-v1'],source_sha256=[STOCK_SHA256],target_sha256=[report['target_sha256']],cup=[],track=[])
    for i in range((len(probe['tracks'])+4)//5):
        data['cup'].append(f'cup-{i+1}|{values[f"cup_{i+1}_name"]}|{i}')
    for row in probe['tracks']:
        slot=row['slot']; name=values.get(f'name_slot_{slot}',row.get('name',f'Course {slot+1}'))
        data['track'].append(f'course-{slot+1}|{name}|{values[f"cup_for_slot_{slot}"]}|{slot}')
    validate_identities(data)
    return data


def write_fields(path, data):
    path.write_text(''.join(f'{key}={value}\n' for key,values in data.items() for value in values), encoding='utf-8')


def publish_pack(pack, out):
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.fzc-publish-', dir=out.parent) as publication:
        staged = Path(publication)/'pack'
        shutil.copytree(pack, staged)
        if out.exists() or out.is_symlink():
            raise ValueError(f'Refusing to replace existing output: {out}')
        staged.rename(out)


def write_review_report(out, report):
    out.mkdir(parents=True, exist_ok=False)
    (out/'conversion-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    text = ('The editable courses passed native extraction and round-trip validation.\n'
            'Confirm the labels and cup assignments in Custom Content to finish import.\n'
            'Vehicles and global donor game changes are excluded. No pack was installed.\n') if report['status']=='needs-input' else REVIEW
    (out/'REVIEW.txt').write_text(text,encoding='utf-8')


def convert_inferred(source, out, target, stock, report, probe, exporter, inspector, answers):
    normalized, layout, evidence = infer_fzedit_layout(target,stock,probe)
    form = review_form(report,probe)
    required=[row.split('|',1)[1] for row in layout.get('require',[])]
    if required:
        form['description'] += ' Recognized supported course mechanics: '+', '.join(required)+'.'
    values = review_values(report,form,answers)
    data = inferred_manifest(report,probe,values)
    identity = data['id'][0]
    if probe.get('msu_selector',{}).get('recognized'):
        layout['msu_source'] = [identity]
        layout['msu'] = [f'{row["slot"]}|{row["msu_track"]}' for row in probe['tracks']]
        data['soundtrack_prefix'] = [PurePosixPath(report.get('donor_member',report['input_name'])).stem]
        data['menu_music'] = [f'{cue}|music/menu-{number}.pcm'
                             for cue, number in probe['msu_selector'].get('events', {}).items()]
    report.update(inferred_layout=layout, inference=evidence, review=form, required_mechanics=required,
                  gameplay_qualification='Course data only; vehicles/global donor code excluded; full races not certified',
                  cup_labels='User labels and confirmed assignments; not claimed as authored donor menu labels',
                  title_policy='Original game presentation; donor title/menu changes excluded')
    report['warnings'].append('Courses use the current engine. Donor vehicles, global gameplay changes, '
                              'menus and executable code are excluded; test the courses before relying on complete race playability.')
    report['warnings'].append(evidence['opponents_policy'])
    with tempfile.TemporaryDirectory(prefix='fzc-') as temporary:
        temp = Path(temporary); base=temp/'inferred'; pack=temp/'packs'/identity
        baseline=temp/'native'; baseline.mkdir()
        write_fields(base.with_suffix('.ini'),data); write_fields(base.with_suffix('.layout'),layout)
        credits=(f'{values["pack_name"]}\nCourse author: {values["author"]}\n'
                 f'Imported from {report["input_name"]}. Preserve the original author files and credits.\n'
            f'Editable reconstruction from {probe.get("loader_family", "FZEdit")} resource data; not original author project.\n').encode('utf-8')
        extraction=export_pack(stock,normalized,base,pack,exporter,native_resources=baseline,
            credit_bytes=credits, source_label=f'recognized {probe.get("loader_family", "FZEdit")} extraction; user-confirmed course pack')
        report['validation']=inspect_roundtrip(inspector,temp,pack,baseline,extraction)
        report['validation']['byte_exact_to_extracted_courses']=report['validation'].pop('byte_exact_to_donor_courses')
        report['validation']['basis']='Typed donor resources plus the explicit opponent policy in inference'
        report.update(pack_id=identity,metadata=dict(tracks=probe['tracks'], source_order=probe['source_order_prefix']),
                      omissions=['donor executable patches, vehicles, menus and global game rules',
                                 'original author editing layers/history for reconstructed courses'])
        if answers is None:
            report.update(status='needs-input',message=f'{len(probe["tracks"])} editable courses passed validation. '
                          'Confirm the pack name, cup labels and course assignments to import them.')
        else:
            copy_archive_audio(source,report,pack)
            report.update(status='converted',review_answers=values,
                          message=f'{len(probe["tracks"])} editable courses imported with your labels and cup assignments.')
            # Audio is optional; installed course data already passed byte checks.
            (pack/'conversion-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            publish_pack(pack,out)
    if answers is None:
        write_review_report(out,report)
    return report


def copy_archive_audio(source, report, pack):
    """Map recordings only through existing course/menu metadata, stream copies."""
    inventory = report.get('audio_inventory')
    if inventory is None:
        return
    index = json.loads((pack/'courses.json').read_text(encoding='utf-8'))
    donor_path = PurePosixPath(report['donor_member'])
    prefixes = {item['prefix'].casefold() for item in index.get('soundtracks', [])}
    for item in inventory['members']:
        path = PurePosixPath(item['member'])
        if item['format'] == 'msu' and path.parent == donor_path.parent and path.stem == donor_path.stem:
            prefixes.add(path.stem.casefold())
    recordings = [item for item in inventory['members'] if item['format'] == 'pcm' and item.get('header_valid')]
    plans, blocked = {}, set()
    def assign(destination, candidates):
        if destination in blocked:
            return
        previous = plans.get(destination)
        if len(candidates) > 1 or (candidates and previous is not None
                                  and previous['member'] != candidates[0]['member']):
            blocked.add(destination)
            plans.pop(destination, None)
            for item in candidates + ([previous] if previous is not None else []):
                item['mapping_reason'] = f'Multiple recordings match {destination}; choose one manually'
            return
        if candidates:
            plans[destination] = candidates[0]
    def numbered(item, number):
        match = re.fullmatch(r'(.+)-(\d+)\.pcm', PurePosixPath(item['member']).name, re.IGNORECASE)
        return match is not None and match[1].casefold() in prefixes and int(match[2]) == number
    for course in index['courses']:
        stem = PurePosixPath(course['source']).stem
        music = course.get('music', {})
        matches = [item for item in recordings if PurePosixPath(item['member']).name.casefold() == (stem+'.pcm').casefold()
                   or ('track' in music and numbered(item, music['track']))]
        assign(f'music/{stem}.pcm', matches)
    for destination in sorted(set(index.get('menu_music', {}).values())):
        path = PurePosixPath(destination)
        if (path.is_absolute() or '..' in path.parts or '\\' in destination or ':' in destination
                or not path.parts or path.parts[0] != 'music'):
            raise ValueError('Invalid reviewed menu music destination')
        match = re.fullmatch(r'.+-(\d+)\.pcm', path.name, re.IGNORECASE)
        candidates = [item for item in recordings if PurePosixPath(item['member']).name.casefold() == path.name.casefold()
                      or (match is not None and numbered(item, int(match[1])))]
        assign(destination, candidates)
    if sum(item['bytes'] for item in plans.values()) > ZIP_TOTAL_LIMIT:
        report['warnings'].append('Mapped recordings would exceed the 2 GiB publication limit; '
                                  'courses were imported without recordings. Place music manually.')
        plans.clear()
    if blocked:
        report['warnings'].append(f'{len(blocked)} course/menu music destinations had multiple matching recordings; '
                                  'those recordings were skipped. Choose one manually; see audio_inventory.')
    with bounded_zip(source) as archive:
        for destination, item in plans.items():
            target = pack/Path(destination); target.parent.mkdir(parents=True, exist_ok=True)
            partial = target.with_name('.'+target.name+'.part')
            checksum, count = hashlib.sha256(), 0
            created = False
            try:
                entry = archive.getinfo(item['member'])
                if entry.file_size != item['bytes'] or f'{entry.CRC:08x}' != item['zip_crc32']:
                    raise ValueError('Archive changed during conversion')
                if target.exists():
                    raise ValueError('Music destination already exists')
                with partial.open('xb') as output:
                    created = True
                    with archive.open(entry) as original:
                        while chunk := original.read(1024*1024):
                            count += len(chunk)
                            if count > ZIP_FILE_LIMIT or count > item['bytes']:
                                raise ValueError('Recording exceeds its declared limit')
                            output.write(chunk); checksum.update(chunk)
                if count != item['bytes']:
                    raise ValueError('Truncated recording')
                partial.rename(target)
            except (ValueError, KeyError, zipfile.BadZipFile, OSError, RuntimeError, NotImplementedError,
                    EOFError, lzma.LZMAError, zlib.error) as error:
                item['copy_error'] = str(error)
                report['warnings'].append(f'Recording {item["member"]} could not be installed: {error}. '
                                          'Courses are available; replace or place this recording manually.')
                continue
            finally:
                if created:
                    partial.unlink(missing_ok=True)
            item.update(mapping='installed', sha256=checksum.hexdigest())
            item.setdefault('destinations', []).append(destination)
    inventory['mapped_pcm_count'] = sum(item['mapping'] == 'installed' for item in recordings)
    inventory['unmapped_pcm_count'] = inventory['pcm_count']-inventory['mapped_pcm_count']
    inventory['copied_bytes'] = sum(item['bytes']*len(item.get('destinations', [])) for item in recordings)
    if inventory['unmapped_pcm_count']:
        amount = inventory['unmapped_pcm_count']
        report['warnings'].append(f'{amount} PCM recording'+(' was' if amount == 1 else 's were')+' not installed because '
            'they have no unambiguous reviewed course/menu mapping or a valid header. '
            'They remain in your unchanged ZIP. Place them in the installed pack\'s music folder manually; '
            'see audio_inventory for unresolved filenames and CONVERSION.md for naming.')
    unsupported = sum(item['format'] not in ('pcm', 'msu') for item in inventory['members'])
    if unsupported:
        report['warnings'].append(f'{unsupported} other audio files were not installed; this importer plays MSU PCM recordings. '
                                  'The original files remain in your unchanged ZIP.')


def validate_identities(data):
    """Protect output paths and preserve unambiguous registry identities/order."""
    for field in ('format', 'id', 'name', 'author', 'adapter', 'source_sha256'):
        if len(data.get(field, [])) != 1:
            raise ValueError(f'Registry needs exactly one {field}')
    if (data['format'] != ['1'] or data['adapter'] != ['fzero-course-v1']
            or data['source_sha256'] != [STOCK_SHA256]
            or not ID_PATTERN.fullmatch(data['id'][0])):
        raise ValueError('Unsupported registry identity or format')
    cups, tracks, slots = {}, set(), set()
    for row in data.get('cup', []):
        parts = row.split('|')
        if (len(parts) != 3 or not ID_PATTERN.fullmatch(parts[0])
                or parts[0] in cups or not parts[1] or not parts[2].isdecimal()):
            raise ValueError('Invalid or duplicate cup identity')
        cups[parts[0]] = 0
    for row in data.get('track', []):
        parts = row.split('|')
        if (len(parts) != 4 or not ID_PATTERN.fullmatch(parts[0])
                or parts[0] in tracks or not parts[1] or parts[2] not in cups
                or not parts[3].isdecimal() or not 0 <= int(parts[3]) < 128
                or int(parts[3]) in slots):
            raise ValueError('Invalid, duplicate or missing course identity')
        tracks.add(parts[0]); slots.add(int(parts[3])); cups[parts[2]] += 1
    if not 1 <= len(cups) <= 32 or not 1 <= len(tracks) <= 128 or any(not 1 <= n <= 5 for n in cups.values()):
        raise ValueError('A pack needs 1–32 cups with 1–5 courses each')


def changed_summary(stock, target):
    """Bound the report even for an image with alternating changed bytes."""
    ranges, banks, count, range_count, start = [], Counter(), 0, 0, None
    for offset in range(min(len(stock), len(target)) + 1):
        changed = offset < min(len(stock), len(target)) and stock[offset] != target[offset]
        if changed:
            count += 1; banks[offset // 0x8000] += 1
            if start is None:
                start = offset
        elif start is not None:
            range_count += 1
            if len(ranges) < 128:
                ranges.append(dict(file_offset=start, length=offset-start, ownership='unreviewed'))
            start = None
    return dict(changed_stock_bytes=count, size_delta=len(target)-len(stock),
                changed_stock_bytes_by_bank={f'{bank:02x}': n for bank, n in sorted(banks.items())},
                changed_ranges=ranges, changed_range_count=range_count,
                changed_ranges_truncated=range_count > len(ranges))


def inspect_roundtrip(inspector, temp, pack, baseline, extraction):
    dump = temp/'roundtrip'; dump.mkdir()
    run = subprocess.run([str(Path(inspector).resolve()), str(pack.parent), '--dump', str(dump)],
                         cwd=temp, capture_output=True, text=True, timeout=120, check=False,
                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if run.returncode or run.stderr.strip():
        raise ValueError(f'Native pack validation failed: {run.stdout} {run.stderr}')
    index = json.loads((pack/'courses.json').read_text(encoding='utf-8'))
    ident = index['id']
    expected = {f'{ident}--{course["id"]}.fzc' for course in index['courses']}
    if {p.name for p in dump.glob('*.fzc')} != expected:
        raise ValueError('Native validation did not load every declared course')
    hashes = {}
    for line in run.stdout.splitlines():
        match = re.fullmatch(re.escape(ident)+r'/([a-z0-9-]+) ([0-9a-f]{64})', line)
        if match:
            if match[1] in hashes:
                raise ValueError('Native validation returned a duplicate course')
            hashes[match[1]] = match[2]
    if hashes != extraction['record_hashes']:
        raise ValueError('Editable projects changed normalized course record hashes')
    exact = []
    for course in index['courses']:
        cid = course['id']
        if ident == 'cgp' and cid == 'huckmine':
            continue  # Preserved author source intentionally supersedes older ROM data.
        if (dump/f'{ident}--{cid}.fzc').read_bytes() != (baseline/f'{cid}.fzc').read_bytes():
            raise ValueError(f'Editable round trip changed native course fields: {cid}')
        exact.append(cid)
    title_hashes = {title['id']: digest((pack/title['source']).read_bytes())
                    for title in index.get('titles', [])}
    return dict(native_loader_passed=True, record_hashes=hashes,
                byte_exact_to_donor_courses=exact, validated_title_sha256=title_hashes,
                fields_checked='all serialized native course fields, including SPC and intro glyphs')


def convert(source, out, *, stock=None, exporter=None, inspector=None, patch_member=None, answers=None):
    """Publish one new directory only after validation; never alter the inputs."""
    out = Path(out)
    if out.exists() or out.is_symlink():
        raise ValueError(f'Refusing to replace existing output: {out}')
    out = out.resolve()
    stock_data = read_stock(stock) if stock is not None else None
    target, report = read_submission(source, stock_data, patch_member)
    if answers is not None:
        read_review_answers(report,answers)
    profile = reviewed_profile(target) if target is not None else None
    report.update(format='fzero.content-conversion', version=1, source_rom_exported=False,
                  donor_code_executed=False)
    report.setdefault('warnings', [])
    if profile is None:
        if target is not None:
            probe = report['donor_probe'] = probe_fzedit_metadata(target)
            count = probe.get('internal_resource_count')
            recordings = report.get('audio_inventory', {}).get('pcm_count', 0)
            detected = f'Detected {count} internal course resources' if count else 'The donor needs a reviewed course layout'
            if recordings:
                detected += f' and {recordings} PCM recordings'
            report['reason'] = (detected+'. The resource layout needs a supported decoder. '
                                'No courses or recordings were installed; your original files are unchanged.')
            if probe.get('unsupported_reason'):
                report['reason'] = probe['unsupported_reason'] + ' No courses or recordings were installed.'
            if probe['status'] == 'recognized-resource-metadata' and stock_data is not None:
                # Bad/stale form values are input errors, not failed extraction.
                if answers is not None:
                    review_values(report,review_form(report,probe),answers)
                native_exporter=Path(exporter or native_tool('FZeroExportCourses.exe')).resolve()
                native_inspector=Path(inspector or native_tool('FZeroInspectPacks.exe')).resolve()
                if not native_exporter.is_file() or not native_inspector.is_file():
                    raise ValueError('Build FZeroExportCourses/FZeroInspectPacks or supply --exporter and --inspector')
                try:
                    report.pop('reason',None)
                    return convert_inferred(source,out,target,stock_data,report,probe,
                                            native_exporter,native_inspector,answers)
                except (ValueError,subprocess.SubprocessError) as error:
                    detail = str(error)
                    if isinstance(error,subprocess.CalledProcessError):
                        detail = (error.stderr or error.stdout or detail).strip()
                    report.pop('review',None)
                    report.update(structural_error=detail,reason='The candidate course resources could not be extracted '
                                  f'and round-trip validated: {detail}. No pack was installed.')
        report.update(status='review-required', qualification_document='mods/PARSE_MANIFEST.md')
        if report.get('audio_inventory', {}).get('pcm_count'):
            report['warnings'].append('Recordings were inventoried, not assigned or installed. '
                                      'They remain in your unchanged ZIP; donor MSU behavior needs review.')
        if stock_data is not None and target is not None:
            report['file_difference_evidence'] = changed_summary(stock_data, target)
        write_review_report(out,report)
        return report
    if stock_data is None:
        raise ValueError('Known ROM export requires --stock for reviewed title resources')
    exporter = Path(exporter or native_tool('FZeroExportCourses.exe')).resolve()
    inspector = Path(inspector or native_tool('FZeroInspectPacks.exe')).resolve()
    if not exporter.is_file() or not inspector.is_file():
        raise ValueError('Build FZeroExportCourses/FZeroInspectPacks or supply --exporter and --inspector')
    data = fields(profile); validate_identities(data)
    metadata = audit_metadata(target, fields(profile.with_suffix('.layout')), data)
    report.update(status='converted', pack_id=data['id'][0], profile=profile.name,
                  profile_sha256=digest(profile.read_bytes()),
                  layout_sha256=digest(profile.with_suffix('.layout').read_bytes()),
                  metadata=metadata, cup_labels='previously reviewed registry labels; not redecoded here',
                  gameplay_qualification='Existing profile qualification; this run checks data only')
    out.parent.mkdir(parents=True, exist_ok=True)
    # Native ZIP/source caches add long names beneath this directory. Keep them
    # outside the caller's nested installation stage to stay below MAX_PATH.
    with tempfile.TemporaryDirectory(prefix='fzc-') as temporary:
        temp = Path(temporary)
        pack = temp/'packs'/data['id'][0]
        baseline = temp/'native'; baseline.mkdir()
        extraction = export_pack(stock_data, target, profile.with_suffix(''), pack, exporter,
                                 KNOWN_IDS.index(data['id'][0]), native_resources=baseline)
        copy_archive_audio(source, report, pack)
        report['validation'] = inspect_roundtrip(inspector, temp, pack, baseline, extraction)
        if data['id'][0] == 'cgp':
            report['preserved_author_source'] = dict(course='huckmine',
                donor_record_hash=extraction['extracted_huckmine_hash'],
                selected_record_hash=extraction['record_hashes']['huckmine'],
                note='Original Huckmine FZEdit project retained, matching the shipped pack; differs from donor extraction')
        report['omissions'] = ['donor executable patches, vehicles and global game rules',
                               'original author editing layers/history for reconstructed courses']
        if not report.get('audio_inventory', {}).get('pcm_count'):
            report['omissions'].append('MSU PCM recordings (add separately)')
        report['title_policy'] = ('Reviewed presentation profile; Astra reuses the CGP title '
                                  'supplied by the CGP pack, Bower uses Original')
        (pack/'conversion-report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        # The short workspace may be on another volume. Copy a complete,
        # validated pack beside the destination, then publish by local rename.
        publish_pack(pack,out)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--out', required=True, type=Path, help='new final pack/report directory')
    parser.add_argument('--stock', type=Path, help='private original F-Zero USA ROM')
    parser.add_argument('--exporter', type=Path)
    parser.add_argument('--inspector', type=Path)
    parser.add_argument('--answers', type=Path, help='source-bound JSON review answers from Custom Content')
    parser.add_argument('--patch-member', '--member', dest='patch_member',
                        help='exact IPS/BPS/ROM member name in an ambiguous ZIP')
    args = parser.parse_args(argv)
    try:
        report = convert(args.source, args.out, stock=args.stock, exporter=args.exporter,
                         inspector=args.inspector, patch_member=args.patch_member, answers=args.answers)
    except (ValueError, OSError, zipfile.BadZipFile, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'{error}\n')
    if report['status'] == 'review-required':
        print(f'Unsupported course format: {args.out}/conversion-report.json; see CONVERSION.md')
        return 2
    if report['status'] == 'needs-input':
        print(f'Confirm course labels and cup assignments: {args.out}/conversion-report.json')
        return 3
    print(f'{report["pack_id"]}: validated editable course pack in {args.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
