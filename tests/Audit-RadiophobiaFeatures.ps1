[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CleanRoot,
    [Parameter(Mandatory)][string]$ExtractedGamedataRoot,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../release/.r3-feature-audit/report'),
    [switch]$FailOnFindings
)

# A static audit of the effective clean-install + selected release resources.
# Does not launch or modify the reference game, and does not prove runtime parity.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$clean = (Resolve-Path -LiteralPath $CleanRoot).Path
$base = (Resolve-Path -LiteralPath $ExtractedGamedataRoot).Path
$resources = Join-Path $repo 'Game/Resources_SoC_1.0006'
. (Join-Path $repo 'release/Get-RadiophobiaPayload.ps1')
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path

function Remove-LuaComments([string]$Text) {
    $Text = [regex]::Replace($Text, '(?s)--\[\[.*?\]\]', '')
    return [regex]::Replace($Text, '(?m)--[^\r\n]*', '')
}
function Add-Layer([hashtable]$Map, [string]$Root, [string]$Layer) {
    foreach ($subdir in @('scripts', 'config')) {
        foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root $subdir) -Recurse -File) {
            $relative = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
            $Map[$relative] = [pscustomobject]@{ Path = $relative; Source = $file.FullName; Layer = $Layer }
        }
    }
}
function Get-Addons([string]$Path) {
    $text = Remove-LuaComments ([IO.File]::ReadAllText($Path))
    $table = [regex]::Match($text, '(?s)\baddons\s*=\s*\{(.*?)\}')
    if (-not $table.Success) { throw "Cannot find static add-on registry: $Path" }
    return @([regex]::Matches($table.Groups[1].Value, '"([\w]+)"') | ForEach-Object { $_.Groups[1].Value })
}
$reference = @{}
Add-Layer $reference $base 'clean archive'
Add-Layer $reference (Join-Path $clean 'gamedata') 'clean loose'
$effective = $reference.Clone()
foreach ($file in Get-RadiophobiaPayload -ResourceRoot $resources) {
    if ($file.RelativePath -notmatch '^gamedata/(scripts|config)/') { continue }
    $relative = $file.RelativePath.Substring('gamedata/'.Length)
    $effective[$relative] = [pscustomobject]@{ Path = $relative; Source = $file.Source; Layer = 'fork payload' }
}
$scripts = @($effective.Values | Where-Object Path -Like 'scripts/*.script')
$namespaces = @{}
foreach ($file in $scripts) {
    $name = [IO.Path]::GetFileNameWithoutExtension($file.Path).ToLowerInvariant()
    if (-not $namespaces.ContainsKey($name)) { $namespaces[$name] = @() }
    $namespaces[$name] += $file
}
$registry = 'scripts/ogse/ogse_signals_addons_list.script'
$oldAddons = @(Get-Addons $reference[$registry].Source)
$newAddons = @(Get-Addons $effective[$registry].Source)
$findings = [Collections.Generic.List[object]]::new()
function Add-Finding([string]$Severity, [string]$Feature, [string]$Evidence) {
    $findings.Add([pscustomobject]@{ Severity = $Severity; Feature = $Feature; Evidence = $Evidence })
}
foreach ($module in $newAddons) {
    if (-not $namespaces.ContainsKey($module)) {
        Add-Finding 'Blocker' $module 'Registered module has no effective script.'
        continue
    }
    foreach ($file in $namespaces[$module]) {
        $text = Remove-LuaComments ([IO.File]::ReadAllText($file.Source))
        if ($text -notmatch '\bfunction\s+(?:\w+\.)?attach\s*\(|\battach\s*=') {
            $severity = if ($module -in $oldAddons) { 'Inherited risk' } else { 'Registration risk' }
            Add-Finding $severity $module "$($file.Path) has no local attach definition; ogse_signals.subscribe_module requires one. Verify initialization/inherited globals before claiming a game crash."
        }
    }
}
foreach ($module in $oldAddons) {
    # Folopes' controller intentionally replaces the original BaS controller.
    # Exercise its behavior with Test-RadiophobiaLaser.lua, not this name scan.
    if ($module -eq 'zzz_bas_laser_control' -and 'rad_laser_control' -in $newAddons -and
        $namespaces.ContainsKey('rad_laser_control')) { continue }
    if ($module -notin $newAddons) { Add-Finding 'Parity gap' $module 'Enabled in clean R3, disabled in the fork registry; requires an equivalent replacement or restoration.' }
}
foreach ($name in $namespaces.Keys) {
    if ($namespaces[$name].Count -gt 1) {
        Add-Finding 'Maintenance' $name ('Multiple paths define this Lua namespace: ' + ($namespaces[$name].Path -join ', '))
    }
}

$scriptText = ($scripts | ForEach-Object { Remove-LuaComments ([IO.File]::ReadAllText($_.Source)) }) -join "`n"
$weapon = [IO.File]::ReadAllText((Join-Path $repo 'ogsr_engine/xrGame/Weapon.cpp'))
$exports = [IO.File]::ReadAllText((Join-Path $repo 'ogsr_engine/xrGame/script_game_object_script.cpp'))
if ($weapon.Contains('CWeapon_OnSwitchSightMode') -and
    $scriptText -notmatch 'function\s+(?:_G\.)?CWeapon_OnSwitchSightMode\s*\(|CWeapon_OnSwitchSightMode\s*=' -and
    $exports -notmatch 'value\("on_actor_weapon_alt_aim_switch"') {
    Add-Finding 'Parity gap' 'Alternate-aim fake-lens notification' 'Native sight switching exists, but its new hook has no effective script implementation and the old callback is not exported.'
}

$oldOptions = [xml]::new()
$defaults = [IO.File]::ReadAllText($effective['config/default_controls.ltx'].Source)
$baselineDefaults = [IO.File]::ReadAllText($reference['config/default_controls.ltx'].Source)
if ($baselineDefaults -match '(?m)^\s*bind\s+night_vision_rad\s+\S+' -and
    $defaults -notmatch '(?m)^\s*bind\s+night_vision_rad\s+\S+' -and
    $scriptText.Contains('key_bindings.kNIGHT_VISION_RAD')) {
    Add-Finding 'Input gap' 'Wearable night-vision default binding' 'The R3 handler requires night_vision_rad, but the selected defaults do not bind it. The native night_vision action is a different action, not an alias.'
}
$oldOptions.Load($reference['config/ui/ui_mm_opt.xml'].Source)
$newOptions = [xml]::new()
$newOptions.Load($effective['config/ui/ui_mm_opt.xml'].Source)
foreach ($entry in @('g_font_scale_x', 'g_font_scale_y')) {
    if ($oldOptions.SelectSingleNode("//options_item[@entry='$entry']") -and
        -not $newOptions.SelectSingleNode("//options_item[@entry='$entry']")) {
        Add-Finding 'UI gap' $entry 'Clean R3 offers this setting in Options; the fork still has the console command but no options control.'
    }
}
$oldPreferences = [regex]::Matches(
    [IO.File]::ReadAllText($reference['scripts/ui/ui_mm_opt_gameplay.script'].Source),
    'ui_data\.(?:load|save)\("([^"]+)"') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique
$newGameplay = [IO.File]::ReadAllText($effective['scripts/ui/ui_mm_opt_gameplay.script'].Source)
foreach ($id in $oldPreferences) {
    if (-not $newGameplay.Contains($id)) { Add-Finding 'UI gap' $id 'Original saved gameplay preference is absent from the current gameplay/HUD script.' }
}

$fileRows = foreach ($file in $effective.Values | Sort-Object Path) {
    $hash = (Get-FileHash -LiteralPath $file.Source).Hash
    $status = 'Inherited unchanged'
    if (-not $reference.ContainsKey($file.Path)) { $status = 'Added by fork' }
    elseif ($file.Layer -eq 'fork payload' -and $hash -ne (Get-FileHash -LiteralPath $reference[$file.Path].Source).Hash) { $status = 'Overridden by fork' }
    [pscustomobject]@{ Path = $file.Path; Layer = $file.Layer; Status = $status; SHA256 = $hash }
}
$fileRows | Export-Csv (Join-Path $output 'effective-content.csv') -NoTypeInformation
$findings | Export-Csv (Join-Path $output 'findings.csv') -NoTypeInformation
$summary = [pscustomobject]@{
    CleanRoot = $clean
    ReferenceExeSHA256 = (Get-FileHash -LiteralPath (Join-Path $clean 'bin_x64/xrEngine.exe')).Hash
    BaseArchiveSHA256 = (Get-FileHash -LiteralPath (Join-Path $clean 'gamedata.sq_base')).Hash
    EffectiveScripts = $scripts.Count
    EffectiveConfigFiles = @($effective.Keys | Where-Object { $_ -match '^config/.*\.(ltx|xml)$' }).Count
    BaselineActiveAddons = $oldAddons.Count
    CurrentActiveAddons = $newAddons.Count
    MissingOriginalGameplayPreferences = @($findings | Where-Object { $_.Feature -like 'rad3_*' }).Count
    Findings = $findings.Count
    RuntimeValidated = $false
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
$summary | Format-List
$findings | Format-Table Severity,Feature -AutoSize
if ($FailOnFindings -and $findings.Count) { throw 'Static parity audit found gaps; inspect findings.csv.' }
