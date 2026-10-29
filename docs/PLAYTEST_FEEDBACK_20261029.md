# Multiplayer tester feedback, October 29

Tracking: `beads-8wg.5.113` (lettering) and `beads-iiw8` (remaining feedback).
Source: Worthy/Fennor screenshots `Discord_b43skvKt66.png` and
`Discord_Z1zrGSHnmG.png` supplied by the owner.

## Course lettering

The menu renderer was indexing the original tall cartridge alphabet correctly.
The source artwork itself makes Z look like 2 and K/X look like H. Q is present
and correct. This is a presentation problem, not a changed course name or an
importer decoding Z as a digit.

Keep the other 23 tall letters unchanged. Derive clear K/X/Z silhouettes from
the cartridge's complete records alphabet at `$0E:DEA0`, double its seven rows
inside the 8x16 letter cell, and use the tall I's native outline/fill colors on
each scanline. The four menu palettes still control selection/dimming. No font
artwork is embedded, and raw tile storage remains unchanged for information
cards, borders and other graphics. Both local and online course pickers use the
shared text renderer. Custom machine headings use the same resolved alphabet.

Validation and retained private evidence: `captures/feedback-20261029`.
Windows desktop/headless builds pass with the existing qualified native engine
and generated modules. All three focused CTests pass (6.00 seconds): native
actor/artwork oracle, online menu reducer and local controls. The oracle checks
the rendered alphabet against independently read cartridge bitplanes at 1x/2x
with all four palettes. The course-label/alphabet preview was inspected after
rendering with the shared production UI code. This does not qualify audio,
traffic behavior or a new multiplayer session.

## Native behavior reused in multiplayer

The owner requested native SNES sounds and existing single-player behavior;
skip requests without an existing implementation. These changes keep the
qualified native engine, APU and generated modules. The delivered 0.11.3
archives have not been replaced.

| Report | Status |
| --- | --- |
| Roughly the first second of the starting sound is wrong | Corrected a concrete arrival/rev mismatch. Capture the native arrival substate, use its music program 3 and engine command 3, then start rev program 1 at the original transition. Restart follows the same sequence. Exact subjective listening report still needs reporter confirmation. |
| Engine, boost and vehicle-contact sounds are sometimes missing | Not reproduced in the short driving checks. Retain the native SPC engine/effect encoders and their channel priorities. Engine/effect DSP channels were active in 301/302, 322/323 and 322/323 active driving frames; this does not prove every overlapping sound is audible. |
| Start menu obstructs driving while the other racer continues | Replaced the large central panel with a compact 176x72 panel in the operating player's pane. The other pane and every pixel outside the panel remain unchanged. The panel ends at row 76; it does cover the first 28 rows of road in that pane. The race continues. |
| Large object/vehicle obscures the track when following another player | Corrected human body paint priority to match native human OAM priority over later CPU slots. Exact truncated reporter description and oversized-object reproduction remain unverified; no invented sprite resizing. |
| Red bumpers stand still; request variants and better placement | Native `$DE57` explicitly assigns braking action `$30` to explosive cars. Retain stationary explosive reds and existing placement; skip new variants. Brown bumpers remain unchanged. |
| Lap and final-lap sounds | Reused Practice's native 22-tick normal-lap and 108-tick final-lap beep, on its existing SPC command bit. Both humans can trigger it; concurrent cues share that native channel. |
| Minimap and own/opponent race rank | Reused the actual native course minimap upload, marker artwork and world-to-map rotation. Show both human markers, own rank and the other car's rank label. Rank uses the native lap/checkpoint sort key, restricted to two humans, with stable player-ID ties. Finish/DNF presentation follows existing results. |
| Added voice callouts | Skipped at the owner's request. Keep native SNES sounds. |

Native source references: `$B3CF`, `$8C87` and `$8CA6` for arrival/revs;
`$9AF7` and `$FA0F` for lap cues; `$8892` and `$AA6C` for map upload/markers;
`$E8E4` for rank sorting. Read map tiles `$1E0..$1FF` from the actual expanded
PPU upload, including imported course data, rather than embedding course art.
Audio cue state is included in current-build save/load, rewind and rollback
snapshots; older disposable save states are rejected.

## Validation

Private evidence: `captures/burndown-20261029`. All automated launches are
hidden and muted, with isolated settings/saves. No owner profile is modified.

- Four focused CTests pass: native actor/artwork/sound oracle, online reducer,
  renderer and local controls (10.51 seconds). The oracle includes 1,000
  rank comparisons against original CPU routine `$E8E4`, arrival command
  checks, normal/final-lap timers against `$FA0F`, and existing 40,000 native
  sound-encoder fixtures. The rematch arrival program is covered too.
- Actual Mute City (0), Astra (15) and Sulfur Swamp (59) short pad-driven runs
  pass HUD captures at native and 16:9 widths, 1x/2x presentation, both menu
  operators, and current-build save/load plus actual rewind-ring resimulation.
  These are short driving checks, not complete-course qualification. See
  `hud-02/report.json`, screenshots, sound/DSP traces and run logs.
- Native SPC, synthetic MSU course music and an ordered-checkpoint teleport
  fixture with both five-lap finishes pass (`msu-01/report.json`). Teleports
  position the racers; they do not directly set finish or lap-success flags.
- Two real hidden SDL netplay clients, native/HD presentation, pass 1,460
  matching confirmed ticks with an actual rollback/resimulation and no desync
  (`net-pause/report.json`). This is an ordinary pair, not a loss-injection or
  cross-build compatibility qualification.
- A real two-device SDL local session passes prepared bumper views/contacts,
  resizing, live/results restart, change course, exit and single-player return
  (`live-01/game.log`). Native traffic fixtures pass on stock course 0 and
  special-terrain course 59, including actual jump/up-magnet tiles where
  present, both contacts, save/replay and invalid-state rejection (`traffic.log`).

The exact intermittent audio dropout and oversized-object reports remain
unconfirmed. Physical-controller/Steam acceptance from the earlier burndown is
also separate; these automated checks do not establish it.
