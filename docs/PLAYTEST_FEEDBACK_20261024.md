# VS feedback: October 24, 2026

Central issue: `beads-8wg.5.107`. Baseline `faab2cf`, branch
`feat/fzero-local-2p`. The owner explicitly selected all twelve CGP versions
for VS; single-player choices remain independent.

## Changes and findings

| Report | Result |
| --- | --- |
| Required online rules appear as a confusing checkbox | Hidden through the existing provider visibility contract. Always enabled, cannot be disabled, and still part of mandatory build/rules admission. Course catalog entries remain visible. |
| V in VS MODE is incomplete | Its six-column resampling skipped the source's center column and taper. Preserve all eight source columns, with the native title shading and outline. |
| Original four cars use retail artwork in VS | VS previously explicitly chose retail profiles. Bind all twelve to the existing authored CGP profiles, including handling, cards, body art, exhaust, energy drain and collisions. |
| POWER and three bottom boost symbols | Decode the authored CGP lightning/gauge tiles. Use one available/burning/spent boost symbol beside the upper gauge. Native retail oracle rendering is retained separately. |
| Host input lag | The shared lobby default was six frames, intentionally delaying local input too. F-Zero initializes its lobby to the supported two-frame minimum; the host can increase it. Shared engine/UI code is unchanged. |
| Unspecified sound at race start | Menu first-entry/Change Race, SPC/MSU identity and native race audio checks pass. The tester's particular sound has not been reproduced or identified; retain the existing listening follow-up in `beads-iiw8`, rather than claim a sound fix. |

Profiles are immutable session inputs, not mutable single-player settings.
Rules identity is 8. Snapshots reject a different original-car profile binding;
their canonical hash includes it. No old-state migration is required. Native
SRAM and single-player progress are untouched.

## Validation

All automated launches were muted. Private evidence is in `captures/feedback24`.

- Complete Windows build and all 18 CTests pass with the intended bundled
  catalog supplied through `FZERO_PACKS_DIR`.
- `validate_machine_profiles.py`: all twelve profiles pass native CPU handling,
  movement, boost/refill and exhaust comparisons. All 144 ordered pairs pass
  own-profile collision checks and save/replay/rematch checks. The original
  retail CPU/render oracles still pass with the retail binding.
- All twelve VS information-card silhouettes and frame colors match actual
  CGP single-player captures. Exhaust checks cover every identity, thirteen
  steering/shoulder poses, both hover offsets and 256 ignition/color ticks.
- A real two-peer driving run, Blue Falcon and Golden Fox, native/HD displays
  and different window shapes: 1,457 matching ticks, zero desyncs, boot digest
  agreed, delay 2. Two natural rollback episodes resimulated two ticks.
  This is same-build Windows evidence, not remote/Linux qualification.
- `validate_local_versus.py`: current-build snapshots at bootstrap/countdown/
  racing, fresh-process restore, rewind, viewport changes, diagnostic replay,
  divergent replay detection and invalid snapshot rejection all pass.
- `validate_online_menu_audio.py`: first entry and live Change Race, with SPC
  and synthetic MSU, retain music and identical canonical WRAM/ARAM/DSP.
- All thirteen `validate_versus_audio.py` cases pass across stock, imported and
  BS courses, ignored multiplayer Start inputs, and native SP pause/resume.
- Inspected title, original CGP selectors, 4:3 race HUD and actual widescreen
  peer frames. The V, lightning emblem and single upper boost are intact.

The initial Mods CTest ran without installed packs and failed its expected
three-title assertion. It passed with the existing bundled-catalog fixture;
no title-loader behavior was changed. One private menu-only headless probe
deliberately ended in a race and returned its expected qualification failure
(8); that harness requires returning to the selector. Real peer driving and
the lifecycle validator separately qualify the actual race.

Only selected logs, reports, images and two short muted audio captures are
retained. Generated donor ROMs, duplicate assets and disposable test saves are
removed from task scratch. The normal development build remains available.
