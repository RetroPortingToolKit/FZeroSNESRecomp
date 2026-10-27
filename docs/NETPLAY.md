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
three-column selector. All twelve use their authored CGP artwork, handling,
energy boost and exhaust, including the CGP versions of the original four cars.
Each actor binds its own immutable profile, including mixed-cohort collisions;
selecting another ship never swaps the shared guest cartridge.

Four shared regular NPC bumpers follow each course's authored CPU lines,
including imported opponent data. Both humans can hit them, and both views
render the same field. Traffic is automatic in VS and never contributes laps
or places to the human result. Red explosive bumpers and ranked CPU rivals
are not implemented.

VS rules are mandatory and hidden in Lobby Mods. The shared course catalog is
still listed there and checked before play. Both players always use the same
VS roster, regardless of their single-player car switches. The HUD uses CGP's
lightning emblem and one boost indicator beside the energy bar.

F-Zero lobbies default to two frames of input delay (about 33 ms at 60 FPS),
down from six frames (about 100 ms). This delay applies to the host too. The
host can raise it in the lobby for a less reliable connection. Rollback remains
required; two frames is not a claim of zero total input or network latency.

Multiplayer races cannot be paused. Start opens a live menu while the race
continues; its owner sends neutral driving input. Focus loss and hotkeys never
stop the simulation. Restart and Change Race use shared transitions. The
in-game menus have no lobby exit option; closing the game returns through the
existing launcher flow. Finishing cars
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
and captured PCM on stock, imported and BS courses, including multiplayer
Start inputs and native single-player pause/resume. Live multiplayer menus
keep the song playing and never send the native crash command. Engine noise
or a crash tail alone does not pass the check. The actual-peer `pause`
scenario exercises Start from both seats and checks continuing music.

MSU-1 also works in local and online VS through the existing Audio setting.
The existing course-file and menu/event music resolver is reused; missing or
empty recordings restore SNES music. Audio files and the music preference are
local to each computer and do not have to match. The complete native SPC/DSP
simulation still runs identically; only the output mixer replaces music voices,
retaining native engine/effect voices and their original FIR echo. Rollback
does not restart the soundtrack or advance its presentation echo. Live menus
keep music playing, and actual loads/rewinds restart the restored song rather than
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

Rules9 (`FZVS009`) adds the canonical bumper field to the twelve-car race;
online protocol3 still carries stable machine identities and independent
selector slide/spin state. The snapshot namespaces are
`fzero-local-2p-v2` and `fzero-online-v3`; earlier VS snapshots are intentionally
incompatible. Build identity includes the reviewed generated mechanics/exhaust
tables and verified vehicle art manifests/payloads, alongside the existing
source/dependency fingerprint. This does not relax exact-build admission.

`fzero_traffic.c` reuses the cartridge's checkpoint lookup, CPU guidance,
steering and physics routines through the engine's portable callback CPU.
Calls are bounded and isolated from the live CPU, PPU, APU, host clock and
devices. The resource-only seed retains course/class/lane tables; multiplayer
owns a persistent four-car field and contacts. Spawn positions are screened
against both humans' maximum view frustums; cars keep driving when distant
and are replaced only if native physics removes them. Imported terrain uses the
same course-property adapter as the human actors. Native generic NPC art and
palettes are captured once at the starting grid. There is one shared traffic
image, not a game instance per player. Its private RAM, reset seed and contact
latches are serialized and hashed; presentation poses are derived after load.

## Validation

`tests/validate_traffic.py --help` boots real original/imported courses, checks
current-build disk save/load and rewind replay, then tests the moving native
field, collisions from both humans, both projected views, atomic invalid-state
rejection and restart equivalence. It requires a private USA ROM and installed
packs. `tests/validate_live_menu.py` also observes painted bumper pixels in both
actual SDL views, using prepared position/motion/health encounters to exercise
both human impacts. Those encounters do not qualify natural driving. Peer
traces include the active traffic count and first NPC's
position; the canonical comparison covers the complete field.

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
