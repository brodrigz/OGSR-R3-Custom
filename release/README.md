# Radiophobia Unofficial Patch release tooling

The maintained runtime resource source is `Game/Resources_SoC_1.0006`.
Despite its historical name, it targets Radiophobia 3 1.20, not vanilla SoC.
See the [resource guide](../Game/Resources_SoC_1.0006/README.md) for editing
and resource-selection rules.

`Publish-RadiophobiaDropIn.ps1` creates a root-mirroring ZIP for extraction
into a backed-up clean Radiophobia 3 1.20 installation. It copies:

- `bin_x64/xrEngine.exe` and NVIDIA's DLSS 310.9.1 retail `nvngx_dlss.dll`;
- every runtime shader from the Game resource tree (formatter metadata excluded);
- the non-shader paths in `Game/Resources_SoC_1.0006/radiophobia-release-files.txt`.

Edit the Game resource tree to change a release. Packaging copies it directly.

```powershell
.\release\Publish-RadiophobiaDropIn.ps1 `
  -OutputDirectory '.\release\output' `
  -Version 'YOUR-NEW-VERSION'
```

`-ResourceRoot` selects an alternative complete resource tree, including its
release file list.
`-EngineBinaryPath` and `-DlssRuntimePath` can select built runtime files.

`Update_Components.cmd` pins the retail DLSS DLL to NVIDIA's `v310.9.1` release
while retaining the existing integration SDK. Release builds copy that DLL to
`bin_x64`, where the publisher picks it up.

The output is `radiophobia-unofficial-patch-VERSION.zip` with a checksum beside
it. The bundled `README-RADIOPHOBIA-UNOFFICIAL-PATCH.md` identifies the patch,
version, source commit, and OGSR engine series. `meta.ini` provides the version
and patch description for MO2. `-EngineSeries` records the underlying OGSR
version in the README; it is separate from the patch version.

The publisher refuses to overwrite existing artifacts, checks engine freshness
against renderer, gameplay, common AI, and core engine C++ sources, and checks those sources plus resource and release
sources for uncommitted changes. Use `-AllowDirty` for intentional working-tree
validation and `-AllowStaleEngine` only when deliberately overriding freshness.
These checks do not replace compiling and testing engine changes.

Before compression it validates the actual staged Radiophobia UI, script/XML
contracts, custom textures, English/Russian strings, gameplay integration,
renderer presets, and required shader bindings. An omitted required resource
fails validation even if it exists elsewhere in the repository.

The ZIP also contains `RESOURCE-REPORT.txt` and `SHA256SUMS.txt`.
Resources retain their source bytes.
Optional add-ons, saves, logs, PDBs, and development tools are not packaged.

## Verification

```powershell
.\tests\Test-RadiophobiaRelease.ps1
.\ogsr_engine\LuaJIT\bin\x64\Lua_JIT.exe .\tests\Test-RadiophobiaLaser.lua .\Game\Resources_SoC_1.0006
.\tests\Test-RadiophobiaRelease.ps1 -ArchivePath '.\release\output\YOUR-ARCHIVE.zip'
# Optionally compare runtime paths and bytes between two packages:
.\tests\Test-RadiophobiaRelease.ps1 `
  -ArchivePath 'AFTER.zip' -BaselineArchivePath 'BEFORE.zip'
```

Publish a new version to distribute current resources. No game installation is
needed to run these checks; visual and gameplay changes also need in-game testing.

Release scripts and documentation are eligible for Git tracking. Generated
archives and staging directories remain ignored. Commit the
resource tree, file list, release tools, and tests together when releasing.

## Optional add-ons

Seb's Pack and Atmospherics compatibility patches are maintained in the sibling
`Addons` repository. Each patch has its own sources and `Build.ps1` for creating
an MO2 package; the engine publisher does not require that repository.

For MO2 launcher installation and engine-mod switching, see the
[plugin guide](mo2-plugin/README.md).

## Night-vision diagnostics

To capture a frame, activate NVG, wait for the animation to finish, enter
`r_pnv_debug 1`, and close the console. The command resets itself after one frame.
Inspect `[NVDBG E1]` entries in the game log for mode, postprocess parameters,
texture dimensions/formats, and thirteen pixel samples before NVG, after NVG,
after rain, after combine, and after final postprocess. The readback briefly
synchronizes with the GPU only when requested. An unknown command indicates
that the active executable predates the instrumentation.

For shader inspection, `tests/New-NightVisionDiagnostic.ps1` generates an optional
MO2 diagnostic ZIP from current resources. Enable it with highest priority for
the test and disable it afterwards. It displays twelve numbered tiles and a cyan
border; the engine samples their centers and the upper-left pixel.
`tests/Test-NightVisionDiagnostic.lua` checks its logging and shader binding.
Gameplay diagnostics use `log1`; the shader VM uses native `log`.

`tests/Test-PostprocessTextureRemap.ps1` checks the engine's texture-cache and
driver-binding synchronization through repeated scene-target swaps, unrelated
texture preservation, binding restoration, and explicit unbinding.
