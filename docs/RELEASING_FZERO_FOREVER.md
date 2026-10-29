# F-Zero Forever release versioning

Owner decision: 2026-09-24. Tracked by `beads-8wg.5.55`.

Development continues on `f-zero-forever`, created from `fzero-55` at
`eba1adcfb1cc457400094e15e929f84b31532bb0`. The earlier branch and all existing
preview bundles keep their history. This change does not produce a new release.

## Version policy

`VERSION` is the shared source of truth for this branch's game and bundles.
The new baseline is **0.1.10**, corresponding to the work already completed.
There is no new 0.1.10 download.

Before the next release, review the changes and update `VERSION`:

- Fixes, maintenance or packaging changes: **0.1.11**.
- Any new feature: **0.2.0**.

Continue that rule afterward: fixes increment the patch number; a feature
increments the minor number and resets the patch number to zero. A release
containing both features and fixes takes the feature increment. Branch creation
and this versioning setup do not themselves count as a new gameplay feature.

Both Windows variants (`with-msu` and `without-msu`) use the same version,
as do any other platform bundles produced for that release. Do not continue
the old `1.8.3-fzero-55-preview.N` sequence or give variants independent numbers.
Use the plain version without a `fzero-55-preview` label.

## Current release

The current local tester bundles are **0.12.1**: native multiplayer lap/boost
visuals and corrected Quick Save/Load shortcut capture, in addition to the
0.12.0 minimaps, human ranks, sounds and compact live menu. Both variants retain
per-pack music, prebuilt portable course caches and the qualified native runtime.
Next fixes are 0.12.2; new features are 0.13.0.
Tracked by `beads-8wg.5.115`; feedback work is `beads-iiw8`.

## Preparing a release

Tester bundles use the default `--profile player`: runtime resources, short
launch instructions, credits and licenses only. Integrity manifests remain
beside the ZIPs. Use `--profile developer` for a separate `-dev` bundle with
ASM sources, extraction reports, authoring documentation and reference images.
Keep developer staging folders separate from the shareable tester directory.

**Every bundle ships with a prebuilt cache.** The packager copies the reviewed
course sources into a fresh staging directory, then runs the real loader there
to populate both `mods/packs/.cache/courses` and `.cache/sources`. It checks
the resulting courses against their extraction manifests. Never copy caches
from a user's installation or a developer build. Include these generated caches
in the archive and integrity manifest; keep the editable course ZIPs as well.
Unchanged projects must reuse the shipped cache after extraction to another
directory. Changed/new projects still rebuild automatically.

1. Update `VERSION`, add the matching changelog entry and refresh tester notes.
2. Reconfigure the existing build so `SNESRECOMP_BUILD_VERSION` matches
   `VERSION`; an old CMake cache can retain the prior preview version. In
   PowerShell, quote the full CMake definition to preserve dotted numbers:

   ```powershell
   $releaseVersion = (Get-Content -LiteralPath VERSION -Raw).Trim()
   & 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build "-DSNESRECOMP_BUILD_VERSION=$releaseVersion"
   ```

3. Build and perform checks appropriate to the actual changes, then commit.
4. Package both music variants with `tools/make_release.py`, omitting
   `--label`, and passing the reviewed editable course set with
   `--packs captures/reconstructed-projects-03/packs`. Keep its extraction
   manifests for the pack parity check; do not package the installed cache.
   Its version checks require the cache, executable and `VERSION`
   to agree. A fix release produces filenames such as
   `FZeroSNESRecomp-0.1.11-windows-x64-with-msu.zip` and
   `FZeroSNESRecomp-0.1.11-windows-x64-without-msu.zip`.

5. Check a relocated player ZIP with `tests/validate_release_cache.py --build
   build --bundle <without-msu.zip> --out <fresh-validation-directory>`. This
   verifies first-launch cache reuse and invalidation after editing a project.

The Linux build script also reads `VERSION`; do not override it with an old
preview version. Keep all already distributed bundles unchanged.
