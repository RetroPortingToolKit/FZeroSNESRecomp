# Intermittent course-tile flicker

Owning feedback: `beads-iiw8`, N05. Baseline `bf79ddf`, version 0.11.0.
The reporter confirms the latest Nextcloud build still exhibits the issue;
the supplied screenshot is the earlier October 18 report. The exact current
course and original reporter route are unknown.

The 0.10.3 fix corrected wrapping the whole affine displacement at the
1024-texel streaming-map period. It did not cover floating-point cancellation
when independently interpolating the world camera and its hardware centre.
For world X 1154 -> 1155 and centre 130 -> 131, alpha 0.004 subtracts to
1023.9999999999999. Flooring that reference selects 1023 instead of 1024.
Course tiles then disagree with their intratile texture coordinates for one
presentation, especially visible along barriers, magnets and road edges.
Both single-player and VS use the affected compositor, with or without HD.

Version 0.11.1 interpolates the integer relative reference and the difference
between the wrapped camera/centre movements. A constant reference remains
exact; legitimate relative motion and world/cache seams retain their topology.
This is a default rendering correction and introduces no setting.

## Reproduction and validation

- A translation-invariance renderer test fails on the baseline: 7,168 wrong
  pixels at alpha 0.004. Translating a camera by one streaming-map period over
  a repeating course must preserve its image.
- The real Astra U-Zero raster fixture also reproduces corruption through
  `FzeroRenderViewCaptureRace`, using equivalent translated course grids and
  cameras. At alpha 0.007, the baseline differs by 3,475 native pixels and
  13,936 pixels at 2x HD. The correction yields zero differences in both.
- The expanded regression exercises single-player and VS capture paths,
  native/2x HD, narrow/ultrawide views, arbitrary presentation fractions,
  both axes, hardware-centre seams and world-coordinate wraps.
- All 18 Windows CTest checks pass. The expanded renderer test also passes
  in the Linux build using the same portable implementation.
- Actual windowed Direct3D 11 local VS runs pass at a 144 Hz presentation
  target, separately with native and 2x HD rendering. Both SDL input devices,
  prepared bumper impacts in both views, resizing and live-race menu
  restart/change/exit remain functional. This is a functional check, not
  a claim that every presentation deadline was met.
- A matched 1,400-frame baseline/candidate Astra race produces identical
  canonical race state, full WRAM, DSP registers and final noninterpolated
  pixels. Current-build save/load and rewind replay checks pass in both.
- Separate windowed Direct3D 11 single-player runs reach native GP racing
  with native and 2x HD rendering at a 144 Hz presentation target. The shipped
  support collector produces a valid ZIP containing replay/host information,
  the performance log and exit report, without ROM or PCM files.

Private repro inputs, baseline/corrected images and logs are retained under
`captures/flicker-20261027`. These establish a current presentation defect;
they do not establish that every tester-reported flicker has this cause.
For another occurrence, request an exact route and diagnostics as described
in [DIAGNOSTIC_REPLAY.md](DIAGNOSTIC_REPLAY.md), plus a clip/time marker.
