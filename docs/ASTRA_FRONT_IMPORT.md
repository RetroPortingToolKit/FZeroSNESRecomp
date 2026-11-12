# Astra Front import

Reviewed source: the owner's `F-ZERO Astra Front.sfc`, unheadered SHA-256
`76e03d7ef6d1c5e63da554f00e48843f589ec1407a4aaf80ed46af5df4f988c0`.
This revision publishes **two cups / ten courses**. Its 65 internal resource
slots include unused CGP content; those other 55 slots are not imported.
The published web release is not used to guess this local revision's contents.

| Cup | Course | Source slot | Native SNES theme |
| --- | --- | ---: | --- |
| Astra | U Zero I | 55 | Silence |
| Astra | Candany | 58 | White Land I |
| Astra | U Zero II | 57 | Silence |
| Astra | U Zero III | 59 | Silence |
| Astra | Vulcanom | 60 | Fire Field |
| Front | U Zero IV | 61 | Silence |
| Front | Brutal Wind I | 56 | Death Wind |
| Front | Moon | 63 | Silence |
| Front | U Zero V | 62 | Silence |
| Front | Death City | 64 | Mute City |

The GP permutation is at CPU `$1088C9`; cup labels are native small-font
strings addressed by `$10878D`. Intro names, source ordering and music are
independently decoded by the metadata auditor. The source's SPC upload blocks
and native song table are identical to stock; no custom SPC assets are needed.

## Course mechanics and omissions

The owner confirms the same author's CGP behavior applies. Both cups declare
the shared grip/up-magnet capabilities. In particular, downpull without the
separate MAGNET property grips without CGP magnet damage; unrelated packs
retain their native semantics. Required landing behavior uses the same shared
adapter. Vehicle stats, boost profiles, difficulty, collision options and MSU
music remain separate choices; the donor's global code is not installed.

The donor retains Rainbow Road gravity/illusion code targeting ordinal 50,
outside these two cups. That dormant rule is not attached to Astra courses.
Its title tiles and palette are byte-identical to Community Grand Prix.

`tools/import_astra_front.py` verifies the exact source revision and writes a
resource-only IPS. `tools/pack_course_resources.py` retains only the selected
table entries and reachable course resources over a clean stock base. The
expanded image's other bytes are zero. Two small metadata-locator fragments
are retained solely for offline auditing, never executed by the port.
The full donor ROM, unused CGP venues (including the retired Volcania art),
vehicles and music recordings are not bundled.

The C extractor hashes every selected resource before and after packing;
those hashes and the metadata must match exactly. The ROM-free provenance is
in `assets/track-packs/astra-front-import.txt`. Sparse layouts are qualified
using their manifest's source indices, not every unused internal slot.
This packer is for reviewed FZEdit layouts; it is not a generic ASM converter.

## Validation (2026-09-25)

- All ten courses loaded on retail and expanded engines: twenty race-start,
  actual SPC-song and scoped-mechanics checks passed.
- Inspected a capture of each course for road, sky, HUD, car and minimap
  corruption. These are early-race visual checks, not full driven laps.
- Both cups on both engines passed scripted five-race completion, records
  overview and all five details, page navigation and battery-save reload.
  Expanded mode also passed vehicle-record isolation and return-to-car checks.
  Browsing did not change stored times. The fixture seeds timing values and
  crosses the native finish line; it does not substitute a full playthrough.
- Metadata tests and focused course/parser/save/mod unit tests passed.

Private evidence: `captures/astra-01` (courses),
`captures/astra-records-02` (records). The reusable smoke harness is
`tests/validate_imported_pack.py`; all ROMs, saves and decoded captures stay
outside version control.

## Intro font repair (2026-09-25)

The initial early-race checks missed the course intro: U Zero rendered its Z
as a stock Japanese glyph. Astra patches `$00D146` to look up top/bottom OBJ
tiles through `$108095` / `$108150`, while preserving the old name character
codes. Its `$0F8000` atlas has a different tile order. Decoding the name alone
therefore did not reproduce its artwork.

`tools/audit_intro_font.py` compares every used letter with stock. Only R and Z
differ: R maps to donor tiles `$69/$96` and Z to `$B4/$B5`. The layout imports
their two 16-byte 2bpp halves. Both engines upload only letters used by the
current course, after the ordinary font atlas loads at `$0088D3`; the next
course's native load restores the original tiles. No donor font code or car/HUD
atlas is imported. These presentation resources are outside the course hash.

`tests/validate_intro_glyphs.py` checks the actual intro OAM and VRAM for all ten
courses on both engines, including ordinary letters and unused-override
controls. The corrected U Zero I intro was visually inspected on both engines.
Evidence: `captures/astra-intro-validation-02` and `captures/astra-intro-fixed`.
All ten normalized gameplay/record hashes are unchanged from the initial import.
