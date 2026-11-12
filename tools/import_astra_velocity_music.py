"""Verify and stage the supplied Astra Velocity P3 course recordings."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile

from import_cgp_music import verify_file
from pack_manifest import course_source_stem

ROOT = Path(__file__).resolve().parents[1]


def manifest():
    return json.loads((ROOT / 'assets/music/astra-velocity.json').read_text(encoding='utf-8'))


def verify_music(folder):
    tracks = manifest()['tracks']
    if {p.name for p in folder.iterdir()} != tracks.keys():
        raise ValueError('Unexpected or missing Astra Velocity recordings')
    for name, entry in tracks.items():
        verify_file(folder / name, entry)
        with (folder / name).open('rb') as source:
            header = source.read(8)
        if header[:4] != b'MSU1' or struct.unpack_from('<I', header, 4)[0] != entry['loop']:
            raise ValueError('Unexpected PCM header or loop point')
        if (entry['bytes'] - 8) % 4 or not 0 <= entry['loop'] < (entry['bytes'] - 8) // 4:
            raise ValueError('Invalid stereo PCM length or loop point')
    return tracks


def stage_music(source, pack):
    tracks = verify_music(source)
    courses = json.loads((pack / 'courses.json').read_text(encoding='utf-8'))['courses']
    expected = {course_source_stem(pack, c) + '.pcm': c['music']['track'] for c in courses}
    if expected.keys() != tracks.keys() or any(tracks[name]['track'] != number for name, number in expected.items()):
        raise ValueError('Astra Velocity recordings do not match the authored course mapping')
    destination = pack / 'music'
    destination.mkdir(exist_ok=True)
    if {p.name for p in destination.iterdir()} - expected.keys():
        raise ValueError('Unexpected files in staged Astra Velocity music')
    for name in tracks:
        shutil.copy2(source / name, destination / name)
    verify_music(destination)
    return len(tracks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, nargs='?')
    parser.add_argument('--stage-course-from', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT / 'music/astra-velocity')
    args = parser.parse_args()
    if bool(args.archive) == bool(args.stage_course_from):
        parser.error('provide an archive or --stage-course-from')
    if args.stage_course_from:
        print(f'Staged {stage_music(args.stage_course_from, args.out)} Astra Velocity recordings')
        return
    metadata = manifest()
    with args.archive.open('rb') as source:
        if hashlib.file_digest(source, 'sha256').hexdigest() != metadata['source_archive_sha256']:
            raise ValueError('Unreviewed Astra Velocity soundtrack archive')
    with zipfile.ZipFile(args.archive) as archive:
        args.out.mkdir(parents=True, exist_ok=False)
        for name, entry in metadata['tracks'].items():
            if archive.getinfo(entry['source']).file_size != entry['bytes']:
                raise ValueError('Unexpected PCM size')
            with archive.open(entry['source']) as source, (args.out / name).open('xb') as target:
                shutil.copyfileobj(source, target)
    verify_music(args.out)
    print('Verified twenty Astra Velocity course recordings')


if __name__ == '__main__':
    main()
