param([string]$Fxc = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$shaderPath = Join-Path $repoPath 'Game/Resources_SoC_1.0006/gamedata/shaders/r3'
$outputPath = Join-Path $repoPath 'ogsr_engine/_TEMP/rsm_validation'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$script:count = 0
function Compile-Shader([string]$Name, [string]$Profile, [string[]]$Defines, [string]$Variant) {
    $compilerArgs = @('/nologo','/T',$Profile,'/E','main','/O3','/Zpr','/I',$shaderPath)
    foreach ($define in $Defines) { $compilerArgs += @('/D',$define) }
    $compilerArgs += @('/Fo',(Join-Path $outputPath "$Name-$Variant.cso"),(Join-Path $shaderPath $Name))
    $result = & $Fxc @compilerArgs 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Failed: $Name ($Variant)`n$result" }
    $script:count++
}
foreach ($name in @('evaluate','temporal','filter','resolve')) {
    Compile-Shader "ogsr_rsm_$name.cs" 'cs_5_0' @('USE_RSM=1') 'rsm'
}
Compile-Shader 'shadow_direct_terrain.ps' 'ps_5_0' @('USE_RSM=1','USE_HWSMAP=1') 'rsm'
foreach ($enabled in 0..1) {
    $defines = @('USE_HWSMAP=1','SMAP_size=2048','SUN_QUALITY=2')
    if ($enabled) { $defines += 'USE_RSM=1' }
    foreach ($name in @('base','base_aref','terrain','tree','tree_aref','tree_s','tree_s_aref')) {
        Compile-Shader "shadow_direct_$name.vs" 'vs_5_0' $defines "enabled-$enabled"
    }
    foreach ($skin in @('NONE','0','1','2','3','4')) {
        foreach ($name in @('model','model_aref')) {
            Compile-Shader "shadow_direct_$name.vs" 'vs_5_0' ($defines + "SKIN_$skin=1") "enabled-$enabled-skin-$skin"
        }
    }
    foreach ($name in @('base','base_aref')) {
        Compile-Shader "shadow_direct_$name.ps" 'ps_5_0' $defines "enabled-$enabled"
    }
    foreach ($mode in @('off','ssdo','gtao','bent')) {
        $variantDefines = @($defines)
        if ($mode -ne 'off') { $variantDefines += 'SSAO_QUALITY=3' }
        if ($mode -eq 'gtao') { $variantDefines += 'USE_GTAO=1' }
        if ($mode -eq 'bent') { $variantDefines += 'USE_XEGTAO_BENT_NORMALS=1' }
        foreach ($name in @('combine_1','combine_1_ao')) {
            Compile-Shader "$name.ps" 'ps_5_0' $variantDefines "enabled-$enabled-$mode"
        }
    }
    Compile-Shader 'combine_2_naa.ps' 'ps_5_0' $defines "enabled-$enabled"
}
Compile-Shader 'ogsr_rsm_debug.ps' 'ps_5_0' @('USE_RSM=1') 'rsm'
Write-Output "PASS: $script:count RSM capture, skinning, compute, composition and disabled shader variants."
