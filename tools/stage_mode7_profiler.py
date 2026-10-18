"""Stage a one-off, ROM-free Mode 7 profiler from a reviewed player bundle.

No private ROMs, recordings, user configs or saves are added. The default
version preserves the original serial profiling bundle; worker builds can
also be staged with an explicit version and renderer description.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', default='0.8.0-mode7-profile')
    parser.add_argument('--renderer', default='unchanged serial renderer')
    args = parser.parse_args()
    base, exe, out = args.base.resolve(), args.exe.resolve(), args.output.resolve()
    if not base.is_dir() or not exe.is_file() or out.exists():
        parser.error('Need an existing reviewed bundle/executable and a fresh output directory.')
    image = exe.read_bytes()
    if b'--profile-mode7' not in image or args.version.encode('utf-8') not in image:
        parser.error('Executable is not the expected profiling build.')
    forbidden = {'.sfc', '.smc', '.srm', '.sav', '.bin', '.c', '.cpp', '.h', '.pcm', '.msu'}
    for path in base.rglob('*'):
        if not path.is_file():
            continue
        if (path.suffix.lower() in forbidden or path.name.lower() in
                {'config.ini', 'fzero-video.ini', 'keybinds.ini', 'rom.cfg'} or
                path.relative_to(base).parts[0] in {'saves', 'diagnostics', 'profile-results'}):
            parser.error(f'Base contains private/non-player material: {path.relative_to(base)}')
    shutil.copytree(base, out)
    shutil.copy2(exe, out / 'F-Zero Forever.exe')
    for path in (ROOT / 'tools/mode7_profiler').iterdir():
        if path.is_file():
            shutil.copy2(path, out / path.name)
    (out / 'profiling-build.json').write_text(json.dumps({
        'version': args.version,
        'renderer': args.renderer,
        'executable_sha256': hashlib.sha256(image).hexdigest(),
    }, indent=2) + '\n', encoding='utf-8')
    readme = out / 'README.txt'
    readme.write_text(readme.read_text(encoding='utf-8').replace(
        '{{BUILD_DESCRIPTION}}', f'Build {args.version}: {args.renderer}.'), encoding='utf-8')
    archive = out.parent / (out.name + '.zip')
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path in sorted(out.rglob('*')):
            if path.is_file():
                bundle.write(path, path.relative_to(out.parent))
    print(archive)
    print(f'{archive.stat().st_size / 1024 / 1024:.1f} MiB')
    print('SHA256 ' + hashlib.sha256(archive.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
