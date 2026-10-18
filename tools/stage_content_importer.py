"""Add the opt-in importer helper and its DLL closure to a developer build.

Run after building the game, FZeroExportCourses and FZeroInspectPacks. Never
copies ROMs or changes installed packs, saves, or settings.
"""
import argparse
import importlib.metadata
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def write_notices(destination):
    destination = Path(destination) / 'content-importer'
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copy2(Path(sys.base_prefix) / 'LICENSE.txt', destination / 'Python.txt')
    for package, output in (('pillow', 'Pillow.txt'), ('pyinstaller', 'PyInstaller.txt')):
        distribution = importlib.metadata.distribution(package)
        for item in distribution.files:
            if item.name in ('LICENSE', 'COPYING.txt') and 'licenses' in item.parts:
                shutil.copy2(distribution.locate_file(item), destination / output)
                break
        else:
            raise ValueError(f'Missing {package} license')


def stage(build, converter, mingw):
    build, converter, mingw = map(Path, (build, converter, mingw))
    required = [build / 'F-Zero Forever.exe', build / 'FZeroExportCourses.exe',
                build / 'FZeroInspectPacks.exe', converter]
    for path in required:
        if not path.is_file():
            raise ValueError(f'Build or provide {path} first')
    target = build / 'FZeroConvertContent.exe'
    if converter.resolve() != target.resolve():
        shutil.copy2(converter, target)
    notices = converter.parent / 'licenses/content-importer'
    if not notices.is_dir():
        raise ValueError('Rebuild converter to generate its license notices')
    notice_target = build / 'licenses/content-importer'
    if notices.resolve() != notice_target.resolve():
        shutil.copytree(notices, notice_target, dirs_exist_ok=True)
    pending = required[:3] + [target]
    seen = set()
    system = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32'
    while pending:
        binary = pending.pop()
        imports = subprocess.check_output([str(mingw / 'bin/objdump.exe'), '-p', str(binary)], text=True)
        for name in re.findall(r'DLL Name:\s*(\S+)', imports):
            key = name.lower()
            if key in seen:
                continue
            seen.add(key)
            if key.startswith(('api-ms-', 'ext-ms-')) or (system / name).is_file():
                continue
            source = next((p / name for p in (build, mingw / 'bin') if (p / name).is_file()), None)
            if source is None:
                raise ValueError(f'Missing dependency {name}')
            dest = build / name
            if source.resolve() != dest.resolve():
                shutil.copy2(source, dest)
            pending.append(dest)
    print(f'Importer helper and {len(seen)} checked dependencies: {build.resolve()}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--converter', type=Path)
    parser.add_argument('--write-notices', type=Path, help='helper build: save licenses from this Python environment')
    parser.add_argument('--mingw', type=Path, default=Path('C:/msys64/mingw64'))
    args = parser.parse_args()
    if args.write_notices:
        write_notices(args.write_notices)
    elif args.build and args.converter:
        stage(args.build, args.converter, args.mingw)
    else:
        parser.error('Supply --build and --converter, or --write-notices')
