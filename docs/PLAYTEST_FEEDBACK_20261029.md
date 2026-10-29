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

## Remaining feedback

These reports are recorded for follow-up; the lettering correction does not
claim to resolve them. The delivered 0.11.3 archives have not been replaced.

| Report | Status |
| --- | --- |
| Roughly the first second of the starting sound is wrong | Unresolved listening report; compare the actual sequence and SPC output with a native reference. |
| Engine, boost and vehicle-contact sounds are sometimes missing | Unresolved audio report; capture overlapping events and the audio channel allocation. |
| Start menu obstructs driving while the other racer continues | Unresolved live-menu usability report. Continuing the race is intentional; menu visibility needs review. |
| Large object/vehicle obscures the track when following another player | Description is truncated in the screenshot; exact object and reproduction remain unverified. |
| Red bumpers stand still; request variants and better placement | Unresolved traffic report; verify moving versus explosive behavior before changing native rules. Brown bumpers received positive feedback. |
| Lap and final-lap sounds | Requested feedback cues; check current event coverage before adding anything. |
| Minimap and own/opponent race rank | Requested multiplayer HUD features. |
| “You got boost power” voice or similar | Optional added voice feature, not original SNES sound behavior. |
