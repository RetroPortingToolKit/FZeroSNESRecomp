"""Private-ROM integration for CGP MSU mapping, fallback and rewind.

Distinct synthetic stereo signatures identify the song at the real mixer.
All generated media and cartridge data remain in the requested output folder.
"""
import argparse
import collections
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import wave
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from pack_manifest import write_index

from validate_native_menus import player_route, press
from validate_practice_catalog import route as practice_route

ROOT = Path(__file__).resolve().parents[1]


def make_tracks(folder, prefix, numbers, bias=0):
    folder.mkdir(exist_ok=True)
    (folder / f"{prefix}.msu").write_bytes(b"")
    for number in numbers:
        (folder / f"{prefix}-{number}.pcm").write_bytes(
            b"MSU1" + struct.pack("<I", 0) +
            struct.pack("<hh", (number+bias) * 100, -(number+bias) * 100) * 4410)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("build", "stock", "out"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--legacy-patch", type=Path)
    parser.add_argument("--packs", type=Path, required=True)
    parser.add_argument("--filter", default="", help="Comma-separated case names")
    args = parser.parse_args()
    build, stock, out = args.build.resolve(), args.stock.resolve(), args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    pack, empty, legacy = out / "synthetic", out / "empty", out / "legacy"
    make_tracks(pack, "cgp", list(range(1, 6)) + [7] + list(range(10, 65)))
    make_tracks(pack, "F-ZERO Astra Front", range(10,19), bias=100)
    # Owner installations can already contain real recordings. Keep those
    # from masking synthetic signatures and deliberate missing-file cases.
    catalog = out / 'catalog'
    shutil.copytree(args.packs.resolve(), catalog, ignore=shutil.ignore_patterns('*.pcm', '*.msu'))
    menu = catalog / 'cgp/music'
    menu.mkdir(exist_ok=True)
    for number in (1, 2, 3, 4, 5, 7):
        shutil.copy2(pack / f'cgp-{number}.pcm', menu / f'cgp-{number}.pcm')
    empty.mkdir()
    clean = {k: v for k, v in os.environ.items()
             if not k.startswith(("FZERO_", "SNESRECOMP_", "SDL_", "LNG_"))}
    cups = [("knight-cgp", 10), ("queen-cgp", 15), ("king-cgp", 20),
            ("bs-1", 25), ("bs-2", 30)]
    cups += [(f"cgp-{i + 1}", 35 + i * 5) for i in range(6)]
    cases = [dict(name=cup, cup="cgp/" + cup, track=track) for cup, track in cups]
    cases += [
        dict(name="retail-queen", cup="retail/queen", track=15, packs=0),
        dict(name="bs-courses", cup="bs-deluxe/bs-2", track=30, packs=0, bs=True),
        dict(name="unknown-pack-fallback", cup="bower-league/bower", fallback=True),
        dict(name="missing-music", cup="cgp/cgp-1", empty=True, fallback=True),
        dict(name="rewind-cgp", cup="cgp/cgp-1", track=35, rewind=True),
        dict(name="practice-baron", practice=True, track=35),
        dict(name="practice-scepter", practice=True, track=40, league=12, rewind=True),
    ]
    cases += [dict(name="installed-shared",cup="astra-front/astra",track=110,installed=True),
              dict(name="astra",cup="astra-front/astra",track=110),
              dict(name="front",cup="astra-front/front",track=115,rewind=True),
              dict(name="astra-missing",cup="astra-front/front",ordinal=4,fallback=True)]
    cases += [dict(name="course-astra",cup="astra-front/astra",track=110,course_files=True,rewind=True),
              dict(name="course-huckmine",cup="cgp/cgp-4",track=50,course_files=True),
              dict(name="course-bower",cup="bower-league/bower",track=210,course_files=True),
              dict(name="course-missing",cup="astra-front/astra",fallback=True,course_files=True),
              dict(name="course-practice",cup="cgp/cgp-1",practice=True,track=35,course_files=True)]
    cases += [dict(name="menu-custom",cup="astra-front/astra",track=110,course_files=True,menu_custom=True),
              dict(name="menu-off",cup="astra-front/astra",track=110,course_files=True,menu_off=True)]
    if args.legacy_patch:
        make_tracks(legacy, "test", range(1, 32))
        shutil.copy2(args.legacy_patch, legacy / "f-zero_msu1.ips")
        cases += [dict(name="legacy-catalog", legacy=True, cup="bs-deluxe/knight"),
                  dict(name="legacy-stock", legacy=True, cup="retail/knight", packs=0)]
    if args.filter:
        cases = [c for c in cases if c["name"] in args.filter.split(",")]
    assert cases, "No matching cases"
    results = {}

    def run(case):
        folder = out / case["name"]
        folder.mkdir()
        (folder / "s").mkdir()
        shutil.copytree(ROOT / "assets", folder / "assets")
        packs, bs = case.get("packs", 7), case.get("bs", False)
        frames = 1850
        inputs = player_route(0, packs) + ",600-606:8,730-736:8,790-796:8,1160-1849:1"
        if not packs:
            inputs = "320-326:8,440-446:8,560-566:8,730-736:8,790-796:8,1160-1849:1"
        if case.get("practice"):
            # Actual vertical navigation: three retail cups, Bower, three
            # corrected originals, two corrected BS cups, then Baron.
            inputs = practice_route(0, 255) + ",1200-1206:8"
            inputs += "".join(press(1300 + i * 24, 32) for i in range(case.get("league", 11)))
            inputs += ",1720-1726:8,1900-1906:8,2300-3099:1"
            frames = 3100
        installed_root = None
        if case.get("installed"):
            installed_root=folder/'mods/packs'
            shutil.copytree(catalog,installed_root)
            shutil.copytree(pack,installed_root/'audio')
            audio=installed_root/'audio'
            music=audio/'music';music.mkdir()
            for f in audio.glob('*.pcm'):f.rename(music/f.name)
            write_index(audio,dict(format=1,id="test-audio",name="Test Audio",author="QA",
                                  soundtracks=[dict(id="cgp",prefix="cgp",directory="music")]))
        if case.get("course_files"):
            installed_root=folder/'mods/packs'
            shutil.copytree(catalog,installed_root)
            # Keep the existing shared menu cues. Race songs have no prefix or
            # new JSON map, including Bower which has no MSU metadata at all.
            menu=installed_root/'cgp/music';menu.mkdir(exist_ok=True)
            for number in (1,2,3,4,5,7):
                shutil.copy2(pack/f'cgp-{number}.pcm',menu/f'cgp-{number}.pcm')
            ident,cup=case['cup'].split('/')
            root=installed_root/ident
            metadata=json.loads((root/'courses.json').read_text())
            key=next(c for c in metadata['cups'] if c['id']==cup)['courses'][0]
            course=next(c for c in metadata['courses'] if c['id']==key)
            filename=Path(course['source']).stem+'.pcm'
            music=root/'music';music.mkdir(exist_ok=True)
            # A same-named PCM in another pack must not mask a missing song.
            target=menu/filename if case.get('fallback') else music/filename
            sample=case.get('track',110)*100
            target.write_bytes(b'MSU1'+bytes(4)+struct.pack('<hh',sample,-sample)*4410)
        source = legacy / "test.msu" if case.get("legacy") else (
            empty / "none" if case.get("empty") else pack / "cgp.msu")
        if installed_root: source=folder/"installed-music"
        if case.get('menu_custom') or case.get('menu_off'):
            settings = folder/'packs'; settings.mkdir()
            if case.get('menu_custom'):
                custom = folder/'my-title.pcm'
                custom.write_bytes(b'MSU1'+bytes(4)+struct.pack('<hh',2500,-2500)*4410)
                (settings/'loader.cfg').write_text(f'menu_title={custom}\n')
            else:
                (settings/'loader.cfg').write_text('menu_music=0\n')
        env = dict(clean, FZERO_DELUXE_DATA="embedded", FZERO_BS_CARS=str(int(bs)),
                   FZERO_BS_TRACKS=str(int(bs)), FZERO_CGP_CARS=str(packs),
                   FZERO_CGP_REBALANCE="15" if packs else "0",
                   FZERO_RULES="" if case.get("legacy") else "all,cgp-msu,cgp-credits",
                   FZERO_TRACK_PACKS="packs", FZERO_PACKS_DIR=str(installed_root or catalog),
                   FZERO_PACK_LOADER="0" if bs else "1", FZERO_TEST_COURSE=str(case.get("ordinal",0)), SNESRECOMP_SAVE_ROOT="s",
                   SNESRECOMP_INPUT_SCRIPT=inputs, SNESRECOMP_WAV=str(folder / "audio.wav"),
                   SNESRECOMP_WRAM_DUMP=str(folder / "ram.bin"),
                   SNESRECOMP_MSU1='"' + str(source) + '"')
        if not case.get("practice"):
            env["FZERO_CUP"] = case["cup"]
        if case.get("rewind"):
            env.update(FZERO_REWIND_TEST="1", FZERO_VEHICLE_CROSS_STATE="1",
                       FZERO_TEST_SAVE_FRAME="2600" if case.get("practice") else "1500")
        proc = subprocess.run([str(build / "FZeroSNESRecompHeadless.exe"), str(stock), str(frames)],
                              cwd=folder, env=env, capture_output=True, text=True, timeout=150)
        log = proc.stdout + proc.stderr
        (folder / "run.log").write_text(log)
        assert proc.returncode == 0, (case["name"], log[-2000:])
        ram = (folder / "ram.bin").read_bytes()
        assert ram[0x54:0x56] == b"\x02\x03", (case["name"], ram[0x54:0x57].hex())
        assert bool(ram[0x58]) == bool(case.get("practice"))
        assert bool(ram[0x182]) == bool(case.get("fallback")), (case["name"], ram[0x180:0x185].hex())
        with wave.open(str(folder / "audio.wav")) as audio:
            pairs = list(struct.iter_unpack("<hh", audio.readframes(audio.getnframes())))
        if not case.get("empty"):
            title = (2500,-2500) if case.get('menu_custom') else (400,-400)
            if case.get('menu_off'):
                assert pairs.count(title) < 100, 'Menu switch failed to restore SNES audio'
            else:
                assert pairs.count(title) > 1000, (case["name"], "title PCM missing")
        # Centered SNES engine SFX cancel in L-R, exposing the song signature.
        tail = collections.Counter(left - right for left, right in pairs[-100000:])
        if "track" in case:
            assert tail[case["track"] * 200] > 1000, (case["name"], tail.most_common(3))
        if case.get("fallback"):
            assert any(left or right for left, right in pairs[-100000:]), case["name"]
        if case.get("legacy"):
            assert "Conn/Cubear v11 active" in log and 0 < ram[0x183] < 32
        if case.get("rewind"):
            assert "resimulation identical" in log
        results[case["name"]] = dict(ram_music=ram[0x180:0x185].hex(),
                                     dominant_stereo_differences=tail.most_common(3),
                                     rewind=bool(case.get("rewind")))
        print(case["name"], "PASS", flush=True)

    errors = []
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(run, case) for case in cases]
        for future in futures:
            try:
                future.result()
            except Exception as error:
                errors.append(str(error))
    (out / "validation.json").write_text(json.dumps(dict(passed=results, failed=errors), indent=2) + "\n")
    assert not errors, "\n".join(errors)


if __name__ == "__main__":
    main()
