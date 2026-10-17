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
    p.add_argument('--scenario', choices=('selection', 'devices', 'titleback', 'titlestate', 'driving', 'finish', 'crash', 'state', 'reject', 'reject-catalog', 'menuwrap', 'pause', 'change'), default='driving')
    p.add_argument('--machines', default='2,3', help='Two stable vehicle IDs (0..11), selected through real menu controls')
    p.add_argument('--cup', type=int, default=0, help='Select this enabled league through synchronized menu inputs')
    p.add_argument('--course-order', type=int, default=1, help='Zero-based course within the selected league')
    p.add_argument('--aspect', choices=(0, 1, 2), type=int, default=1)
    p.add_argument('--loss', action='store_true')
    p.add_argument('--hd-peer', action='store_true')
    p.add_argument('--rematch', action='store_true', help='Start another race after the finish fixture results')
    p.add_argument('--port', type=int, default=18770)
    p.add_argument('--music0', default='off', help='Peer-local MSU prefix/directory for seat 1')
    p.add_argument('--music1', default='off', help='Peer-local MSU prefix/directory for seat 2')
    p.add_argument('--packs1', type=Path, help='Guest catalog override, including mismatch refusal checks')
    p.add_argument('--force-mispredict', type=int, default=0, help='Shared driver rollback fixture: correct every Nth remote row')
    p.add_argument('--peers-dir', type=Path, help='Persistent QA installation (default: BUILD/qa-peers); reuse for every case')
    a = p.parse_args()
    if a.course_order < 0:
        p.error('course-order must be nonnegative')
    try:
        a.machines = [int(n) for n in a.machines.split(',')]
        assert len(a.machines) == 2 and all(0 <= n < 12 for n in a.machines)
    except (ValueError, AssertionError):
        p.error('--machines needs two IDs in0..11')
    for name in ('build', 'runtime', 'rom', 'packs', 'settings', 'video', 'out'):
        setattr(a, name, getattr(a, name).resolve())
    a.peers_dir = (a.peers_dir or a.build / 'qa-peers').resolve()
    if a.out.is_relative_to(a.peers_dir) or a.peers_dir.is_relative_to(a.out):
        p.error('The persistent peer installation must be outside the case output directory')
    a.out.mkdir(parents=True, exist_ok=False)
    a.peers_dir.mkdir(parents=True, exist_ok=True)
    marker = a.peers_dir / '.fzero-netplay-qa'
    if not marker.exists():
        if any(a.peers_dir.iterdir()):
            p.error('Refusing to reuse an unmarked, nonempty peer installation')
        marker.write_text('F-Zero automated netplay peers; no owner saves\n', newline='\n')
    # One installation per seat, not per scenario: Windows firewall program
    # rules identify a full executable path. Serialize cases to protect these
    # two installations while keeping every result/trace in its fresh folder.
    lock = a.peers_dir / '.running'
    lock_fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    os.write(lock_fd, ('%d %s\n' % (os.getpid(), a.out)).encode())
    os.close(lock_fd)
    frames = dict(selection=550, devices=700, titleback=600, titlestate=1000, driving=1000, finish=2850 if a.rematch else 2280, crash=700, state=1300, reject=300, **{'reject-catalog':300}, menuwrap=1000, pause=1000, change=1300)[a.scenario]
    if a.scenario != 'selection':
        frames += 460  # Real native boot/title and the synchronized VS action.
    processes, logs, launches = [], [], []
    started = time.monotonic()
    try:
        for slot in range(2):
            folder = a.out / ('peer%d' % slot)
            folder.mkdir()
            install = a.peers_dir / ('peer%d' % slot)
            install.mkdir(exist_ok=True)
            saves = install / 'saves/netplay/fzero'
            if saves.exists():
                assert saves.resolve().is_relative_to(a.peers_dir), 'QA save path escaped its installation'
                for old in saves.glob('fzero-local-2p*.sav*'):
                    if old.is_file():
                        old.unlink()
            shutil.copy2(a.build / 'FZeroSNESRecomp.exe', install / 'FZeroSNESRecomp.exe')
            for dll in a.runtime.glob('*.dll'):
                shutil.copy2(dll, install / dll.name)
            shutil.copytree(a.build / 'assets', install / 'assets', dirs_exist_ok=True)
            for name in ('config.ini', 'keybinds.ini'):
                shutil.copy2(a.runtime / name, install / name)
            if a.scenario == 'devices':
                config = install / 'config.ini'
                text = config.read_text()
                text, count = re.subn(r'(?m)^SourceP1\s*=.*$', 'SourceP1 = ' + str(2 if slot == 0 else 1), text)
                assert count == 1, 'source fixture needs an explicit SourceP1'
                config.write_text(text, newline='\n')
            video = a.video.read_text().replace('HDMode7=0', 'HDMode7=' + str(int(slot == 1 and a.hd_peer)))
            (folder / 'video.ini').write_text(video, newline='\n')
            env = {k: v for k, v in os.environ.items() if not k.startswith(
                ('FZERO_', 'SNESRECOMP_', 'SDL_', 'SNES_NET_', 'SNES_RB', 'RNET_', 'LNG_'))}
            env.update(SDL_AUDIODRIVER='dummy', FZERO_TEST_LOCAL_UI_HIDDEN='1',
                       FZERO_VIDEO_CONFIG=str(folder / 'video.ini'), FZERO_PACKS_DIR=str(a.packs1.resolve() if slot == 1 and a.packs1 else a.packs),
                       FZERO_TRACK_PACKS=str(a.settings), FZERO_PACK_LOADER='1', SNESRECOMP_MSU1=(a.music0, a.music1)[slot],
                       SNES_NET_ENABLE='1', SNES_NET_SLOT=str(slot), SNES_NET_MODE='rollback',
                       SNES_NET_BIND='127.0.0.1:%d' % (a.port + slot),
                       SNES_NET_PEER='127.0.0.1:%d' % (a.port + 1 - slot),
                       SNES_NET_SESSION_ID='18644', FZERO_NET_ASPECT=str(a.aspect),
                       FZERO_TEST_NET_TRACE=str(folder / 'sim'), SNESRECOMP_AUTOCLOSE_FRAMES=str(frames),
                       FZERO_TEST_DSP_DUMP=str(folder / 'dsp.bin'),
                       SNESRECOMP_FRAME_BMP=str(folder / 'final.bmp'),
                       FZERO_VIEWPORT_SCRIPT=('450:4:3@600x800,550:16:9@1342x1378,650:Fit@1280x720,750:32:9@1600x600,850:21:9@768x576' if slot == 0 else
                                              '450:Fit@1600x600,550:32:9@600x800,650:4:3@1342x1378,750:16:9@1280x720,850:Fit@768x576'))
            inputs = '60-62:1' if a.scenario == 'titleback' else '' if a.scenario == 'selection' else '20-22:8'
            if a.scenario == 'devices':
                env['FZERO_TEST_ONLINE_SOURCE'] = '1'
            if slot == 0 and a.scenario not in ('selection', 'titleback'):
                for cup in range(a.cup):
                    inputs += ',%d-%d:32' % (40 + cup * 10, 42 + cup * 10)
                delay = a.cup * 10
                inputs += ',%d-%d:8' % (40+delay,42+delay)
                for course in range(a.course_order):
                    inputs += ',%d-%d:32' % (60+delay+course*10,62+delay+course*10)
                confirm = 80+delay+max(0,a.course_order-1)*10
                inputs += ',%d-%d:8' % (confirm,confirm+2)
            if a.scenario == 'finish':
                env['FZERO_TEST_ONLINE_FIXTURE'] = 'teleport'
                if slot == 0 and a.rematch:
                    inputs += ',2400-2402:8'
            elif a.scenario == 'crash':
                env['FZERO_TEST_ONLINE_FIXTURE'] = 'crash'
            elif frames > 860 and a.scenario not in ('selection', 'titleback', 'pause'):
                inputs += ',400-%d:1' % (frames - 461)
            if a.scenario == 'titlestate' and slot == 0:
                env.update(FZERO_STATE_SAVE_AT='335:4', FZERO_STATE_LOAD_AT='350:4')
            if a.scenario == 'state' and slot == 0:
                env.update(FZERO_STATE_SAVE_AT='960:4', FZERO_STATE_LOAD_AT='1360:4', FZERO_TEST_ONLINE_MENU='1')
            if a.scenario == 'menuwrap' and slot == 0:
                inputs += ',600-602:8,620-622:32,640-642:32,660-662:32,680-682:8'
            if a.scenario == 'pause':
                inputs += ',600-602:8,680-682:8,780-782:8' if slot == 0 else ',700-702:8'
            if a.scenario == 'change':
                if slot == 0:
                    inputs += ',600-602:8,620-622:32,640-642:32,660-662:8,720-722:8,740-742:8,760-762:8'
                else:
                    inputs += ',720-722:8'
            if a.scenario == 'reject' and slot == 1:
                env['FZERO_NET_ASPECT'] = str((a.aspect + 1) % 3)
            if a.loss:
                env.update(RNET_SIM_LATENCY_MS='30', RNET_SIM_JITTER_MS='15', RNET_SIM_LOSS_PCT='10', RNET_SIM_SEED=str(42 + slot))
            if a.force_mispredict:
                env['RNET_RB_FORCE_MISPREDICT'] = str(a.force_mispredict)
            # Every case enters the actual native menu. An extra Down is harmless
            # when Records is locked, since VS is the final row in either layout.
            shifted = []
            for part in filter(None, inputs.split(',')):
                span, button = part.split(':')
                first, last = map(int, span.split('-'))
                shifted.append('%d-%d:%s' % (first+460,last+460,button))
            boot = '300-302:32,330-332:32,360-362:32,390-392:8,' if slot == 0 else ''
            roster = [4,6,5,7,0,2,8,9,10,11,1,3]
            before, after = roster.index((0,1)[slot]), roster.index(a.machines[slot])
            moves = []
            delta = (after//4 - before//4) % 3
            if delta:
                moves.append(128 if delta == 1 else 64)
            row_delta = (after%4 - before%4) % 4
            moves += [16] if row_delta == 3 else [32] * row_delta
            navigation = ['%d-%d:%d' % (410+n*16,412+n*16,button) for n,button in enumerate(moves)]
            env['SNESRECOMP_INPUT_SCRIPT'] = (boot + ','.join(navigation + shifted)).rstrip(',')
            log = (folder / 'game.log').open('w')
            logs.append(log)
            launches.append(([str(install / 'FZeroSNESRecomp.exe'), str(a.rom)],
                             dict(cwd=install, env=env, stdout=log, stderr=log)))
        # Prepare both installations before starting either boot handshake.
        # Copying assets for peer1 after peer0 starts can consume its deadline.
        for command, options in launches:
            processes.append(subprocess.Popen(command, **options))
            time.sleep(.2)
        # Lifecycle runs can be much longer than selection. This is a logic
        # gate, not a sustained-performance qualification on a busy desktop.
        deadline = time.monotonic() + max(100, frames / 15 + 30)
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
        try:
            for slot in range(2):
                saves = a.peers_dir / ('peer%d/saves' % slot)
                if saves.exists():
                    shutil.copytree(saves, a.out / ('peer%d/saves' % slot))
        finally:
            lock.unlink()
    text = [(a.out / ('peer%d/game.log' % i)).read_text(errors='replace') for i in range(2)]
    report = dict(scenario=a.scenario, machines=a.machines, cup=a.cup, course_order=a.course_order,
                  music=[a.music0, a.music1],
                  aspect=a.aspect, loss=a.loss, hd_peer=a.hd_peer,
                  force_mispredict=a.force_mispredict,
                  peer_installation=str(a.peers_dir),
                  executable_sha256=hashlib.sha256((a.build / 'FZeroSNESRecomp.exe').read_bytes()).hexdigest(),
                  seconds=round(time.monotonic() - started, 3), exits=[proc.returncode for proc in processes])
    if a.scenario in ('reject', 'reject-catalog'):
        assert all('mod_set_mismatch' in s for s in text), 'expected exact identity refusal'
        assert all('simulation=0' in s for s in text), 'incompatible peer admitted a simulation frame'
    else:
        assert report['exits'] == [0, 0], report
        assert all('desync=0' in s for s in text), 'desync or missing final report'
        assert all('starting UNVERIFIED' not in s for s in text), 'startup state verification timed out'
        if a.force_mispredict:
            assert all(re.search(r'final tick=\d+ confirmed=\d+ episodes=[1-9]\d* resim=[1-9]\d*', s) for s in text), 'rollback fixture did not replay ticks'
            if a.scenario == 'pause':
                for slot, music in enumerate((a.music0, a.music1)):
                    if music != 'off':
                        assert len(re.findall(r'\[versus-msu\] command=6 ', text[slot])) == 1, 'rollback restarted the live race soundtrack'
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
        assert any(row['phase'] == '0' for row in rows[0].values()), 'native title was skipped'
        if a.scenario == 'titleback':
            assert any(row['phase'] == '1' for row in shared), 'did not enter selector'
            assert shared[-1]['phase'] == '0', 'Back did not restore title'
        elif a.scenario == 'selection':
            assert shared[-1]['phase'] == '1', 'did not remain in independent selection'
            assert [int(shared[-1]['p1']), int(shared[-1]['p2'])] == a.machines, 'independent choices missing'
        elif a.scenario == 'devices':
            assert shared[-1]['phase'] == '1', 'one controller confirmed both participants'
            assert [int(shared[-1]['p1']), int(shared[-1]['p2'])] == [2, 1], 'controller reached keyboard-only client'
            assert all('online-source-test: one controller moves/confirms only its configured client PASS' in t for t in text), 'actual SDL device test incomplete'
        else:
            assert any(row['phase'] == '5' for row in shared), 'did not enter race'
            assert all([int(row['p1']), int(row['p2'])] == a.machines for row in shared if row['phase'] == '5'), 'race machine identities differ'
        if a.scenario == 'finish':
            results = [row for row in shared if row['stage'] == '3']
            assert results and all(row['lap1'] == row['lap2'] == '5' for row in results), 'two real five-lap finishes missing'
            if a.rematch:
                assert any(int(row['tick']) > 2803 and row['stage'] == '1' for row in shared), 'rematch countdown missing'
        if a.scenario == 'crash':
            assert any(row['status1'] == '2' and row['status2'] == '3' and row['stage'] == '3' for row in shared), 'DNF/survivor result missing'
        if a.scenario == 'menuwrap':
            assert shared[-1]['phase'] == '5' and shared[-1]['pause'] == '0', 'menu wrap did not continue race'
            assert not any(row['phase'] == '6' for row in shared), 'in-game menu unexpectedly exited to lobby'
        if a.scenario == 'pause':
            pauses = [row for row in shared if row['pause'] != '0']
            assert len(pauses) >= 100 and shared[-1]['pause'] == '0', 'pause/resume not exercised'
            assert shared[-1]['stage'] == '2', 'race did not continue after pause'
            for slot in range(2):
                dsp = (a.out / ('peer%d/dsp.bin' % slot)).read_bytes()
                assert [dsp[0x0c], dsp[0x1c]] == [0x54, 0x54], 'music master did not survive pause'
                assert sum(dsp[i*16+8] > 0 for i in range(6)) >= 2, 'music voices silent after resume'
        if a.scenario == 'change':
            assert any(int(row['tick']) > 1100 and row['phase'] == '1' for row in shared), 'change race did not return to machine selection'
            assert any(int(row['tick']) > 1400 and row['stage'] == '2' for row in shared), 'changed race did not start'
        if a.scenario in ('state', 'titlestate'):
            if a.scenario == 'state':
                assert 'online-menu-test:' in text[0], 'real keyboard browser flow incomplete'
            assert all('hard_resync' in s or 'hard resync' in s for s in text), 'synchronized load epoch missing'
            for slot in ((0, 4) if a.scenario == 'state' else (4,)):
                files = [a.out / ('peer%d/saves/netplay/fzero/fzero-local-2p%d.sav' % (i, slot)) for i in range(2)]
                assert all(f.exists() for f in files), 'missing synchronized slot'
                assert files[0].read_bytes() == files[1].read_bytes(), 'slot bytes differ'
    report['passed'] = True
    (a.out / 'report.json').write_text(json.dumps(report, indent=2), newline='\n')
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
