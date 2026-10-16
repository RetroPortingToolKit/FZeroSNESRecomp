"""Run two muted real SDL peers through the shared recomp-net session stack.

Private ROM, installed course packs and a working desktop build are explicit
inputs. Finish/crash cases use owner-authorized pose/health fixtures, never
write lap/time/outcome fields, and do not qualify natural driving. The trace
includes replayed rows: last execution wins at each tick before comparison.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'runtime', 'rom', 'packs', 'settings', 'video', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--scenario', choices=('driving', 'finish', 'crash', 'state', 'reject', 'exit', 'change'), default='driving')
    p.add_argument('--cup', type=int, default=0, help='Select this enabled league through synchronized menu inputs')
    p.add_argument('--aspect', choices=(0, 1, 2), type=int, default=1)
    p.add_argument('--loss', action='store_true')
    p.add_argument('--hd-peer', action='store_true')
    p.add_argument('--rematch', action='store_true', help='Start another race after the finish fixture results')
    p.add_argument('--port', type=int, default=18770)
    a = p.parse_args()
    for name in ('build', 'runtime', 'rom', 'packs', 'settings', 'video', 'out'):
        setattr(a, name, getattr(a, name).resolve())
    a.out.mkdir(parents=True, exist_ok=False)
    frames = dict(driving=1000, finish=2850 if a.rematch else 2280, crash=700, state=1300, reject=300, exit=1000, change=1300)[a.scenario]
    processes, logs = [], []
    started = time.monotonic()
    try:
        for slot in range(2):
            folder = a.out / ('peer%d' % slot)
            folder.mkdir()
            shutil.copy2(a.build / 'FZeroSNESRecomp.exe', folder / 'FZeroSNESRecomp.exe')
            for dll in a.runtime.glob('*.dll'):
                shutil.copy2(dll, folder / dll.name)
            shutil.copytree(a.build / 'assets', folder / 'assets')
            for name in ('config.ini', 'keybinds.ini'):
                shutil.copy2(a.runtime / name, folder / name)
            video = a.video.read_text().replace('HDMode7=0', 'HDMode7=' + str(int(slot == 1 and a.hd_peer)))
            (folder / 'video.ini').write_text(video, newline='\n')
            env = {k: v for k, v in os.environ.items() if not k.startswith(
                ('FZERO_', 'SNESRECOMP_', 'SDL_', 'SNES_NET_', 'SNES_RB', 'RNET_', 'LNG_'))}
            env.update(SDL_AUDIODRIVER='dummy', FZERO_TEST_LOCAL_UI_HIDDEN='1',
                       FZERO_VIDEO_CONFIG=str(folder / 'video.ini'), FZERO_PACKS_DIR=str(a.packs),
                       FZERO_TRACK_PACKS=str(a.settings), FZERO_PACK_LOADER='1', SNESRECOMP_MSU1='off',
                       SNES_NET_ENABLE='1', SNES_NET_SLOT=str(slot), SNES_NET_MODE='rollback',
                       SNES_NET_BIND='127.0.0.1:%d' % (a.port + slot),
                       SNES_NET_PEER='127.0.0.1:%d' % (a.port + 1 - slot),
                       SNES_NET_SESSION_ID='18644', FZERO_NET_ASPECT=str(a.aspect),
                       FZERO_TEST_NET_TRACE=str(folder / 'sim'), SNESRECOMP_AUTOCLOSE_FRAMES=str(frames),
                       SNESRECOMP_FRAME_BMP=str(folder / 'final.bmp'),
                       FZERO_VIEWPORT_SCRIPT=('450:4:3@600x800,550:16:9@1342x1378,650:Fit@1280x720,750:32:9@1600x600,850:21:9@768x576' if slot == 0 else
                                              '450:Fit@1600x600,550:32:9@600x800,650:4:3@1342x1378,750:16:9@1280x720,850:Fit@768x576'))
            inputs = '10-12:32,20-22:8'
            if slot == 0:
                for cup in range(a.cup):
                    inputs += ',%d-%d:32' % (40 + cup * 10, 42 + cup * 10)
                delay = a.cup * 10
                inputs += ',%d-%d:8,%d-%d:32,%d-%d:8' % (40+delay,42+delay,60+delay,62+delay,80+delay,82+delay)
            if a.scenario == 'finish':
                env['FZERO_TEST_ONLINE_FIXTURE'] = 'teleport'
                if slot == 0 and a.rematch:
                    inputs += ',2400-2402:8'
            elif a.scenario == 'crash':
                env['FZERO_TEST_ONLINE_FIXTURE'] = 'crash'
            elif frames > 400:
                inputs += ',400-%d:1' % (frames - 1)
            if a.scenario == 'state' and slot == 0:
                env.update(FZERO_STATE_SAVE_AT='500:4', FZERO_STATE_LOAD_AT='900:4', FZERO_TEST_ONLINE_MENU='1')
            if a.scenario == 'exit' and slot == 0:
                inputs += ',600-602:8,620-622:32,640-642:32,660-662:32,680-682:8'
            if a.scenario == 'change':
                if slot == 0:
                    inputs += ',600-602:8,620-622:32,640-642:32,660-662:8,720-722:8,740-742:8,760-762:8'
                else:
                    inputs += ',720-722:8'
            if a.scenario == 'reject' and slot == 1:
                env['FZERO_NET_ASPECT'] = str((a.aspect + 1) % 3)
            if a.loss:
                env.update(RNET_SIM_LATENCY_MS='30', RNET_SIM_JITTER_MS='15', RNET_SIM_LOSS_PCT='10', RNET_SIM_SEED=str(42 + slot))
            env['SNESRECOMP_INPUT_SCRIPT'] = inputs
            log = (folder / 'game.log').open('w')
            logs.append(log)
            processes.append(subprocess.Popen([str(folder / 'FZeroSNESRecomp.exe'), str(a.rom)],
                                               cwd=folder, env=env, stdout=log, stderr=log))
            time.sleep(.2)
        deadline = time.monotonic() + 100
        while any(proc.poll() is None for proc in processes) and time.monotonic() < deadline:
            time.sleep(.25)
        assert all(proc.poll() is not None for proc in processes), 'peer timeout'
    finally:
        for proc in processes:
            if proc.poll() is None:
                proc.kill()
            proc.wait()
        for log in logs:
            log.close()
    text = [(a.out / ('peer%d/game.log' % i)).read_text(errors='replace') for i in range(2)]
    report = dict(scenario=a.scenario, aspect=a.aspect, loss=a.loss, hd_peer=a.hd_peer,
                  executable_sha256=hashlib.sha256((a.build / 'FZeroSNESRecomp.exe').read_bytes()).hexdigest(),
                  seconds=round(time.monotonic() - started, 3), exits=[proc.returncode for proc in processes])
    if a.scenario == 'reject':
        assert all('mod_set_mismatch' in s for s in text), 'expected exact identity refusal'
        assert all('simulation=0' in s for s in text), 'incompatible peer admitted a simulation frame'
    else:
        assert report['exits'] == [0, 0], report
        assert all('desync=0' in s for s in text), 'desync or missing final report'
        rows, last_ticks = [], []
        for i in range(2):
            found = sorted((a.out / ('peer%d' % i)).glob('sim*.csv'))
            assert found, 'missing authoritative trace'
            data = {}
            for file in found:
                with file.open(newline='') as f:
                    for row in csv.DictReader(f):
                        data[int(row['tick'])] = row
                        last_tick = int(row['tick'])
            rows.append(data)
            last_ticks.append(last_tick)
        # After a synchronized load the online tick returns to the saved
        # epoch; compare the final shared epoch, excluding unconfirmed tails.
        # Use the actual confirmed frontier, never compare predicted tails.
        # Loading creates a new wire epoch rooted at a saved game tick.
        offsets, ends = [], []
        for i in range(2):
            match = re.search(r'final tick=(\d+) confirmed=(\d+)', text[i])
            assert match, 'missing final confirmed frontier'
            tip, confirmed = map(int, match.groups())
            last_tick = last_ticks[i]
            offset = last_tick - (tip - 1)
            offsets.append(offset)
            ends.append(offset + confirmed)
        assert offsets[0] == offsets[1], 'wire epochs differ'
        common = sorted(tick for tick in rows[0].keys() & rows[1].keys()
                        if offsets[0] <= tick <= min(ends))
        assert len(common) > (300 if a.scenario == 'state' else 400), 'insufficient confirmed simulation'
        mismatches = [tick for tick in common if rows[0][tick] != rows[1][tick]]
        assert not mismatches, 'authoritative differences: %s' % mismatches[:10]
        report['matching_ticks'] = len(common)
        shared = [rows[0][tick] for tick in common]
        assert any(row['phase'] == '5' for row in shared), 'did not enter race'
        if a.scenario == 'finish':
            results = [row for row in shared if row['stage'] == '3']
            assert results and all(row['lap1'] == row['lap2'] == '5' for row in results), 'two real five-lap finishes missing'
            if a.rematch:
                assert any(int(row['tick']) > 2403 and row['stage'] == '1' for row in shared), 'rematch countdown missing'
        if a.scenario == 'crash':
            assert any(row['status1'] == '2' and row['status2'] == '3' and row['stage'] == '3' for row in shared), 'DNF/survivor result missing'
        if a.scenario == 'exit':
            assert any(row['phase'] == '6' for row in shared), 'confirmed return to lobby missing'
        if a.scenario == 'change':
            assert any(int(row['tick']) > 700 and row['phase'] == '1' for row in shared), 'change race did not return to machine selection'
            assert any(int(row['tick']) > 1000 and row['stage'] == '2' for row in shared), 'changed race did not start'
        if a.scenario == 'state':
            assert 'online-menu-test:' in text[0], 'real keyboard browser flow incomplete'
            assert all('hard_resync' in s or 'hard resync' in s for s in text), 'synchronized load epoch missing'
            for slot in (0, 4):
                files = [a.out / ('peer%d/saves/netplay/fzero/fzero-local-2p%d.sav' % (i, slot)) for i in range(2)]
                assert all(f.exists() for f in files), 'missing synchronized slot'
                assert files[0].read_bytes() == files[1].read_bytes(), 'slot bytes differ'
    report['passed'] = True
    (a.out / 'report.json').write_text(json.dumps(report, indent=2), newline='\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
