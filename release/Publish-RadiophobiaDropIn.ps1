[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$OutputDirectory,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$Version,
    [ValidatePattern('^\d+\.\d+$')][string]$EngineSeries = '3.548',
    [string]$ResourceRoot,
    [string]$EngineBinaryPath,
    [string]$DlssRuntimePath,
    [switch]$AllowDirty,
    [switch]$AllowStaleEngine
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Assert-RadiophobiaUI.ps1')
. (Join-Path $PSScriptRoot 'Get-RadiophobiaPayload.ps1')
if ($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw 'Version must be a filename-safe release identifier.'
}
if ([string]::IsNullOrWhiteSpace($ResourceRoot)) {
    $ResourceRoot = Join-Path $PSScriptRoot '..\Game\Resources_SoC_1.0006'
}
if ([string]::IsNullOrWhiteSpace($EngineBinaryPath)) {
    $EngineBinaryPath = Join-Path $PSScriptRoot '..\bin_x64\xrEngine.exe'
}
if ([string]::IsNullOrWhiteSpace($DlssRuntimePath)) {
    $DlssRuntimePath = Join-Path $PSScriptRoot '..\bin_x64\nvngx_dlss.dll'
}
$rendererSourceRoots = @(
    'ogsr_engine\Layers\xrRender',
    'ogsr_engine\Layers\xrRenderDX10',
    'ogsr_engine\Layers\xrRenderPC_R4'
)
$engineSourceRoots = $rendererSourceRoots + @(
    'ogsr_engine\xrGame', 'ogsr_engine\COMMON_AI', 'ogsr_engine\xr_3da'
)
$dirtyWatchPaths = $engineSourceRoots + @(
    'Game/Resources_SoC_1.0006', 'release', 'tests/Test-RadiophobiaRelease.ps1'
)

function Get-ExistingDirectory {
    param([string]$Path, [string]$Label)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Label is not an existing directory: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path.TrimEnd('\')
}

function Get-ExistingFile {
    param([string]$Path, [string]$Label)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is not an existing file: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Assert-ContainedPath {
    param([string]$Root, [string]$Path, [string]$Label)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $prefix = $Root.TrimEnd('\') + '\'
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes its expected root: $fullPath"
    }
    return $fullPath
}

function Add-Payload {
    param(
        [Collections.Generic.List[object]]$Records,
        [string]$StageRoot,
        [string]$Source,
        [string]$RelativePath
    )

    $destination = Assert-ContainedPath -Root $StageRoot -Path (Join-Path $StageRoot $RelativePath) -Label 'Payload destination'
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination -Force
    $Records.Add([pscustomobject]@{
        relative_path = $destination.Substring($StageRoot.Length).TrimStart('\')
        size_bytes = (Get-Item -LiteralPath $destination).Length
    })
    return $destination
}

function Get-GitOutput {
    param([string]$RepoRoot, [string[]]$Arguments)

    $previousErrorAction = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & git -C $RepoRoot @Arguments 2>&1
        return [pscustomobject]@{
            ExitCode = $LASTEXITCODE
            Output = @($output | ForEach-Object { "$_" })
        }
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
}

function Assert-CleanWatchedTree {
    param([string]$RepoRoot, [string[]]$RelativePaths, [switch]$AllowDirty)

    $result = Get-GitOutput -RepoRoot $RepoRoot -Arguments (@('status', '--porcelain', '--') + $RelativePaths)
    if ($result.ExitCode -ne 0) {
        if ($AllowDirty) {
            Write-Warning 'Git status failed for watched source paths; continuing because -AllowDirty was set.'
            return 'unknown (Git unavailable)'
        }
        throw 'Git status failed for watched release source paths. Pass -AllowDirty only if you intend to publish an unverifiable tree.'
    }

    $dirty = @($result.Output | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($dirty.Count -eq 0) {
        return 'clean'
    }
    if ($AllowDirty) {
        Write-Warning "Watched source paths contain uncommitted changes; packaging the working tree because -AllowDirty was set.`n$($dirty -join "`n")"
        return 'contains uncommitted changes in watched release source paths; packaged working tree'
    }
    throw @"
Refusing to publish with uncommitted release source changes:
$($dirty -join "`n")
Commit those files, or pass -AllowDirty to package the working tree explicitly.
"@
}

function Assert-EngineFreshness {
    param([string]$RepoRoot, [string]$EnginePath, [string[]]$RelativeRoots, [switch]$AllowStaleEngine)

    $engineTime = (Get-Item -LiteralPath $EnginePath).LastWriteTimeUtc
    $newest = $null
    foreach ($relativeRoot in $RelativeRoots) {
        $root = Join-Path $RepoRoot $relativeRoot
        if (-not (Test-Path -LiteralPath $root -PathType Container)) {
            throw "Engine source root is missing: $root"
        }
        $candidate = Get-ChildItem -LiteralPath $root -File -Recurse -Force |
            Where-Object { $_.Extension -in '.cpp', '.cxx', '.c', '.h', '.hpp' } |
            Sort-Object LastWriteTimeUtc |
            Select-Object -Last 1
        if ($candidate -and ((-not $newest) -or ($candidate.LastWriteTimeUtc -gt $newest.LastWriteTimeUtc))) {
            $newest = $candidate
        }
    }
    if (-not $newest) {
        throw 'No C++ sources were found for the engine freshness check.'
    }
    if ($newest.LastWriteTimeUtc -le $engineTime) {
        return
    }
    $message = "Engine binary is older than source $($newest.FullName) ($($newest.LastWriteTimeUtc.ToString('u')) > $($engineTime.ToString('u'))). Rebuild Release|x64 before publishing."
    if ($AllowStaleEngine) {
        Write-Warning $message
        return
    }
    throw $message
}

function Assert-CombinePostprocessBind {
    param([string]$CombinePath)

    $text = [IO.File]::ReadAllText($CombinePath)
    $element3 = [regex]::Match($text, '(?s)function\s+element_3\s*\(.*?(?=\r?\nfunction\s|\z)')
    if (-not $element3.Success) {
        throw "Staged combine.s is missing function element_3: $CombinePath"
    }
    if ($element3.Value -notmatch 'dx10texture\s*\(\s*"s_image"\s*,\s*"\$user\$postprocess0"\s*\)') {
        throw 'Staged combine.s element_3 does not bind s_image to $user$postprocess0. The live shader overlay is stale or incorrect.'
    }
    if ($element3.Value -match 'dx10texture\s*\(\s*"s_image"\s*,\s*"\$user\$generic0"\s*\)') {
        throw 'Staged combine.s element_3 still binds s_image to $user$generic0. Refusing to publish a pre-upscale final combine.'
    }
}

function Assert-RuntimeIntegration {
    param([string]$Root)

    $addons = [IO.File]::ReadAllText((Join-Path $Root 'gamedata\scripts\ogse\ogse_signals_addons_list.script'))
    if ($addons -notmatch '(?m)^\s*"rad_laser_control"' -or $addons -match '(?m)^\s*"zzz_bas_laser_control"') {
        throw 'The add-on list must register the adapted Folopes laser controller.'
    }
    $controls = [IO.File]::ReadAllText((Join-Path $Root 'gamedata\config\default_controls.ltx'))
    if ($controls -notmatch '(?m)^bind night_vision_rad kN\s*$' -or $controls -notmatch '(?m)^bind laser_on mouse5\s*$') {
        throw 'Default controls must retain the Radiophobia NVG and laser bindings.'
    }
    $nvShader = [IO.File]::ReadAllText((Join-Path $Root 'gamedata\shaders\r3\ogsr_nightvision.s'))
    if ($nvShader -notmatch 'dx10texture\("s_laser_scene", "\$user\$generic_temp"\)') {
        throw 'NVG laser visibility requires the pre-tonemap scene binding.'
    }
    if ($addons -notmatch '"rad_breath_indoors"') {
        throw 'The add-on list does not register rad_breath_indoors.'
    }
    if ($addons -notmatch 'hoc_backpack_inventory_anim\.script') {
        throw 'The add-on list does not detect Seb''s backpack script.'
    }
    if ($addons -match '(?m)^\s*"hoc_backpack_inventory_anim"') {
        throw 'The add-on list always registers hoc_backpack_inventory_anim; it must be conditional on Seb''s pack.'
    }
    if ($addons -match '(?m)^\s*"rad_qol_moves"') {
        throw 'The add-on list still registers rad_qol_moves; native QoL replaced that script.'
    }
    if ($addons -notmatch '"ogsr_hud_animation_callbacks"') {
        throw 'The add-on list does not register ogsr_hud_animation_callbacks.'
    }
    if ($addons -notmatch '"animation_common"') {
        throw 'The add-on list does not register animation_common.'
    }

    $hudCb = [IO.File]::ReadAllText((Join-Path $Root 'gamedata\scripts\ogsr_hud_animation_callbacks.script'))
    if ($hudCb -notmatch 'CHudItem__PlayHUDMotion') {
        throw 'ogsr_hud_animation_callbacks.script is missing CHudItem__PlayHUDMotion.'
    }
    $animCommon = [IO.File]::ReadAllText((Join-Path $Root 'gamedata\scripts\animation_common.script'))
    if ($animCommon -notmatch 'scripted_snd_') {
        throw 'animation_common.script does not map scripted_snd_ keys.'
    }
}

function Assert-RequiredStagedShaders {
    param([string]$StageRoot)

    $required = @(
        'gamedata\shaders\r3\combine.s',
        'gamedata\shaders\r3\temporal_resolve.s',
        'gamedata\shaders\r3\temporal_resolve.ps',
        'gamedata\shaders\r3\contrast_adaptive_sharpening.s'
    )
    foreach ($relative in $required) {
        $path = Join-Path $StageRoot $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required staged shader is missing: $relative"
        }
    }
    Assert-CombinePostprocessBind -CombinePath (Join-Path $StageRoot 'gamedata\shaders\r3\combine.s')
    $cas = [IO.File]::ReadAllText((Join-Path $StageRoot 'gamedata\shaders\r3\contrast_adaptive_sharpening.s'))
    if ($cas -notmatch 'dx10texture\s*\(\s*"t_current"\s*,\s*"\$user\$postprocess0"\s*\)') {
        throw 'Staged contrast_adaptive_sharpening.s does not sample $user$postprocess0.'
    }
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resources = Get-ExistingDirectory -Path $ResourceRoot -Label 'Radiophobia resource root'
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$engine = Get-ExistingFile -Path $EngineBinaryPath -Label 'Engine binary'
$dlssRuntime = Get-ExistingFile -Path $DlssRuntimePath -Label 'DLSS runtime'
$sourceTree = Assert-CleanWatchedTree -RepoRoot $repoRoot -RelativePaths $dirtyWatchPaths -AllowDirty:$AllowDirty
Assert-EngineFreshness -RepoRoot $repoRoot -EnginePath $engine -RelativeRoots $engineSourceRoots -AllowStaleEngine:$AllowStaleEngine
$resourceFiles = @(Get-RadiophobiaPayload -ResourceRoot $resources)

New-Item -ItemType Directory -Force -Path $output | Out-Null
$archiveName = "radiophobia-ogsr-$EngineSeries-engine-upgrade-$Version.zip"
$archivePath = Join-Path $output $archiveName
$checksumPath = "$archivePath.sha256"
foreach ($path in @($archivePath, $checksumPath)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite an existing release artifact: $path" }
}
$stage = Join-Path $output ('.' + [IO.Path]::GetFileNameWithoutExtension($archiveName) + '.staging-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    $payloadRecords = [Collections.Generic.List[object]]::new()
    $engineHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $engine).Hash
    Add-Payload -Records $payloadRecords -StageRoot $stage -Source $engine -RelativePath 'bin_x64\xrEngine.exe' | Out-Null
    Add-Payload -Records $payloadRecords -StageRoot $stage -Source $dlssRuntime -RelativePath 'bin_x64\nvngx_dlss.dll' | Out-Null
    foreach ($file in $resourceFiles) {
        Add-Payload -Records $payloadRecords -StageRoot $stage -Source $file.Source -RelativePath $file.RelativePath | Out-Null
    }
    # Validate exactly what will ship, not unselected legacy files in Game/.
    Assert-RuntimeIntegration -Root $stage
    Assert-RadiophobiaUI -Root $stage
    Assert-RequiredStagedShaders -StageRoot $stage
    foreach ($name in @('default', 'extreme', 'high', 'low', 'minimum')) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage "gamedata/config/rspec_$name.ltx") -PathType Leaf)) {
            throw "Required Radiophobia renderer preset is missing: rspec_$name.ltx"
        }
    }
    $sourceCommit = 'unknown (Git unavailable)'
    $commitResult = Get-GitOutput -RepoRoot $repoRoot -Arguments @('rev-parse', 'HEAD')
    if ($commitResult.ExitCode -eq 0 -and $commitResult.Output.Count -gt 0) { $sourceCommit = $commitResult.Output[0] }
    $shaderCount = @($resourceFiles | Where-Object RelativePath -Like 'gamedata/shaders/*').Count
    $report = @"
Resource root: $resources
Non-shader selection: radiophobia-release-files.txt
Runtime shaders: $shaderCount
Selected resources: $($resourceFiles.Count)
All resources are copied directly from the maintained Game resource tree.
No external compatibility overlay, frozen shader tree, or exclude list is used.
"@
    [IO.File]::WriteAllText((Join-Path $stage 'RESOURCE-REPORT.txt'), $report + "`r`n", [Text.UTF8Encoding]::new($false))
    $readme = @"
# Radiophobia OGSR $EngineSeries Engine Upgrade $Version

Extract this archive directly into a backed-up vanilla Radiophobia 3 1.20 game
folder and accept overwrite. The archive mirrors the game root.

Source commit reference: $sourceCommit
Source tree at package time: $sourceTree
Engine SHA-256: $engineHash

Game\Resources_SoC_1.0006 is this fork's maintained Radiophobia resource tree.
The release copies every runtime shader and the non-shader files selected by
radiophobia-release-files.txt. UI, scripts, translations, presets, and required
assets come from that same tree. Original R3 archives provide the remaining
base game content. The scope compatibility XSQ stays packed under mods.

Includes the engine and NVIDIA's retail DLSS runtime. Optional add-ons, saves,
logs, PDBs, and development tools are not included.
"@
    [IO.File]::WriteAllText((Join-Path $stage 'README-OGSR-R3-CUSTOM.md'), $readme, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE.md') -Destination (Join-Path $stage 'LICENSE-OGSR.md')
    $sumLines = Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
        "$( (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLowerInvariant() )  $($_.FullName.Substring($stage.Length).TrimStart('\'))"
    }
    [IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'), [string[]]$sumLines, [Text.UTF8Encoding]::new($false))
    $archiveInputs = @(Get-ChildItem -LiteralPath $stage -Force | ForEach-Object FullName)
    Compress-Archive -LiteralPath $archiveInputs -DestinationPath $archivePath -CompressionLevel Optimal
    $archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($checksumPath, "$archiveHash  $archiveName`r`n", [Text.UTF8Encoding]::new($false))
}
finally {
    if (Test-Path -LiteralPath $stage) {
        $stage = Assert-ContainedPath -Root $output -Path $stage -Label 'Staging cleanup'
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
}
Write-Host "Created end-user drop-in archive: $archivePath"
Write-Host "Created checksum: $checksumPath"
Write-Host "Resources: $($resourceFiles.Count) files, including $shaderCount shaders."
