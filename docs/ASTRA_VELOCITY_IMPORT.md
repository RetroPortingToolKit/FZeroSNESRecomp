# Astra Velocity P1

Worthy / MF9_05's twenty courses ship as a default bundled pack alongside
CGP, Bower and MAX. The collection has 85 imported course versions in 17 cups,
plus the original 15 courses in three cups.

| Cup | Courses in authored order |
| --- | --- |
| Eclipse | Big Blue, Death Wind, Mars, Red Canyon, Huckmine |
| Oblivion | Candany, Mute City I, Brutal Wind I, Marine City, Fire Field |
| Vector | Zone Zero, Sand Ocean, Metal Field, Vulcanom, Death City |
| Apex | Mute City II, White Woods, Brutal Wind II, Synobazz, Illusion |

The source intro for Mute City II contains two spaces before II; the display
string preserves them. IDs normalize whitespace. Names, order and SNES music
come from the donor's loaders and tables. Separate MSU recordings were not
provided; this pack uses its authored SNES music. CGP recordings remain assigned
to CGP.

All courses require the existing CGP grip-magnet and up-magnet adapters.
Illusion alone requires `fall-down`: off-road void starts the falling death
animation, wall spin and wall bounce are bypassed, and gravity stays normal.
CGP `rainbow-road` retains its separate increased-gravity behavior. These are
mandatory course rules; they do not enable global vehicle or race tuning.
Both the guest adapter and native multiplayer actors implement the wall bypass.

The reviewed patch SHA-256 is
`5d55032be261f8d9239d4b0ab9e2202476608642f9ac5531a12c6adf4ed5c184`.
Its patched source SHA-256 is
`795548cb23e48731ca5c34aed551165515e195f03db249e710fa0abe241a54f8`.
`tools/import_astra_velocity.py` checks those revisions, the data consumers,
cup labels/order, magnet instructions, Illusion hooks and unchanged gravity.
It retains only reachable course resources. The runtime loads reconstructed
single-course editor ZIPs, with no donor executable patches or full ROM.

Reproduce the resource import with a private original USA ROM:

```powershell
python tools/import_astra_velocity.py --stock path/to/fzero.sfc `
  --patch 'path/to/F-Zero Astra Velocity (CGP) P1.ips' `
  --exporter build/FZeroExportCourses.exe `
  --inspector build/FZeroInspectPacks.exe `
  --packs captures/astra-velocity/packs/astra-velocity
```

The established exporter, release packager and frozen converter include the
new profile. Build/release ownership metadata marks it as bundled; stable IDs
and per-course records remain separate from similarly named courses in other
packs. The other packs' resource and record hashes are unchanged.

Qualification uses the existing imported-pack, driving and scoped-mechanics
checks. Resource reconstruction compares every complete serialized course,
including presentation fields. Records completion fixtures cross the finish
line after seeding final-lap state; they do not certify a normally driven GP.
Driving fixtures position a car on an approach, then sample actual terrain
under ordinary input; they do not set the asserted outcomes. Human validation
of complete races and authored difficulty remains a separate final gate.

The 0.16-based import candidate passed 21 CTest checks, 40 course/music/mechanics runs,
eight completed-cup records/persistence fixtures, 88 terrain/driving checks
across all twenty courses, and eighteen scoped-mechanics/isolation cases.
Thirty-three terrain approaches were unavailable and remain explicitly
unverified; these are not passes. Screenshots cover both vehicle engines and
the exercised interactions. The staged SDL build also passed Fit resizing,
8x HD rendering, current-build snapshot save/load and a four-player Illusion
start. Complete human races, difficulty and final release acceptance remain
pending.

Driving fixtures initialize both the checkpoint index and its cached gates.
Dash retries use other safe authored approaches when a plate cannot be entered
in its checkpoint direction at the first location. Native background contact
can damage and redirect a grounded car onto the road; Illusion instead requires
the falling-death response. The checks distinguish those contracts.

The 0.20.0-rc.1 prerelease is based on released 0.19.1. Its `fall-down`
capability uses bit 16; bit 8 remains the independent soft-horizon-fog
capability. Gameplay hashes include fall-down and exclude the presentation
flag, preserving existing CGP record identities. The current-base follow-up
rebuilds all 95 course sources/caches and repeats course/music, scoped rules
and focused fall/dash checks. Human testing is non-exhaustive; this prerelease
is intended for wider validation.

Fresh bundles include ready-to-use caches for all 85 imported course versions.
Extract into a fresh directory and preserve native saves and settings when updating.
