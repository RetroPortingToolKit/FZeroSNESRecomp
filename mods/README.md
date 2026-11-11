# Install F-Zero Forever packs

Put a folder or ZIP containing `pack.json` in `mods/packs`, then restart.
Enable **Track Pack Loader** in Mods. Everything valid in that directory is
installed together; remove a pack to exclude it. BS Satellaview Tracks cannot
be enabled alongside the loader. Vehicle options remain independent.

The included packs contain 95 additional course versions in 19 cups:
CGP (55), Astra Front (10), Astra Velocity (20), MAX (5), and Bower (5).
Together with the original 15, that is 110 selectable versions in 22 cups.
CGP includes revised originals and
corrected BS courses; these are not 55 entirely new courses.

Enable MSU-1 in Audio settings to use installed music packs. The with-msu
edition includes approved CGP and Astra Front recordings. Astra Velocity
uses its authored SNES selections until separate recordings are supplied.
Missing songs use the course's SNES soundtrack.

See [Pack format](../docs/PACK_FORMAT.md) for JSON examples and raw FZEdit files,
and [MODS.md](../MODS.md) for differences in course mechanics.

## For maintainers and LLMs

Use the same public pack path for bundled and third-party content. Preserve
stable IDs, authored names, cup order, SPC music and scoped mechanic requirements.
Use the reviewed extraction tools for IPS/BPS inputs when editor source is
unavailable; do not claim reconstructed files are the original author project.
Do not copy arbitrary ASM into packs. Qualify unknown mechanics before adding
an engine capability. Validate GP, Practice, records and missing-MSU fallback.
Keep ROMs, editor inspection material, audio and generated payloads out of Git.
See [qualification](PARSE_MANIFEST.md) and [format](../docs/PACK_FORMAT.md).
