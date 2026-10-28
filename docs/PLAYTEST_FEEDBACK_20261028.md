# Multiplayer bumper follow-up

Owner report: bumpers leave the course, ignore jumps/upward magnets, cluster,
appear mainly in the first/final laps, lack explosives and use purple art.
Reproduction course: CGP Baron, Sulfur Swamp, runtime course 59.
Tracking: `beads-8wg.5.112`, follow-up to `beads-8wg.5.109`.

| Feedback | Correction and evidence |
| --- | --- |
| Terrain ignored | The original callback loop omitted the native pre-physics surface pass. Sample the full-course tile and normalize its properties before physics; use the existing imported-course altitude adapter for landings/upward magnets. The previous implementation fails the actual-course surface regression. |
| Driving in void | Remove grounded cars on lethal ground; airborne cars remain until they actually land in void. Both paths pass on Sulfur Swamp tile F8. |
| Missing jumps/magnets | Sulfur jump BA at 6916,1412 enters airborne state with height 0880. Airborne upward-magnet B6 at 6020,1436 changes falling velocity FF00 to FF75. These are position-only fixtures on real course tiles. |
| Purple regular cars | Restore native C41 palette attribute 4 for brown, 6 for red. Retained image captures show both native variants. |
| Sparse later laps | Replenish passed cars outside both humans' views and beyond their exclusion radius. Ahead cars keep driving. A five-circuit position-only route sweep observes nearby traffic on every circuit; it does not qualify natural driving or human lap counting. |
| Clustering | Increase minimum separation between accepted spawn candidates from 80 to 360 world units; reject unsafe ground/jump/magnet spawns. Ordinary simulation can still bring cars together. |
| Missing explosives | Every fourth accepted spawn is a native red explosive. Verify damage, stun, height, flash, sound and unchanged speed for both humans touching the same bumper in one tick; consume once without altering terrain. |

Tests now receive the actual VS world, including imported terrain metadata,
instead of rebuilding an imported course as a retail terrain fixture.
Rules 10 and exact-build netplay checks reject older simulation snapshots.
The qualified engine/native runtime from 0.11.2 remains unchanged.

Private logs, course fixtures and pictures are retained in
`captures/bumper-20261028`. Broader validation and delivery are recorded there
and in the owning issue.

Validation: all 18 Windows CTest checks pass. Original courses 0/14 and
imported courses 15/59/65 pass traffic, terrain where present, five-circuit
population, current-build save/load/rewind, restart, contact and artwork
checks. Sulfur explicitly requires actual jump and upward-magnet coverage;
a course-wide capability flag alone does not establish that those tiles occur.
The actual Sulfur fixture also passes the portable Linux traffic target.
Muted windowed two-SDL-device live-menu/impact/resize checks pass. Two real
Sulfur netplay peers, with HD enabled on one, agree for 1,456 confirmed ticks.
ROM validation, GUI subsystem and fresh/saved feature-default checks pass.
