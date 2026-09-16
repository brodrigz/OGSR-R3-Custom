# Resource consolidation verification — 2026-09-15

The corrected Radiophobia publisher was run before consolidation, then the new
publisher was run with the same engine and DLSS runtime. The after publication
used -AllowDirty because the resource migration is an uncommitted working-tree
change. Engine freshness was not bypassed.

- All 655 runtime paths under bin_x64, gamedata, and mods are byte-identical.
- The new package contains 653 selected Game resources: 568 shaders and 85
  explicitly selected non-shader resources, plus the two runtime binaries.
- All 658 manifest hashes and the archive checksum passed verification.
- Five UI regression cases and five resource-selection checks passed.
- No engine C++ changes, builds, game installations, or game launches were needed.

Only packaging metadata differs: the generated README records the new resource
source, and RESOURCE-REPORT.txt replaces SHADER-OVERLAY-REPORT.txt.

Before archive SHA-256: f4cc0d71ee734b00305fc329b821868ab185d90328955642fca5617e8ad90b63
After archive SHA-256: 0cac9a338939552fa6e9df1cc8aaf2870d71600496f53d164b9011e8ee9e65e9

Local evidence is in release/.resource-migration-validation (ignored by Git).
That directory also preserves the retired overlays, old shader exclusion list,
old publisher, and superseded flat menu scripts. None are active release inputs.
The migration comparison can be repeated using Test-RadiophobiaRelease.ps1 with
-ArchivePath and -BaselineArchivePath pointing to the after/before ZIPs.

The preserved shader exceptions and authoring rules are documented in
Game/Resources_SoC_1.0006/README.md. Existing 1.1.5 artifacts were not overwritten.
