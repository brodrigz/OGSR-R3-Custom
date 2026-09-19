[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$resources = Join-Path $repo 'Game\Resources_SoC_1.0006\gamedata'
$stage = Join-Path $repo 'release\nvg-diagnostic-2'
$shaderDir = Join-Path $stage 'gamedata\shaders\r3'
$scriptDir = Join-Path $stage 'gamedata\scripts\ogse'
New-Item -ItemType Directory -Force -Path $shaderDir, $scriptDir | Out-Null

function Replace-Once([string]$Text, [string]$Before, [string]$After) {
    $start = $Text.IndexOf($Before, [StringComparison]::Ordinal)
    if ($start -lt 0 -or $Text.IndexOf($Before, $start + $Before.Length, [StringComparison]::Ordinal) -ge 0) {
        throw "Expected one diagnostic insertion point: $Before"
    }
    return $Text.Substring(0, $start) + $After + $Text.Substring($start + $Before.Length)
}

# Generate from the release shader so the failing calculations stay intact.
$shader = [IO.File]::ReadAllText((Join-Path $resources 'shaders\r3\ogsr_nightvision.ps'))
$shader = Replace-Once $shader 'float4 main(p_screen I) : SV_Target' @'
// Magenta means a NaN or infinity, blue means negative data.
float4 nvdbg_value(float3 value)
{
    if (any((asuint(value) & 0x7f800000u) == 0x7f800000u)) return float4(1, 0, 1, 1);
    if (any(value < 0)) return float4(0, 0.2, 1, 1);
    return float4(saturate(value), 1);
}

float4 nvdbg_stage(p_screen I, uint stage)
'@
$shader = Replace-Once $shader '    I.tc0 = applyInertiaDev(I.tc0);' @'
    if (stage == 0) return nvdbg_value(s_image.Sample(smp_rtlinear, I.tc0).rgb);
    I.tc0 = applyInertiaDev(I.tc0);
    if (any((asuint(I.tc0) & 0x7f800000u) == 0x7f800000u)) return float4(1, 0, 1, 1);
    if (stage == 1) return nvdbg_value(s_blur_2.Sample(smp_rtlinear, I.tc0).rgb);
    if (stage == 2) return nvdbg_value(s_blur_8.Sample(smp_rtlinear, I.tc0).rgb);
    if (stage == 3) return nvdbg_value((blurred_depth(I.tc0) / 100.0f).xxx);
    if (stage == 4) return nvdbg_value(compute_lens_mask(aspect_ratio_correction(I.tc0), pnv_param_4.x).xxx);
    if (stage == 9) return nvdbg_value(calc_vignette(pnv_param_4.x, I.tc0, pnv_param_2.z).xxx);
    // Four bands: GPU color, gain/vignette/tubes, radius/flip/mode, inertia.
    if (stage == 11)
    {
        if (I.tc0.y < 0.25) return nvdbg_value(pnv_color.rgb);
        if (I.tc0.y < 0.50) return nvdbg_value(float3(pnv_param_2.y / 3, pnv_param_2.z, pnv_param_4.x / 4));
        if (I.tc0.y < 0.75) return nvdbg_value(float3(pnv_param_1.y, pnv_param_1.x / 100, pnv_param_1.z));
        return nvdbg_value(abs(m_cam_inertia_smooth.xyz * device_inertia));
    }
'@
$shader = Replace-Once $shader '        // GLITCH EFFECT -- TO DO' @'
        if (stage == 5) return nvdbg_value(image);
        // GLITCH EFFECT -- TO DO
'@
$shader = Replace-Once $shader '        // APPLY CRT EFFECT' @'
        if (stage == 6) return nvdbg_value(image);
        // APPLY CRT EFFECT
'@
$shader = Replace-Once $shader '        // APPLY NOISE' @'
        if (stage == 7) return nvdbg_value(image);
        // APPLY NOISE
'@
$shader = Replace-Once $shader '        // APPLY VIGNETTE' @'
        if (stage == 8) return nvdbg_value(image);
        // APPLY VIGNETTE
'@
$shader += @'

// Each tile evaluates the whole scene, with decimal labels 1 through 12.
uint nvdbg_digit(uint digit)
{
    if (digit == 0) return 31599;
    if (digit == 1) return 29850;
    if (digit == 2) return 29671;
    if (digit == 3) return 31207;
    if (digit == 4) return 18925;
    if (digit == 5) return 31183;
    if (digit == 6) return 31695;
    if (digit == 7) return 18727;
    if (digit == 8) return 31727;
    return 31215;
}

float4 main(p_screen I) : SV_Target
{
    float2 grid = min(I.tc0 * float2(4, 3), float2(3.99999, 2.99999));
    uint stage = (uint)grid.x + 4 * (uint)grid.y;
    float2 local = frac(grid);
    I.tc0 = local;
    float4 result = nvdbg_stage(I, stage);
    // Cyan frame survives even when the NV calculations return black.
    if (local.x < 0.008 || local.y < 0.008) return float4(0, 1, 1, 1);
    uint2 pixel = (uint2)(local * float2(80, 60));
    if (pixel.x < 11 && pixel.y < 8)
    {
        uint number = stage + 1;
        uint digit = pixel.x < 5 ? number / 10 : number % 10;
        uint x = pixel.x < 5 ? pixel.x : pixel.x - 5;
        bool ink = false;
        if (x >= 1 && x <= 3 && pixel.y >= 1 && pixel.y <= 5)
            ink = ((nvdbg_digit(digit) >> ((pixel.y - 1) * 3 + x - 1)) & 1) != 0;
        return ink ? float4(1, 1, 1, 1) : float4(0, 0.12, 0.12, 1);
    }
    return result;
}
'@
[IO.File]::WriteAllText((Join-Path $shaderDir 'ogsr_nightvision_diagnostic_d2.ps'), $shader)

# Carry exact helper sources so addon priority cannot mix different versions.
foreach ($name in @('night_vision.h', 'ogsr_gasmask_common.h')) {
    Copy-Item -LiteralPath (Join-Path $resources "shaders\r3\$name") -Destination $shaderDir -Force
}
# The shader VM has its own native log function, independent of _g.script.
# A unique pixel-shader name makes a winning diagnostic binding visible in the
# compile log and cache, even if a later postprocess erases the colored grid.
$binding = [IO.File]::ReadAllText((Join-Path $resources 'shaders\r3\ogsr_nightvision.s'))
$binding = Replace-Once $binding '"ogsr_nightvision")' '"ogsr_nightvision_diagnostic_d2")'
$binding = Replace-Once $binding 'function element_0(shader, t_base, t_second, t_detail)' @'
function element_0(shader, t_base, t_second, t_detail)
    log("[NVDBG D2] renderer binding loaded: ogsr_nightvision_diagnostic_d2")
'@
[IO.File]::WriteAllText((Join-Path $shaderDir 'ogsr_nightvision.s'), $binding)

# Byte-preserving edits: comments in this legacy Lua file can use a codepage.
$encoding = [Text.Encoding]::GetEncoding(28591)
$script = [IO.File]::ReadAllText((Join-Path $resources 'scripts\ogse\ogse_night_vision.script'), $encoding)
$script = Replace-Once $script 'function attach(sm)' @'
function attach(sm)
    log1("[NVDBG D2] diagnostic script attached; NV will display 12 numbered tiles")
'@
$script = Replace-Once $script '    r_pnv_gain_current_base = get_float(nv_sect, "r_pnv_gain_current", beef_nv.r_pnv_gain_current)' @'
    r_pnv_gain_current_base = get_float(nv_sect, "r_pnv_gain_current", beef_nv.r_pnv_gain_current)
    log1(string.format("[NVDBG D2] profile=%s color=%.3f,%.3f,%.3f", tostring(nv_sect), color.x, color.y, color.z))
    for key, default in pairs(beef_nv) do
        log1(string.format("[NVDBG D2] %s=%s", key, tostring(get_float(nv_sect, key, default))))
    end
'@
[IO.File]::WriteAllText((Join-Path $scriptDir 'ogse_night_vision.script'), $script, $encoding)

@'
[General]
gameName=S.T.A.L.K.E.R.: Radiophobia 3
modid=0
version=2.0
comments=Temporary NVG shader stage diagnostics. Enable after RC8 and both compatibility patches; disable after testing.
'@ | Set-Content -LiteralPath (Join-Path $stage 'meta.ini') -Encoding utf8

@'
NVG diagnostic D2 (for engine upgrade RC8)

Disable diagnostic D1. Install D2 as a separate MO2 mod with the highest numeric
Priority in the left pane so it wins conflicts over RC8 and both compat patches.
The Data tab entry gamedata/shaders/r3/ogsr_nightvision.s must show D2 as its mod.
Keep RC8 enabled. Restart the game, load the save, and switch NVG off/on.
No engine rebuild or console commands are needed.

Expected: cyan-bordered grid with 12 numbered tiles. Screenshot it after the
activation animation finishes. The log must contain [NVDBG D2] for both the
renderer binding and the gameplay script; profile parameters follow NV activation.
The compiled shader/cache is now named ogsr_nightvision_diagnostic_d2, which
distinguishes it from the RC8 shader even when the image is black.
If that binding marker appears but the grid is black, investigate NV pass
activation and the downstream postprocessing before changing NV shader maths.

Tiles, left to right:
  1 scene before inertia | 2 half blur | 3 eighth blur | 4 blurred depth / 100
  5 lens mask | 6 near blur + attenuation | 7 bloom | 8 CRT blend
  9 noise + tint | 10 vignette | 11 original final output | 12 GPU parameters

Magenta marks NaN/infinite values. Blue marks negative values. Black is zero.
Tile 12 bands: color; gain/3, vignette, tubes/4; radius, flip/100, mode; inertia.
Tiles after the lens test are black outside the lens according to the profile.
Disable this mod after the test to restore RC8. Do not use it for normal play.
'@ | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding utf8

$zip = Join-Path $repo 'release\radiophobia-nvg-diagnostic-2-mo2.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
Write-Output $zip
