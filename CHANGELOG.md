# Changelog

## 0.10.0 - Crash capture and replay

- Keep recent inputs and game checkpoints automatically, including existing
  installations. Crash diagnostics and replay defaults on in Mods for testing.
- Write shared engine crash reports and Windows minidumps alongside the
  recording. Later launches preserve earlier crash evidence.
- Capture effective pack/video choices, renderer details and course records.
  Start a new checkpoint after loading a state or rewinding.
- Add an isolated developer replay tool that checks build/content identity and
  detects guest-state divergence. Performance diagnostics remain separate.

## 0.9.1 - Title demo vehicle fix

- Keep the recorded Blue Thunder title demo independent of a previously
  selected CGP vehicle's artwork, HUD, rivals and handling.
- Preserve Blue Thunder's native steering and complete replay, with normal
  custom vehicle selection working again when starting a race.

## 0.9.0 - Quick saves and Practice results

- Add a dedicated Quick save slot with a thumbnail. Bind Quick Save and
  Quick Load to keyboard keys, controller buttons or controller combinations
  in Controls. The Quick slot stays accessible when switching courses.
- Keep the save-state browser centered at 4:3 in widescreen windows.
- Show the actual custom vehicle beside BEST 5 and BEST LAP times after
  Practice, including Dragon Bird and the other CGP car packs.
- Include the legacy course import fixes described in 0.8.4 below.

## 0.8.4 - Legacy Fuzee course imports

- Import supported Fuzee layouts used by Nebula Highway, Alternate Strike
  and Parallel Dimension into editable course ZIPs. Preserve source order,
  course names, native music, terrain, art, skies, minimaps and checkpoint/AI data.
- Recognize relocated native music tables from their actual instruction
  operands. Keep donor vehicles and global code excluded from course imports.
- Validate real driving across repair strips, walls, jump/boost pads and
  controller fuzz, alongside saved-record checks.

## 0.8.3 - Native terrain preservation

- Preserve Maximum Velocity's original repair strips, jump pads, dash plates
  and boundaries when converting its older FZEdit courses. Its disabled mine
  bitmap loader no longer produces an empty terrain-property table.
- Add repeatable driving and controller fuzz checks for imported courses,
  using real terrain approaches and game physics alongside records checks.
- Packs imported from Maximum Velocity with 0.8.2 need removal and reimport.

## 0.8.2 - Older course-pack imports

- Import older FZEdit layouts used by the SNES Maximum Velocity hack, Hybrid
  and ReFractured, preserving course names, source order, maps and SNES music.
- Map Hybrid's supplied MSU recordings from its patch's selector and install
  them with the usual per-pack course filenames.

## 0.8.1 - Shared rendering workers and import refinements

- Distribute HD racing and finish scenery across persistent rendering workers,
  using the shared pool merged into snesrecomp. Keep identical output, a serial
  fallback and sleeping workers between frames.
- Improve imported MSU song matching from donor selectors and keep music
  discovery working when course source files are renamed.
- Identify legacy Fuzee layouts in conversion reports; unsupported course
  formats still require further conversion work.
- Retain editable course ZIPs, ready-to-use caches, per-pack music filenames
  and both MSU/no-MSU downloads.

## 0.8.0 - Custom Content importer

- Add one Import flow for course ZIPs, F-Zero Edit projects, IPS/BPS patches and
  supported ROM hacks, with editable pack names and cup assignments.
- Convert supported ROM-hack courses into editable packs automatically. Keep
  included CGP, Astra, Bower and MAX packs protected and retain their records.
- Import confidently matched MSU recordings. Keep an installed-pack notice and
  tooltip for songs that need manual placement; missing songs use SNES music.
- Explain import limits: custom vehicles and unsupported game-wide code are
  excluded. MF and FZero55 remain user imports, not bundled default packs.
- Include standalone import tools, instructions and ready-to-use course caches
  in both downloads; no Python installation is needed.

## 0.7.4 - Car-menu transitions and HD race results

- Keep CGP car colors and neighboring columns stable while opening the stats
  and league panels, preserving the native rotating-car animation.
- Use native shaded lettering for the expanded league/class picker.
- Keep HD Mode 7 detail on the frozen course behind race results.

## 0.7.3 - Shared renderer optimization and race-menu fixes

- Share Grand Prix and Practice records per course/car, retaining better
  Practice times from previous test builds. Remove the mode toggle.
- Keep all lap/rank digits together during the live end-of-league recap in
  widescreen, including HD Mode 7.
- Match original difficulty controls: Up/Down stop at the ends, Select cycles.
  Keep the confirmed class visible through its fade.
- Apply each car group's text colors before the course intro first appears,
  fixing the brief stock-color flash for imported vehicles.
- Reduce CPU work in HD Mode 7 sampling without reducing resolution or
  changing interpolation or output pixels.
- Use the shared snesrecomp sampling helpers in both its PPU and F-Zero's
  renderer; regenerate private ROM modules for the updated framework.

## 0.7.2 - Pack music and Records scenery corrections

- Use one MSU enable switch in both bundles, without a source/file picker.
  Course music matches the visible ZIP filename, including
  `huckmine.zip` with `music/huckmine.pcm`.
- Keep pack music discovery enabled by the CGP preset even when recordings
  have not been added yet; missing recordings retain SNES music.
- Remove the added vehicle/mode heading from Records and restore the full
  native-height scenery strip. Fix gray boxes behind transparent stars and
  honor the native skyline layer priority.
- Retain prebuilt course caches in both bundles.

## 0.7.1 - Ready-to-use course caches

- Ship prebuilt course and source-extraction caches in every bundle, so
  unchanged bundled tracks do not extract or compile on first launch.
- Build caches from the exact release inputs. Keep the editable course ZIPs
  and automatically rebuild caches when projects change or new ones are added.

## 0.7.0 - Records testing and presentation fixes

- Add the default-on Always show Records mod so testers can open Records
  from the title menu before setting a time, without changing saved records.
- Fix Astra's stray Z at the first-lap notification by restoring native
  race glyphs after the course introduction.
- Keep course names and venue art together through Records transitions;
  prevent brief donor-name flashes and premature graphic replacement.
- Match stock and imported course details to the original tall lettering,
  colors and underline, with native fades and saved-state restoration.

## 0.6.0 - Editable source archives for every bundled course

- Ship all 75 course versions as editor ZIPs with their existing course filenames.
  Huckmine retains the author's original project; the other 74 are reconstructed.
- Keep generated FZC files in the disposable course cache. Unchanged projects
  preserve the previous runtime data, music, artwork, mechanics and records.
- Preserve native details absent from FZEdit using a data-only reconstruction
  companion. Editing a source component rebuilds that component on launch.
- Scan each course archive directory once to avoid repeated work at startup.

## 0.5.0 - Editor source example and pack music settings

- Ship Huckmine as its original FZEdit source ZIP with pack descriptions;
  build the course cache on launch. Preserve its twelve author files.
- Add Menu and event music in Mods, prefilled with CGP's six recordings.
  Browse to replace individual cues, Clear to restore pack defaults.
- Include the supplied ten Astra recordings in the music bundle, using the
  same course filenames supported by the smaller download.
- Preserve CGP identity in newly recorded Practice ghosts and keep rival
  selection state outside the native ghost codec's workspace.
- Show imported course scenery and the selected vehicle's icons in records.
- Huckmine's source differs slightly from the extracted ROM revision; Zenith
  starts a new records namespace. Existing records remain on disk. The ghost
  workspace fix changes the vehicle snapshot signature; old vehicle save
  states are incompatible, while battery records are retained.

## 0.4.0 - Course-owned mechanics and simple music filenames

- Load optional race music from each pack's `music/` folder by matching its
  course source filename. No extra song map, prefix or MSU descriptor required.
- Bundle CGP race recordings using those filenames inside its course pack;
  remove the separate audio pack from new releases. Missing songs use SNES audio.
- Package mandatory grip/up magnets and Rainbow Road rules with the courses
  that require them, and remove their global Mods checkboxes.
- Enable Track Pack Loader and Legend by default; leave diagnostics off.
  Preserve explicit saved settings and the Vanilla/Satellaview presets.
- Preserve all 75 imported course record identifiers. Provide runtime-only
  Windows tester ZIPs with and without the approved CGP recordings.

## 0.3.0 - Discoverable course and music packs

- Replace individual track switches with one Track Pack Loader. Load versioned
  JSON packs from folders or ZIPs; BS Satellaview Tracks remains exclusive.
- Convert the existing 75 courses into portable resources with identical
  decoded data and record keys. Preserve scoped mechanics and native music.
- Import raw FZEdit FZM/AIP/TMX/TSX and image exports without the editor installed.
- Resolve installed/custom MSU recordings by soundtrack namespace. Astra and
  CGP can share an audio directory without confusing their track numbers.
- Discover title artwork and music from pack metadata. Missing songs use SPC.
- Include authoring instructions and with/without-CGP-audio local bundles.

## 0.2.0 - Astra Front and expanded records

- Consolidate all four CGP original-car rebalances into one opt-in and migrate
  existing partial choices. Preserve the independent added-vehicle switch.
- Import Astra's authored intro lettering, fixing the corrupted Z in U Zero
  without changing course or record keys.
- Add Astra Front as an optional pack: ten courses in the authored Astra and
  Front cups, with donor music and scoped CGP-compatible terrain behavior.
  Exclude unused donor courses and validate each new cup's records.
- Consolidate title artwork into one independent Presentation mod with
  Original, Community Grand Prix and MAX League choices. Migrate CGP title
  settings, keep F-Zero 55 hidden, and preserve course/vehicle records.
- Load each imported course's SNES music in GP and Practice, including when
  its MSU recording is missing. Map all 55 CGP courses plus MAX and Bower from
  their donor music tables; restore Mute City III's Mute City theme.
- Keep music corrections separate from existing course and vehicle record keys.
- Browse all enabled cups and vehicle-specific records in the native records
  overview/detail layout, including existing times after restart.
- Keep browsing read-only, with cup pages, car selection and GP/Practice
  contexts. Retain existing save keys and snapshot layout.
- Restore the Deluxe records VRAM upload mode on snapshot load, fixing
  garbled detail text, venue graphics and minimaps after loading a state.
- Create the base save directory before a track-only setup's first cup save.

## 0.1.10 - F-Zero Forever version baseline

- Continue the current work on `f-zero-forever`, branched from `fzero-55`.
- Start an independent version sequence for all future release bundles.
  The next release is 0.1.11 for fixes, or 0.2.0 if it adds a feature.
- This establishes the baseline only; no 0.1.10 bundle was produced and
  existing 1.8.3 preview bundles retain their original names and versions.

## 1.8.3-fzero-55-preview.10 - CGP presentation and PC-port soundtrack

- Replace the previous music bundle with the author's 29-recording PC-port set.
  Physically remove superseded recordings from extracted staging.
- Restore Community GP title artwork and the CGP energy HUD lightning symbol
  and underline. Retain F-Zero 55 artwork with its launcher choice hidden.
- Retire the console ending-credits ASM from runtime, presets and Mods.
- Fix successful race results splitting lap/rank digits during the fade to the
  next race. Preserve native table and placing-graphic alignment in wide/HD views.
- Honor MAX League's authored palette cycles, fixing Metal Forest flashing.
  Port Canyon flashing remains unconfirmed in direct and GP-progression checks.

- Exclude eight CosmicTailz recordings identified by exact reference hashes.
- Record the owner's confirmation that the replacement PC-port archive already
  reconciles CosmicTailz/TheBlurCafe attribution; retain historical evidence
  and exclusion checks without an outstanding release requirement.
- Never extract excluded files during import; physically delete old matching
  files when staging and reject them during package verification. Missing music
  uses SNES fallback.
- Delete obsolete music ZIPs, reference audio archives and excluded PCM copies
  from older captures/staging. Keep only the filtered extracted source set;
  PCM/MSU Git ignore rules are a secondary guard.

## 1.8.3-fzero-55-preview.9 - unified CGP vehicles

- Replace the three CGP car-pack toggles with one all-or-nothing CGP vehicles
  mod. Preserve authored selection columns and Grand Prix rival groups.
- Migrate saved partial rosters to the full roster. Keep original-car
  rebalances separate and retain mutual exclusion with BS vehicles.

## 1.8.3-fzero-55-preview.8 - soundtrack download choices

- Offer Windows ZIPs with and without bundled CGP music, sharing the same
  executable and game content. Both retain custom MSU playback.
- Detect bundled music for launcher choices and defaults. The compact build's
  CGP preset uses selected custom music, or SNES audio until music is supplied.
- Hide archive installation when a host has no installer. F-Zero no longer
  displays an unsupported Install .psxmod button in either Mods view.

## 1.8.3-fzero-55-preview.7 - music and editable presets

- Bundle the approved 61-track CGP soundtrack once, shared by all car groups.
  Audio settings offer Community Grand Prix and Custom; music starts off.
- Add Vanilla, Satellaview and complete CGP presets through recomp-ui's new
  opt-in host API. Include Legend in CGP, preserve unrelated packs/settings,
  resolve BS conflicts, and keep every option editable afterward.
- Map GP and Practice music by cup and course, retain SPC fallback for missing
  tracks, and compose the legacy custom adapter into derived car images.
- Verify soundtrack hashes during import and packaging. No ROM is included.

## 1.8.3-fzero-55-preview.6 - authored information cards

- Replace inherited retail information cards with the selected CGP car's
  authored specifications, acceleration graph and name/frame colors.
  Original cars retain their original cards unless individually rebalanced.
- Extract these resources automatically from the native tilemap and graphics
  format. Keep the existing Satellaview panel, controls and shared labels.
- Qualify all twelve CGP cards against reference-emulator captures, independent
  rebalance settings, partial packs, Practice, rewind and wide/HD output.
  Older CGP card snapshots are incompatible; existing course record keys stay
  unchanged.

## 1.8.3-fzero-55-preview.5 - tester feedback fixes

- Keep each authored P1/P2/P3 set together in its native selection column,
  including partial installations, without duplicating original cars or
  enabling their optional rebalances. Older CGP menu save states are rejected;
  existing course record keys are retained.
- Fix Moon Shadow's alternating pink/blue minimap dot and other cars retaining
  retail marker colors. Resolve colors by car identity for GP and Practice
  while preserving the native exhaust animation.
- Clarify that CGP adds 40 courses beyond the original 15, plus separately
  selectable revisions of those original courses. Information-card numbers
  and styling remain a documented limitation.

## 1.8.3-fzero-55-preview.4 - tester build

- Add Bower League, restore MAX to Mods (default off), and restore the CGP
  leagues' authored names. All packs together provide 16 cups / 80 versions,
  including untouched originals and separately selectable CGP revisions.
- Add coherent CGP vehicle packs and separate original-car rebalances. Grand
  Prix uses each enabled CGP set's three rivals; Practice allows any enabled
  opponent. Stock BS vehicles remain a separate mode.
- Preserve the native car selector with continuous columns and side previews.
  Extend Practice to every enabled cup/course and fix its doubled heading.
- Fix targeted Marine City trampoline and Lightning railroad landings,
  results-screen rendering, and missing launcher fonts, logo and controller art.
- Replace Volcania with the author's approved venue revision.
- Include TESTER_NOTES.md with defaults, known limitations and testing guidance.

## 1.8.3-fzero-55-preview.3 - local sharing build

- Add a Title screen dropdown to Community Grand Prix: Original (default)
  or F-Zero 55. The optional artwork applies only while CGP is enabled.
  Cars, gameplay rules, cup selection and course records remain independent.
- Save track-pack settings when closing the launcher as well as pressing Play.

## 1.8.3-fzero-55-preview.2 - local sharing build

- Split BS Satellaview vehicles and original BS tracks into independent mods.
  BS tracks appear under Track Packs; the eight-car roster works with any cup.
- Include CGP's ten corrected BS courses alongside its thirty new courses.
  CGP and original BS tracks are mutually exclusive, avoiding duplicate courses.
- Add 19 opt-in CGP gameplay, cosmetic and fix mods, including P1/P2/P3 tuning,
  energy boost and exhaust choices. These work independently of track packs.
  BS cars keep their individual stat tables and exhaust positions; shared
  physics and the adapted energy boost also apply to them.
- Fix BS car-select palette corruption and disappearing previews caused by
  duplicate scanline updates. All eight selections retain their colors.
- Keep MAX hidden and disabled. Music remains optional, off by default and
  absent from the ZIP. Rewind remains enabled with R; shoulders default to D/C.
- Imported courses currently appear in Grand Prix; imported Practice courses
  and a combined records browser remain future work.

## 1.8.3-fzero-55 - local branch build

- Use F-Zero-specific keyboard defaults: R for rewind, D/C for L/R shoulders.
  Launcher Reset to Defaults uses the same bindings; other games are unchanged.
- Enable rewind by default for fresh installations; R opens the filmstrip.
  Existing saved preferences remain respected.

- Bundle CGP's 30 new courses alongside the 15 original and 10 BS courses:
  55 tracks in 11 cups, with the canonical eight-car roster and rendering.
- Add enabled packs directly to the in-game league menu. Bundled packs use
  one checkbox; no master Track Library mod or file picker is needed.
- Preserve MAX League's files and implementation, but hide and disable it.
- Retain additive IPS/BPS discovery, stable cup records and contributor tools.
- Exclude the CGP MSU soundtrack. This Windows build is local and unpublished.

## 1.8.3 - 2026-09-21

- Add a default-off Diagnostics mod for troubleshooting performance. Enable
  it under Mods, reproduce a slowdown, and attach the newest timestamped
  report from the diagnostics folder beside the game or AppImage.
- Reports include hardware and build information, effective HD Mode 7 and
  display settings, and timings for rendering, uploads, presentation and
  frame pacing. Logs stay local and contain no ROM or save data.
- This patch adds diagnostic reporting; it does not claim to fix the
  reported HD Mode 7 performance regression.

## 1.8.2 - 2026-09-21

- Save and honor Skip Launcher. `--launcher` reopens it even with the setting
  enabled; a missing or invalid remembered ROM also falls back to the launcher.
- Allow whole-number HD Mode 7 resolution multipliers from 2x through 10x.
  Keep 2x as the default and show a severe-slowdown warning above 4x. Graphics
  backends that cannot create the requested texture fall back for that session.

## 1.8.1 - 2026-09-21

- Keep vehicle explosions and smoke together in Widescreen. Their first
  four sprite pieces reuse rank-display slots; only actual rank digits now
  follow the HUD anchor. Works with native and HD Mode 7 rendering.

- Reduce HD Mode 7 CPU cost with Widescreen by sharing native/HD composition,
  caching scanline color math and repeated texture lookups, and using integer
  wrapping for ordinary texture coordinates. Rendering remains pixel-identical
  in baseline comparisons; ultrawide views and high Presentation FPS still
  increase the workload. See [performance measurements](docs/HD_MODE7_PERFORMANCE.md).

## 1.8.0 - 2026-09-20

- Add optional HD Mode 7 track rendering at 2x or 4x resolution. Enable it
  under Mods > HD Mode 7; it starts off and works independently of
  Widescreen and Presentation FPS.
- Resample track tiles with finer affine coordinates and interpolate
  compatible camera scanlines. Cars, HUD and menus keep their pixel artwork,
  and game timing and save-state thumbnails retain native behavior.
- Include shared snesrecomp HD Mode 7 support and documentation. Both SDL
  and OpenGL/shader presentation use the larger texture. 4x costs more CPU
  time, especially with wide aspect ratios or high presentation FPS.

## 1.7.1 - 2026-09-20

- Remember ROMs selected with Browse For ROM after both Play and closing the
  launcher (#8). The shared recomp-ui launcher now saves the verified source
  ROM path; cancelling or selecting an invalid file preserves the last valid
  selection. Includes regression coverage for actual file selection and
  relaunch, rather than relying on a pre-created ROM cache.

## 1.7.0 - 2026-09-20

- Keep the live game-over HUD anchored, including the timer and power fill.
  Anchor the results score/counter while keeping the lap table and menu
  centered through both Try Again and End Game fades.
- Shaders remain off by default; use Settings > Display > Shader to enable one.

- Resolve saved music/shader paths and saves from the installation directory,
  including launches from shortcuts or other working directories. Relative
  command-line ROM paths still resolve against the caller's directory.
- Updated the shared engine to include audio-buffer recovery after state loads
  and starvation, legacy DMA save compatibility, and the latest host settings.

- Gameplay now reads the launcher's selected gamepad, per-device bindings and
  deadzone. Disconnecting another device no longer drops the active pad;
  reconnects and launcher/game focus transitions keep input working (#3).
- Keep the player's spark sprite with the car instead of moving it to the
  widescreen HUD's right anchor (#4).
- Enable user-provided Conn/Cubear v11 MSU-1 patches and music for stock and
  BS Deluxe, composed in memory without modifying the user's ROM. Added
  separate MSU save modes and audio restart after state loads. Patched sessions
  currently use interpreter execution. Validated with synthetic tracks and
  a user-supplied JUD6MENT pack in stock and Deluxe playtests (#5).
- Document the existing custom GLSL preset picker, including CRT-Geom. Invalid
  presets now fall back to unfiltered output. Preserve imported shaders during
  rebuilds but exclude them from release packages. CRT-Geom is not bundled
  pending redistribution-license compatibility review (#6).

## 1.6.2 - 2026-09-20

- Remember launcher settings on both Play and Quit, including display, sound
  and rewind options (#2).

## 1.6.1 - 2026-09-18

- Fixed the ROM button doing nothing on the Linux AppImage. The AppImage set a
  library path for itself that it also handed to the desktop file dialog it
  opens, so the dialog could not start — and its failure was read back as "the
  player cancelled", which is why the click looked ignored. Host programs now
  get a host environment, and a file dialog that cannot run falls through to
  the launcher's own built-in file browser instead of doing nothing.

## 1.6.0 - 2026-09-18

- Added the in-game save-state menu. **F7**, or **Select + R** on a gamepad,
  opens a browser over the frozen game: twelve slots, each with a thumbnail of
  the moment it was saved, A to load, X to save, B or Escape to back out. The
  menu itself is the framework's (`snes_savestate_menu.c`), so a fix there
  reaches every port; this host supplies the events and the pixels. It draws as
  its own layer in both presentation paths - the SDL renderer and the OpenGL
  shader path - so it lands at window resolution and stays crisp at 21:9
  instead of being composited into the 256-pixel game buffer.
- Added rewind. **F8**, or **Select + L**, opens a filmstrip of the recent
  past; Left and Right scrub, A or Enter drops back in, B or Escape leaves.
  It is off by default because it keeps whole snapshots of the machine in
  memory (about 330 KB each); the launcher's Settings page turns it on and
  sets the depth (50-200 snapshots) and the interval (1-30 frames), which are
  remembered in `config.ini` next to the executable — recomp-ui leaves
  Settings persistence to the host, and this host persisted none of it, so
  the switch would otherwise have come back off on every launch. A rewind
  or a state load now resets the presentation clock and the renderer's
  interpolation history together, so nothing blends across the jump and the
  seconds after one are not run as catch-up.
- Both keys are rebindable on the launcher's Controls page, as SaveStateMenu
  and Rewind, and are read back from `config.ini` next to the executable.
  Where one of them claims an F-key, it wins over the old quick slot on that
  key; rebinding it hands the key straight back. The quick-slot keys
  (F1-F12 to load, Shift+F1-F12 to save) still work; every slot is also
  reachable from the menu, with a thumbnail.
- Save states now record which cartridge took them, and loading one taken on
  the other is refused cleanly instead of resuming a machine whose ROM does
  not match the RAM being restored. Stock and BS Deluxe already kept their
  slots in separate directories under separate prefixes, so this only fires on
  a file moved by hand - which used to crash. A load the host makes itself is
  checked before the engine is called at all; a load from the browser, which
  goes through the engine directly, is undone from a snapshot taken when the
  browser opened. The tag reuses a padding byte, so 1.5.0 states still load.
- Bumped `snesrecomp` (242 commits) and `recomp-ui` (36). The runner's
  save-state menu and OSD units now pull in SDL, so they are held back from
  the SDL-free headless host and handed to the desktop host explicitly. Stock
  4:3 output is byte-identical across the bump on all 160 recorded captures,
  and the native compositor still matches the PPU's own frame exactly.

## 1.5.0 - 2026-09-18

- BS Deluxe is now compiled into the executable, so every download carries it
  and a missing or damaged `mods/bs-deluxe.dat` can no longer stop the game
  from starting. The embedded bytes are verified exactly as a file was: magic,
  declared sizes, the stock digest they were built against, ordered
  non-overlapping records, and the digest of the patched cartridge. A file
  beside the executable, or `FZERO_DELUXE_DATA`, is still tried first as a
  development override; if anything fails, the game logs and runs stock for
  that session without rewriting the settings file. Release packaging now
  refuses to build without the embedded payload.
- Corrected the streamed-square test to retail's 16-unit block grid. The
  anchor's low bits do not move the square, so 138,481 of 7,323,648 measured
  cells were classified outside it and up to 0.106% of 32:9 margin samples kept
  a stale tile; both are zero now.
- Every mod now ships on by default and the aspect defaults to Fit, which
  follows the window between 4:3 and 32:9. A first run with no
  `fzero-video.ini` therefore starts with Widescreen on at Fit, Presentation
  FPS on at Auto, and BS Deluxe on. `FzeroVideoStock()` is the stock baseline
  and is what the headless host, `FZeroRenderCapture` and the runtime's own
  pre-host viewport use, so captures and tests are unchanged unless
  `FZERO_ASPECT` opts in. A build configured without the BS Deluxe native
  module logs that it is starting stock instead of refusing to launch.

- Fixed the widescreen Mode 7 draw distance, reported by PowerPanda: pieces of
  the track were missing in the margins and appeared only once they reached the
  middle of the screen. Retail streams the tilemap for the stock 256-pixel
  viewport - `$03:9243` keeps one 1024-by-1024-unit world square uploaded, which
  fills the tilemap exactly - so a widened viewport sampled outside it and read
  the tiles another part of the course had left behind. The compositor now
  resolves those samples through retail's own course tables in WRAM bank `$7F`,
  which the frame snapshot already carries. Samples inside the streamed square
  still read the live tilemap, stock 4:3 output is byte-identical, and no guest
  state is written. Non-player cars were measured and are unaffected: their only
  horizontal visibility test is already widened, and what removes them is
  retail's longitudinal window and proximity cull, identical at every aspect.
- Fixed occasional full-screen flicker introduced by that change on the
  interpolated presentation path. Retail writes either representative of the
  camera's map position - some frames the camera's own value, some that plus
  1024 - and an interpolated scanline origin takes the shortest path across
  that seam, so subtracting the current frame's representative moved every
  Mode 7 sample a whole map period for one presentation. The centre and the
  camera are now blended the same periodic way as the origin. Measured over
  321 consecutive race frames at 21:9: seven presentations changed more than
  10,000 pixels against 1.4.3, the worst 63,091 of 100,352 (63% of the frame);
  after the fix none do, and the worst is 946. Output at full blend is
  unchanged.
- Added `tools/measure_draw_distance.py` and a `--sequence[=alpha]` mode for
  `FZeroRenderCapture` that replays a whole race through the compositor in
  order, including the interpolated presentation path.

## 1.4.3 - 2026-09-18

- Updated the bundled BS F-Zero Deluxe mod from upstream USA v1.0 to v1.1
  (upstream April 1, 2025). v1.1 adds the recovered BS F-Zero Grand Prix 2
  Week 1 data (Forest course graphics, Forest I/II track and path, BS-1 League
  race parameters, adjusted Forest III), fixes an upstream course-load CPU
  crash and garbled records graphics, and makes opponent speeds and Exploding
  Bumper spawn rates league-aware.
- Regenerated the namespaced Deluxe native module and the guarded cartridge
  delta from the v1.1 image; the runtime now verifies the v1.1 digest and
  rejects v1.0 data. `patches/bs-deluxe-usa.ips` is now the upstream v1.1 USA
  patch.
- The Deluxe import tools accept both the v1.0 and v1.1 archive layouts and
  read the upstream version from the archive readme.

Validated on the regenerated 1.4.3 build: all five test suites pass, plus the
patch-tool unit tests for both archive layouts. A scripted power-on route
reaches a BS-1 League Forest I Grand Prix race with Deluxe enabled and runs
1,800 frames in the desktop host at 21:9 (144 Hz presentation, SDL dummy
drivers) and in the headless host at 16:9; captured frames show the eight-
machine grid, the BS-1/BS-2 league list, the Forest I course card and the
race. A 600-frame stock boot without Deluxe stays on the stock cartridge and
writes no Deluxe save directory. The v1.1 module keeps the interpreter-floor
scheduler policy from 1.3.0. Full-cup coverage remains a user playtest item.

## 1.4.2 - 2026-09-08

- Extended live title and start-sequence scenery to the selected aspect ratio
  while keeping logos, menu text, and course-selection artwork together.
- Kept the race HUD adaptive from its first setup frame through attract-demo
  exit fades; intro lives counters now retain their right-edge position.
- Fixed the split Training course map and the centered timer after a Training
  crash. Training and Grand Prix loss screens now use their own HUD layouts.
- Fixed an oversized red/gray panel on the Grand Prix loss screen caused by
  extending its collapsed color window into the widescreen margins.

Accepted in interactive playtesting. All five regression suites pass, with
focused coverage for scene transitions, temporary sprite reservations, and
loss-screen color windows. Captured Training, loss, and attract-exit frames
preserve stock-width output.

## 1.4.1 - 2026-09-08

- Fixed distant scenery disappearing or changing abruptly in widescreen,
  especially at 21:9. Both skyline layers now sample the full panorama across
  section boundaries, including the partially filled final section.
- Added 15 clean interpreter fallback observations to the static coverage
  profile and regenerated with the native analyzer. The generated program now
  contains 468 AOT-eligible variants, up from 410 in the previous build.
- Preserved stock-width rendering, HUD placement, sprite visibility safeguards,
  and the interpreter policy used for widescreen opponent projection.

The skyline fix was accepted in an interactive owner playtest. Regression tests
cover both background panoramas across all aspect modes; 104 captured
frame/aspect comparisons preserve the original center and lower track/HUD.
All five test suites pass after regeneration. A 10,800-frame 21:9 stock soak
matches the previous build byte-for-byte in final WRAM and framebuffer, and a
1,800-frame BS Deluxe desktop smoke run completes successfully.

## 1.4.0 - 2026-09-08

- Added launcher Display shader support for F-Zero, including staged presets:
  CRT Soft, LCD Grid, Sharp, and Warm Composite.
- Added an OpenGL GLSL presentation path used when a shader is selected. The
  no-shader path keeps the existing SDL renderer behavior.
- Added Display aspect choices for F-Zero: 4:3, 16:9, 21:9, 32:9, and Fit to
  window. The built-in Widescreen/Presentation mod remains authoritative over
  aspect when enabled.
- Added widescreen and BS Deluxe screenshots to the README.
- Added a 21:9 BS Deluxe race screenshot to the README.
- Added the BS Deluxe USA IPS patch to the repo and release ZIP at
  `patches/bs-deluxe-usa.ips`.
- Added Linux x86_64 AppImage release packaging.
- Updated BS Satellaview README credit/permission wording.

Validated with direct unit test executables, no-shader and CRT shader desktop
smoke runs, and a live CRT window capture after fixing the OpenGL VAO binding
needed by the shared GLSL renderer.

## 1.3.0 â€” 2026-09-07

Private feature release, advancing two minor versions from 1.1.0 as requested.

- Added BS F-Zero Deluxe USA 1.0 as one independent, all-or-nothing mod:
  original and BS courses, eight machines, alternate leagues/layouts, records
  and Practice ghosts. The stock ROM file remains unchanged.
- Retained separate Widescreen and Presentation FPS plugins, adaptive HUD
  anchoring, wider opponent projection and interpolation independent of logic.
- Added a separately namespaced Deluxe module and isolated 32 KiB SRAM saves.
  Deluxe currently runs its main scheduler through the interpreter floor;
  native interrupt helpers and the custom renderer remain active.
- Fixed the car-selection HDMA bus-read crash and the shared PPU's missing
  XOR/AND/XNOR window operations that hid league and difficulty text.
- Added original North American SNES box art to the launcher.

Validated both Blue Thunder on Forest I and Blue Falcon on Mute City I in
visible desktop runs. Five game test suites and the focused framework dispatch
and PPU regressions pass. Full-cup coverage and ghost recording/playback remain
unverified. See [BS Deluxe details](docs/BS_DELUXE_EXPLORATION.md).

The private release can include the locally verified Deluxe delta and upstream
credits. The packaging tool refuses this payload unless the F-Zero repository
is private. No stock/patched ROM or user save is included.

## 1.1.0 â€” 2026-09-07

- Added a native Mode 7 renderer for 16:9, 21:9, 32:9 and Fit to window.
- Preserved stock 4:3 rendering. Race HUD groups anchor to the outer edges;
  menus and course-intro text remain centered.
- Extended opponent projection/activation to the current viewport through
  original game routines. Viewport changes apply at simulation boundaries.
- Added separate Widescreen and Presentation FPS built-in plugins in recomp-ui,
  each with independent toggles and settings.
- Added Auto refresh and 60/90/120/144/165/240/360 FPS presentation. Simulation
  stays at 60.098811862 Hz; native camera/car interpolation never writes game state.
- Added aspect/FPS shortcuts, pause/minimize pacing, soft reset, deterministic
  input/window replays and snapshot capture tools.
- Fixed stale sprites appearing in wide margins and separated course-title text.

Owner playtest accepted the checkpoint for release. See
[validation evidence and limits](docs/ADAPTIVE_RENDERER.md).
This is the first semantic-version tag; the previous WIP binary was labeled 1.0.
