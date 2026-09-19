# Radiophobia Unofficial Patch resources

This is the authoritative game-content source for Radiophobia Unofficial Patch. Its historical
directory name is retained for existing tooling; the supported target is
Radiophobia 3 1.20. Maintaining vanilla SoC compatibility is not a requirement.

Edit shipped UI, scripts, translations, presets, shaders, and supporting assets
here. Do not keep parallel maintained copies under `release/`. Engine capability
remains in C++; Radiophobia feature policy belongs in these resources, as described
in [R3_CUSTOMIZATION.md](../../R3_CUSTOMIZATION.md).

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

Menu scripts are maintained under `gamedata/scripts/ui/`; avoid duplicate Lua
namespace definitions in the top-level scripts directory.

## Preserving upgraded engine behavior

This tree includes the current engine's XeGTAO, temporal resolve, upscalers, and
tracer shaders alongside Radiophobia UI and presets. Packaging uses these files
directly, without merging another shader tree or compatibility overlay.

When porting future upstream changes, compare against this maintained content
and check the engine/resource contract. An older Radiophobia installation is a
reference for appearance and behavior, not a replacement for upgraded resources.

## Build and verify

From the repository root, run `release/Publish-RadiophobiaDropIn.ps1` with an
output directory and a new version. Use `-AllowDirty` for local working-tree
validation. Run `tests/Test-RadiophobiaRelease.ps1` for UI regression checks and
pass `-ArchivePath` to verify the ZIP, checksums, and exact resource selection.
`-BaselineArchivePath` additionally proves runtime parity across packaging changes.

See [PATCH_NOTES.md](../../PATCH_NOTES.md) for release changes and
[release/README.md](../../release/README.md) for publishing details.
