param(
    [string]$Fxc = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe'
)

$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$shaderPath = Join-Path $repoPath 'Game/Resources_SoC_1.0006/gamedata/shaders/r3'
$outputPath = Join-Path $repoPath 'ogsr_engine/_TEMP/ssgi_validation'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

foreach ($file in Get-ChildItem -LiteralPath $shaderPath -Filter 'ogsr_ssgi*' -File) {
    foreach ($match in [regex]::Matches((Get-Content -LiteralPath $file.FullName -Raw), '#include\s+"([^"]+)"')) {
        if ($match.Groups[1].Value.Contains('/')) { throw "Invalid VFS include in $($file.Name)" }
    }
}

$script:compiledCount = 0
function Compile-Shader([string]$Name, [string]$Profile, [string[]]$Defines, [string]$Variant) {
    $shaderArgs = @('/nologo', '/T', $Profile, '/E', 'main', '/O3', '/Zpr', '/I', $shaderPath)
    foreach ($define in $Defines) { $shaderArgs += @('/D', $define) }
    $shaderArgs += @('/Fo', (Join-Path $outputPath "$Name-$Variant.cso"), (Join-Path $shaderPath $Name))
    $compilerOutput = & $Fxc @shaderArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Failed: $Name ($Variant)`n$compilerOutput" }
    $script:compiledCount++
}

foreach ($name in @('evaluate', 'temporal', 'filter', 'resolve')) {
    Compile-Shader "ogsr_ssgi_$name.cs" 'cs_5_0' @('USE_SSGI=1') 'ssgi'
}

# Exercise every affected accumulation entry, including include-based variants.
$accumulation = @('accum_base', 'accum_omni_normal', 'accum_omni_unshadowed',
    'accum_spot_fullsize', 'accum_spot_normal', 'accum_spot_unshadowed',
    'accum_sun_cascade0', 'accum_sun_cascade1', 'accum_sun_cascade2',
    'accum_emissive', 'accum_emissive_glowo', 'accum_emissivel', 'accum_lamp')
foreach ($enabled in 0..1) {
    $defines = @('SMAP_size=2048', 'SUN_QUALITY=2', 'USE_HWSMAP=1', 'USE_HWSMAP_PCF=1')
    if ($enabled) { $defines += 'USE_SSGI=1' }
    foreach ($name in $accumulation) {
        Compile-Shader "$name.ps" 'ps_5_0' $defines "enabled-$enabled"
    }
    foreach ($mode in @('off', 'ssdo', 'gtao', 'xegtao', 'bent')) {
        $variantDefines = @($defines)
        if ($mode -ne 'off') { $variantDefines += 'SSAO_QUALITY=3' }
        if ($mode -eq 'gtao') { $variantDefines += 'USE_GTAO=1' }
        if ($mode -eq 'bent') { $variantDefines += 'USE_XEGTAO_BENT_NORMALS=1' }
        foreach ($name in @('combine_1', 'combine_1_ao')) {
            Compile-Shader "$name.ps" 'ps_5_0' $variantDefines "enabled-$enabled-$mode"
        }
        if ($enabled) {
            foreach ($name in @('ogsr_ssgi_source', 'ogsr_ssgi_source_ao')) {
                Compile-Shader "$name.ps" 'ps_5_0' $variantDefines $mode
            }
        }
    }
    Compile-Shader 'combine_1_ao.ps' 'ps_5_0' ($defines + @('SSAO_QUALITY=3', 'USE_XEGTAO_BENT_NORMALS=1', 'SSFX_FOG=1', 'SSFX_BEEFS_NVG=1')) "enabled-$enabled-fog-nvg"
    Compile-Shader 'combine_2_naa.ps' 'ps_5_0' ($defines + @('SSFX_INDIRECT_LIGHT=1')) "enabled-$enabled-legacy"
}
Compile-Shader 'ogsr_ssgi_debug.ps' 'ps_5_0' @('USE_SSGI=1') 'ssgi'
Write-Output "PASS: $script:compiledCount SSGI compute, light accumulation, AO composition and legacy shader variants."
