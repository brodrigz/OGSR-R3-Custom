# Radiophobia release tooling

The maintained runtime resource source is `Game/Resources_SoC_1.0006`.
Despite its historical name, it targets Radiophobia 3 1.20, not vanilla SoC.
See that directory's README for editing and resource-selection rules.

`Publish-RadiophobiaDropIn.ps1` creates a root-mirroring ZIP for extraction
into a backed-up clean Radiophobia 3 1.20 installation. It copies:

- `bin_x64/xrEngine.exe` and NVIDIA's `nvngx_dlss.dll`;
- every runtime shader from the Game resource tree (formatter metadata excluded);
- the non-shader paths in `Game/Resources_SoC_1.0006/radiophobia-release-files.txt`.

There is no packaging-time compatibility overlay or shader-baseline merge.
Do not edit files in old ZIPs or local migration backups to change a release.

```powershell
.\release\Publish-RadiophobiaDropIn.ps1 `
  -OutputDirectory '.\release\output' `
  -Version 'YOUR-NEW-VERSION'
```

`-ResourceRoot` selects an alternative complete resource tree, including its
release file list. It replaces the former `-CompatibilityRoot`,
`-RendererResourceRoot`, `-LiveGamedataRoot`, and `-LiveShaderExcludePath`
parameters; those split-source parameters are no longer accepted.
`-EngineBinaryPath` and `-DlssRuntimePath` can select built runtime files.

The publisher refuses to overwrite existing artifacts, checks engine freshness
against renderer C++ sources, and checks watched renderer, resource, and release
sources for uncommitted changes. Use `-AllowDirty` for intentional working-tree
validation and `-AllowStaleEngine` only when deliberately overriding freshness.
These checks do not replace compiling and testing engine changes.

Before compression it validates the actual staged Radiophobia UI, script/XML
contracts, custom textures, English/Russian strings, gameplay integration,
renderer presets, and required shader bindings. An omitted required resource
fails validation even if it exists elsewhere in the repository.

The ZIP contains `RESOURCE-REPORT.txt`, source provenance, and `SHA256SUMS.txt`;
an archive checksum is written beside it. Resources retain their source bytes.
Optional add-ons, saves, logs, PDBs, and development tools are not packaged.

## Verification

```powershell
.\tests\Test-RadiophobiaRelease.ps1
.\tests\Test-RadiophobiaRelease.ps1 -ArchivePath '.\release\output\YOUR-ARCHIVE.zip'
# For a packaging-only migration, also compare every runtime path and byte:
.\tests\Test-RadiophobiaRelease.ps1 `
  -ArchivePath 'AFTER.zip' -BaselineArchivePath 'BEFORE.zip'
```

The consolidation baseline is the corrected UI package, not the old 1.1.5 ZIP.
Local before/after artifacts and retired sources are in the ignored
`.resource-migration-validation` directory. They are not publisher inputs.
Existing published ZIPs remain unchanged; publish a new version to distribute
current resources. No game installation is needed to run these checks.

Release scripts and documentation are eligible for Git tracking. Generated
archives, staging directories, and local backups remain ignored. Commit the
resource tree, file list, release tools, and tests together when releasing.

## Optional add-ons

Seb's Pack and Atmospherics remain separate packages maintained under
`D:\OGSR Fork\Addons`; the engine release does not require that local tree.
