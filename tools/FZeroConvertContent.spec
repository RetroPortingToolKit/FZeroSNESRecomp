# Build with build_convert_course_content.ps1. Contains reviewed data, no ROM.
from pathlib import Path
import os

repo = Path(SPECPATH).parent
engine = Path(os.environ.get('SNESRECOMP_ROOT', repo/'snesrecomp'))
if not (engine/'tools/data_pack.py').is_file():
    raise SystemExit('Set SNESRECOMP_ROOT to a populated shared-engine checkout')
profiles = ('astra-front', 'bower-league', 'cgp', 'max-league', 'astra-velocity')
relative = [f'assets/track-packs/{ident}{suffix}' for ident in profiles for suffix in ('.ini', '.layout')]
relative += [f'assets/track-packs/{name}-credits.txt' for name in ('Astra-Front', 'Bower-League', 'CGP', 'MAX-League', 'Astra-Velocity')]
relative += ['assets/track-packs/presentation/cgp.ips', 'assets/track-packs/presentation/max-league.ips',
             'assets/music/cgp-menu.json', 'examples/huckmine/huckmine.zip', 'examples/huckmine/source.json']
datas = [(str(repo/path), str(Path(path).parent)) for path in relative]
datas.append((str(engine/'tools/data_pack.py'), 'snesrecomp/tools'))
a = Analysis([str(repo/'tools/convert_course_content.py')], pathex=[str(repo/'tools')],
             binaries=[], datas=datas, hiddenimports=[], hookspath=[], hooksconfig={},
             runtime_hooks=[], excludes=['tkinter', 'matplotlib', 'numpy', 'pytest', 'IPython'],
             noarchive=False)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name='FZeroConvertContent',
          debug=False, bootloader_ignore_signals=False, strip=False, upx=False,
          console=True, disable_windowed_traceback=False)
