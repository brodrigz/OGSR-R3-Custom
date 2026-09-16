# Radiophobia runtime resources

This is the authoritative game-content source for OGSR R3 Custom. Its historical
directory name is retained for existing tooling; the supported target is
Radiophobia 3 1.20. Maintaining vanilla SoC compatibility is not a requirement.

Edit shipped UI, scripts, translations, presets, shaders, and supporting assets
here. Do not keep parallel maintained copies under `release/`. Engine capability
remains in C++; Radiophobia feature policy belongs in these resources, as described
in `../../R3_CUSTOMIZATION.md` (at the repository root).

## What ships

`radiophobia-release-files.txt` explicitly selects non-shader files relative to
this directory. Add a path there when adding a new runtime dependency. Every
runtime file under `gamedata/shaders` is included automatically, except formatter
metadata. `mods/radiophobia_scope_texture_compat.xsq` ships intact as an archive.

Some inherited OGSR resources are retained outside that selection for reference
and future integration. They are not part of the supported R3 payload. Do not
copy this entire directory into a game installation: use the publisher, which
selects the required files and validates the staged result. The original game
archives provide the remaining base content, including unchanged scripts/assets.
This is an upgrade resource tree, not a complete extracted game.

Menu scripts are maintained under `gamedata/scripts/ui/`. The five superseded
flat copies were removed to avoid duplicate Lua namespace definitions.

## Preserving upgraded engine behavior

The 2026-09-15 consolidation materialized the corrected publisher's output into
this tree, rather than importing an old installation wholesale. It retains the
current engine's shaders, including XeGTAO, temporal resolve, upscalers, and tracer
changes, alongside Radiophobia UI and presets.

Two shaders previously excluded from live-source replacement retain their exact
published Radiophobia-compatible versions:

- `gamedata/shaders/r3/models_lfo_black_lens_weapons.s`
- `gamedata/shaders/r3/models_lfo_black_soft_lens_weapons.s`

Four baseline-only shaders were brought into source: `copy.ps`, `copy_p.ps`,
`copy_nomsaa.ps`, and `copy_p_nomsaa.ps` under `gamedata/shaders/r3/`. The five
`rspec_*.ltx` files retain Radiophobia's preset values. There is no hidden exclude
list or older shader tree overriding future edits at package time.

When porting future upstream changes, compare against this maintained content
and check the engine/resource contract. An older Radiophobia installation is a
reference for appearance and behavior, not a replacement for upgraded resources.

## Build and verify

From the repository root, run `release/Publish-RadiophobiaDropIn.ps1` with an
output directory and a new version. Use `-AllowDirty` for local working-tree
validation. Run `tests/Test-RadiophobiaRelease.ps1` for UI regression checks and
pass `-ArchivePath` to verify the ZIP, checksums, and exact resource selection.
`-BaselineArchivePath` additionally proves runtime parity across packaging changes.

See `RADIOPHOBIA_FEATURES.md` for the feature inventory and `release/README.md`
at the repository root for publishing details.
