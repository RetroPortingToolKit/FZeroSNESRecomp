# CGP music and experience presets

Tracked by `beads-8wg.5.50`, `beads-8wg.5.51` and shared UI `beads-0fu.9`.

Preview.8 introduced two release variants (`beads-8wg.5.52`): `with-msu` includes
the soundtrack; `without-msu` contains no PCM/MSU payload.
**Attribution resolved:** the owner confirms the replacement PC-port archive
already reconciles the contributor exclusions. Historical exclusions remain
enforced. See [the attribution report](../assets/music/CGP_ATTRIBUTION.md). Both use
the same executable and retain MSU support. Packaging selects the variant
with `tools/make_release.py --music bundled` or `--music external`, records it
in the manifest, and prevents audio files from entering the external variant.
All other content remains identical. The launcher detects the bundled title
stream instead of advertising absent assets. Without the bundle, the CGP
preset enables previously selected custom music or keeps SNES music active;
its remaining options are unchanged. Custom paths survive preset changes.

The unused archive-install button is hidden when a host supplies no installer
(`beads-0fu.10`). F-Zero uses the folder/ZIP JSON workflow in [PACK_FORMAT.md](PACK_FORMAT.md).

## Player behavior

Mods offers **Vanilla**, **Satellaview** and **Community Grand Prix** presets.
These are editable recipes. As of 0.3.0, CGP enables the all-or-nothing Track
Pack Loader; Vanilla and Satellaview disable it. There are no per-pack toggles.
Display settings, controls, volume and rewind remain untouched.

| Owned options | Vanilla | Satellaview | Community Grand Prix |
| --- | --- | --- | --- |
| Original four cars | Original behavior | Original behavior | All four rebalanced |
| BS vehicles / original BS tracks | Off / off | On / on | Off / off |
| Track Pack Loader (all installed courses) | Off | Off | On |
| CGP P1/P2/P3 vehicle groups | Off | Off | All on; 12 total identities |
| CGP rules, including Legend | Off | Off | All on |
| Shared title screen override | Original / off | Original / off | Community Grand Prix / on |
| MSU music | Off | Off | Bundled CGP or configured custom source; otherwise SNES audio |

The console ending-credits patch is retired. The F-Zero 55 title backend and
artwork are retained, but its launcher choice is hidden for a future pack.
Title selection is now one Presentation mod, independent of track-pack
enablement. Original, Community Grand Prix and MAX League are available;
Astra shares the CGP artwork. A later manual choice makes the recipe Custom.

Legend adds its difficulty choice; the player still chooses a race difficulty.
It is enabled by default for fresh settings, while an explicitly saved choice
is respected. Vanilla and Satellaview presets disable it for stock behavior.
Required course mechanics continue to accompany their courses independently.
Grip/up magnets and Rainbow Road rules have no global mod switches; installed
packs supply their mandatory mechanics modules.
No preset is automatically applied when starting the launcher. Fresh
installations enable Track Pack Loader and Legend; music and diagnostics stay
off. Explicit saved settings remain respected.
The label becomes **Custom** when an owned option differs. Unrelated changes
do not change the label. Settings persist on Play and on closing the launcher.

Settings > Audio offers an **Enable MSU-1 music** checkbox in both bundles,
with MSU disabled initially. No file or source picker is needed. Applying
the CGP preset enables it even before recordings are added. Course recordings
use `music/<course ZIP (or FZM/FZC) basename>.pcm`
inside their own pack; no filename prefix or song map is required.
Turning music off restores SNES audio without removing any track pack.

## Shared UI contract

The picker is implemented in `recomp-ui`, not F-Zero's renderer. Games opt in
by implementing all four optional provider callbacks: `preset_count`,
`preset_get`, `preset_current` and `preset_apply`. Absent/incomplete callbacks
produce no picker and no default recipes. F-Zero owns these three definitions
and their conflict handling. Reusable API documentation lives in recomp-ui's
`docs/OPTIONAL_PRESETS.md`.

F-Zero opts into GameInfo's `msu1_managed` capability: the shared UI shows
only the MSU checkbox and help, while the game discovers pack music. Other
hosts retain their existing MSU folder/custom soundtrack interface by default.

## Soundtrack and runtime

- The complete replacement source is F-Zero CGP P1 MSU PCPORT.zip: 29 PCM
  files, totaling 566,847,024 bytes. It supersedes the P1/P2/P3 audio sets.
  Eight hash-confirmed CosmicTailz exclusions remain enforced; another 24
  superseded recordings have been physically removed from staging. The owner
  confirms this exact replacement source is attribution-cleared for bundling.
- CGP's adapter maps stable cup identity plus course ordinal to PCM 10–64.
  Both GP and Practice use this mapping. Original courses use the equivalent
  CGP music; unrelated packs without a mapping use their SNES music.
- Missing PCM files use the adapter's original SPC fallback. CGP playback
  needs no separately selected ASM mod.
- Imported courses load their own native SPC theme before racing, rather than
  the base engine's course metadata. This also covers MSU disabled and missing
  recordings. The full CGP mapping is in [CGP_SNES_MUSIC.md](CGP_SNES_MUSIC.md).
- A custom pack containing the supported, digest-pinned Conn/Cubear v11
  `f-zero_msu1.ips` uses that adapter and its standard numbering. Without
  that patch a custom pack uses CGP numbering. Other patch versions are not
  supported. The legacy adapter is composed into each derived car image.
- Save/load and rewind resume the saved song position, including loop and
  resampling phase. States keep a small playback bookmark and catalog identity;
  the recordings stay in the installed music folders. Missing recordings fall
  back to native music. Online rollback preserves live music and excludes local
  playback from canonical simulation snapshots.

## Burndown and validation

- [x] Audit all three archives; compare every PCM hash and loop bound.
- [x] Import only approved music and make release packaging verify it.
- [x] Add opt-in shared UI capabilities without game-specific defaults.
- [x] Add three editable F-Zero presets and conflict handling.
- [x] Test all preset/MAX/Bower combinations and unrelated settings retention.
- [x] Test eleven CGP cups, original/BS mappings, missing music and unknown
      pack fallback with distinct synthetic stereo samples.
- [x] Test Practice music, actual rewind resimulation and legacy custom music
      with both stock and the additive car catalog.
- [x] Check real launcher selection and persistence across restart.
- [x] Check actual bundled audio through desktop playback and the core mixer.

Release handoff additionally verifies a fresh ZIP extraction against its full
file manifest, then starts that executable and checks preset/music behavior.

Reproduction: core CTest, recomp-ui's `launcher-presets` and
`launcher-video-pick` CTests, and `tests/validate_cgp_music.py` with a private
stock ROM. Captures stay under ignored `captures/msu-presets`.
