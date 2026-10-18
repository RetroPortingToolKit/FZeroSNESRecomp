# F-Zero versus netplay

Open recomp-ui, choose **Netplay**, then use the existing **LAN / Direct IP**
or **Online Netplay** flow. This uses the same shared lobby, account, ICE,
transport and rollback implementation as the other recomp titles. LAN rooms
are discoverable; Direct IP uses the host's waiting-room address and port.
Online rooms use the shared service and its existing account flow.

Two humans race in one authoritative VS simulation. Each computer displays its
own participant's complete viewport. The host chooses **Viewport** in lobby
settings: **4:3**, **16:9**, or **21:9**, default **16:9**. The choice is fixed
for the match and shown read-only to the guest. Window size, HD rendering and
presentation cadence remain local. Local multiplayer keeps top/bottom views;
ordinary single-player still opens at the original title menu with VS MODE.

Starting the match opens the original F-Zero title screen on every client.
Player 1 navigates to **VS MODE** with ordinary game controls. Each person then
gets an independent full-screen machine selector, with the original rotating
car, information panel, numerical stats and acceleration graph. The other
participant's current car/readiness appears below it. Selection does not divide
the viewport; the same pane renderer accepts a local seat or an online seat,
so increasing the online field does not require smaller selection panes.

Each person selects and confirms their own machine with ordinary game controls.
Each online instance honors its configured Player 1 input source. A keyboard-only
instance does not open or read gamepads, including newly connected devices;
a gamepad instance does not merge keyboard gameplay input. This permits two
instances on one computer to use one controller and one keyboard independently.
After both are ready, the host chooses league and course. The same enabled
course catalog as single-player is available, including custom imports. Both
participants need the same build and enabled course packs. Automatic transfer
of F-Zero imports is not implemented; an exact catalog mismatch refuses the
match before its first simulation tick. The catalog identity includes stable
IDs, cup/course names and order as well as decoded track data: independently
renamed imports can therefore be rejected even when their layouts match.
Renaming only an enclosing installation folder does not change the identity.
Personal single-player modifications
are retained. VS offers all twelve existing machine identities in the original
three-column selector. The four original cars retain retail handling; the
eight added ships use their authored CGP handling, energy boost and exhaust.
Each actor binds its own immutable profile, including mixed-cohort collisions;
selecting another ship never swaps the shared guest cartridge.

Multiplayer races cannot be paused. Start, focus loss and in-race menu hotkeys do not stop the simulation. The host navigates rematch or change race only after results. The in-game menus have no lobby
exit option; closing the game returns through the existing launcher flow. Finishing cars
coast and fade using the local VS presentation. A crash uses the native
explosion; when only one non-crashed participant remains, that participant wins.
Results distinguish FINISHED, CRASHED OUT and SURVIVED. Non-finish times are
dashes. The completion predicate counts the remaining field for later expansion;
this frontend currently supports two seats.

The save-state browser is available after the race only and synchronizes both peers on the same confirmed frame.
The host selects, saves, loads and closes it; the guest mirrors the menu.
Quick saves/loads also use shared state synchronization and are unavailable during a race. Online slots live under
`saves/netplay/fzero`; offline single-player and local VS slots remain separate.
Rewind and diagnostic local replay remain offline features. Rewind is disabled
during netplay. Native course loading is a bounded deterministic transition,
followed by the same arrival glide and countdown used in local VS.

The shared SPC timeline switches from the original pre-race program to the
uploaded course song at GO, as single-player does. The host audio latch retains
this across rematches and restored races. `tests/validate_versus_audio.py`
checks real music-voice envelopes over a one-second window, native master volume
and captured PCM on stock, imported and BS courses, including ignored multiplayer Start inputs and native single-player pause/resume.
As in single-player, pause keeps the song playing and clears vehicle effects;
it never sends the native crash command. Engine noise or a crash tail alone
does not pass the check. The actual-peer `pause` scenario exercises pausing
from both seats and verifies music after the host resumes.

MSU-1 also works in local and online VS through the existing Audio setting.
The existing course-file and menu/event music resolver is reused; missing or
empty recordings restore SNES music. Audio files and the music preference are
local to each computer and do not have to match. The complete native SPC/DSP
simulation still runs identically; only the output mixer replaces music voices,
retaining native engine/effect voices and their original FIR echo. Rollback
does not restart the soundtrack or advance its presentation echo. Pause keeps
music playing, and actual loads/rewinds restart the restored song rather than
restoring an exact PCM sample position, matching single-player's behavior.
This does not relax build or course-catalog checks or transfer any files.

## State and compatibility

`fzero_online.c` reduces admitted pad words into canonical setup/menu state.
Neither device sampling nor the local participant's camera enters that state.
Existing engine snapshot extras store it together with the complete local VS
session and native execution state. The shared digest extension includes both
the online state and complete canonical race image; rollback replays the same
runtime entry point.

Before frame zero, the shared startup-profile exchange sends actual CPU features,
architecture and available/qualified backends. F-Zero currently advertises only
the validated portable scalar race profile. The agreed profile and capability
transcript join the full build, rules, course catalog and aspect identity.
Selection happens once and stays fixed for the match. Exact build checks remain;
no cross-build or cross-platform compatibility is implied.

The launcher uses a short discovery version within the shared 32-byte LAN field.
This does not replace the full pre-race identity check. New UI is either existing
recomp-ui lobby UI or the same native F-Zero assets used by local VS.

Rules6 (`FZVS006`) and online protocol3 carry stable twelve-car identities and
independent selector slide/spin state. The snapshot namespaces are
`fzero-local-2p-v2` and `fzero-online-v3`; earlier VS snapshots are intentionally
incompatible. Build identity includes the reviewed generated mechanics/exhaust
tables and verified vehicle art manifests/payloads, alongside the existing
source/dependency fingerprint. This does not relax exact-build admission.

## Validation

`tests/validate_selector.py` compares all four VS information panels and
unselected previews pixel for pixel with real single-player screens. The stock
CPU oracle also verifies all 24 spin angles and their six-tick timing.

`tests/validate_machine_profiles.py --help` builds private fixtures from the
shipped art IPS and independently assembled author routines, then checks the
eight added ships' handling/energy boosts against native CPU execution. It
also covers all 144 ordered machine pairs, own-profile collision damage and
speed response, atomic save/replay/rematch, native body poses, complete spin,
sparks/electricity and exhaust. All twelve exhaust layouts,13 steering/shoulder
poses, both hover offsets and the full ignition/color sequence are compared
against native OAM/CGRAM. Added-car information values, acceleration graph
silhouettes and frame colors are compared with actual single-player screens.
Private ROMs, profile images and captured artwork remain outside Git.

Run `tests/validate_netplay.py --help` for explicit private ROM, runtime DLL,
installed pack, settings, video configuration and fresh output paths. It launches
two actual muted SDL peers and compares their canonical completed states only
through the confirmed frontier. Replayed rows replace previous executions of
the same game tick. After a synchronized load it checks the new wire epoch.
Peer executables stay at `BUILD/qa-peers/peer0` and `peer1` across cases. Approve
those two paths once in Windows Firewall. Cases run sequentially under an
exclusive installation lock; fresh output folders retain separate traces,
images and copied test saves. `--peers-dir` selects another persistent location.
Do not create a new executable path for each matrix cell: that prompts for
firewall permission again. The runner never changes firewall policy.

Scenarios cover ordinary driving, host aspect mismatch rejection before tick
zero, synchronized save/load and real keyboard/pad browser controls, finish and
rematch, crash/survivor results, change race and pause-menu wrapping. `--loss` adds 10%
loss, 30ms latency and 15ms jitter. `--hd-peer` gives the second peer HD rendering.
Both peers resize through different window shapes; local aspect preferences
cannot change the host's negotiated viewport. `--cup` selects an enabled league
through actual synchronized menu inputs, including imported leagues.

Finish/crash fixtures force only pose, motion or health. The real core awards
laps, times and outcomes. Their explicit QA mode enters compatibility identity;
they are never enabled by the ordinary launcher. They do not certify natural
driving. The normal local VS stock CPU/effect oracles and catalog lifecycle
checks continue to apply to the same authoritative race core.

Windows same-build two-process tests and real recomp-ui LAN/Direct IP launch
are automated. Owner physical controller QA and real remote Internet/ICE play
remain final manual checks. Linux and cross-build netplay are not qualified.
