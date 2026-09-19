[CmdletBinding()]
param([string]$ArchivePath, [string]$BaselineArchivePath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$release = Join-Path $repo 'release'
$root = Join-Path $repo 'Game/Resources_SoC_1.0006'
. (Join-Path $release 'Assert-RadiophobiaUI.ps1')
. (Join-Path $release 'Get-RadiophobiaPayload.ps1')
$payload = @(Get-RadiophobiaPayload -ResourceRoot $root)

$fixture = Join-Path $release ('.ui-test-' + [guid]::NewGuid().ToString('N'))
function Assert-Rejected {
    param([string]$Expected)
    try { Assert-RadiophobiaUI -Root $fixture }
    catch {
        if ($_.Exception.Message -notmatch $Expected) { throw }
        Write-Host "Rejected regression: $Expected"
        return
    }
    throw "UI validation accepted regression: $Expected"
}
try {
    foreach ($file in $payload) {
        $destination = Join-Path $fixture $file.RelativePath
        New-Item -ItemType Directory -Force -Path (Split-Path $destination) | Out-Null
        Copy-Item -LiteralPath $file.Source -Destination $destination
    }
    $manifestPath = Join-Path $fixture 'radiophobia-release-files.txt'
    $manifestText = [IO.File]::ReadAllText((Join-Path $root 'radiophobia-release-files.txt'))
    foreach ($case in @(
        @{ Line = 'gamedata/config/missing-release-resource.ltx'; Error = 'Required Radiophobia resource is missing' },
        @{ Line = 'gamedata/../../outside.ltx'; Error = 'Invalid or duplicate' },
        @{ Line = 'gamedata/config/default_controls.ltx'; Error = 'Invalid or duplicate' }
    )) {
        [IO.File]::WriteAllText($manifestPath, $manifestText + "`n" + $case.Line)
        $rejected = $false
        try { Get-RadiophobiaPayload -ResourceRoot $fixture | Out-Null }
        catch {
            if ($_.Exception.Message -notmatch $case.Error) { throw }
            $rejected = $true
        }
        if (-not $rejected) { throw "Accepted invalid resource selection: $($case.Line)" }
    }
    [IO.File]::WriteAllText($manifestPath, $manifestText)
    [IO.File]::WriteAllText((Join-Path $fixture 'gamedata/scripts/unselected-test.script'), '-- not selected')
    [IO.File]::WriteAllText((Join-Path $fixture 'gamedata/shaders/r3/new-test.ps'), '// automatically selected')
    $selection = @(Get-RadiophobiaPayload -ResourceRoot $fixture)
    if ($selection.RelativePath -contains 'gamedata/scripts/unselected-test.script' -or
        $selection.RelativePath -notcontains 'gamedata/shaders/r3/new-test.ps') {
        throw 'Resource selection failed to distinguish listed content from automatic shaders.'
    }
    Write-Host 'Resource selection checks passed (missing, traversal, duplicate, unlisted, new shader).'
    Assert-RadiophobiaUI -Root $fixture
    $options = "$fixture/gamedata/config/ui/ui_mm_opt.xml"
    $original = [IO.File]::ReadAllText($options)
    [IO.File]::WriteAllText($options, $original.Replace('ui\menu_video', 'ui\ui_static_mm_back_03'))
    Assert-Rejected 'Radiophobia menu background'
    [IO.File]::WriteAllText($options, [regex]::Replace($original, '(?s)<tab_hud>.*?</tab_hud>', ''))
    Assert-Rejected 'missing script-required node'
    [IO.File]::WriteAllText($options, $original)
    $xml = [xml]$original
    $node = $xml.SelectSingleNode('/window/video_adv/check_r_xegtao_bent_normals')
    [void]$node.ParentNode.RemoveChild($node)
    $xml.Save($options)
    Assert-Rejected 'video_adv:check_r_xegtao_bent_normals'
    [IO.File]::WriteAllText($options, $original)
    [IO.File]::WriteAllText($options, $original.Replace('entry="g_font_scale_x"', 'entry="removed_font_scale_x"'))
    Assert-Rejected 'Missing HUD font control: g_font_scale_x'
    [IO.File]::WriteAllText($options, $original)
    $keys = "$fixture/gamedata/config/ui/ui_keybinding.xml"
    $keyText = [IO.File]::ReadAllText($keys)
    [IO.File]::WriteAllText($keys, $keyText.Replace('exe="walk_toggle"', 'exe="removed_walk_toggle"'))
    Assert-Rejected 'missing walk_toggle'
    [IO.File]::WriteAllText($keys, $keyText)
    $strings = "$fixture/gamedata/config/text/rus/ui_st_ogsr_upscaler.xml"
    $stringText = [IO.File]::ReadAllText($strings)
    [IO.File]::WriteAllText($strings, $stringText.Replace('id="video_settings_desc_73"', 'id="removed_desc_73"'))
    Assert-Rejected 'Missing rus UI translation: video_settings_desc_73'
    [IO.File]::WriteAllText($strings, $stringText)
    [IO.File]::WriteAllText($strings, $stringText.Replace('id="st_opt_fsr3"', 'id="removed_fsr3"'))
    Assert-Rejected 'Missing rus UI translation: st_opt_fsr3'
    [IO.File]::WriteAllText($strings, $stringText)
    $hudStrings = "$fixture/gamedata/config/text/eng/ui_st_hud_interact.xml"
    $hudStringText = [IO.File]::ReadAllText($hudStrings)
    [IO.File]::WriteAllText($hudStrings, $hudStringText.Replace('id="st_minimap_pos_off"', 'id="removed_minimap_off"'))
    Assert-Rejected 'Missing eng UI translation: st_minimap_pos_off'
}
finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    if (-not $resolved.StartsWith($release.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Test cleanup escaped release directory.'
    }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}

if ($ArchivePath) {
    $archive = (Resolve-Path -LiteralPath $ArchivePath).Path
    $expected = ([IO.File]::ReadAllText("$archive.sha256") -split '\s+')[0]
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'ZIP checksum mismatch.' }
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $entries = @{}
        foreach ($entry in $zip.Entries) { $entries[$entry.FullName.Replace('\', '/')] = $entry }
        $reader = [IO.StreamReader]::new($entries['SHA256SUMS.txt'].Open())
        try { $sums = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $count = 0
        foreach ($line in $sums -split '\r?\n' | Where-Object { $_ }) {
            if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw "Malformed manifest line: $line" }
            $hash = $Matches[1]
            $name = $Matches[2].Replace('\', '/')
            if (-not $entries.ContainsKey($name)) { throw "Missing ZIP entry: $name" }
            $stream = $entries[$name].Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose(); $sha.Dispose() }
            if ($actual -ne $hash) { throw "Payload checksum mismatch: $name" }
            $count++
        }
        foreach ($file in $payload) {
            $name = $file.RelativePath
            if (-not $entries.ContainsKey($name)) { throw "Compatibility file missing from ZIP: $name" }
            $stream = $entries[$name].Open()
            $sha = [Security.Cryptography.SHA256]::Create()
            try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose(); $sha.Dispose() }
            if ($actual -ne (Get-FileHash -LiteralPath $file.Source).Hash) { throw "Stale Game resource: $name" }
        }
        $engineStream = $entries['bin_x64/xrEngine.exe'].Open()
        $engineBytes = [IO.MemoryStream]::new()
        try {
            $engineStream.CopyTo($engineBytes)
            $engineText = [Text.Encoding]::ASCII.GetString($engineBytes.ToArray())
        }
        finally {
            $engineStream.Dispose()
            $engineBytes.Dispose()
        }
        foreach ($marker in @('night_vision_rad', 'shader_param_5')) {
            if (-not $engineText.Contains($marker)) { throw "Engine is missing required runtime export: $marker" }
        }
        if ($engineText.Contains('kNIGHT_VISION_RAD')) {
            throw 'Engine contains a duplicate static NV action export.'
        }
        $runtimeNames = @($entries.Keys | Where-Object { $_ -match '^(gamedata|mods|bin_x64)/' -and -not $_.EndsWith('/') })
        $expectedNames = @($payload.RelativePath) + @('bin_x64/xrEngine.exe', 'bin_x64/nvngx_dlss.dll')
        if (Compare-Object $expectedNames $runtimeNames) { throw 'Unexpected or missing runtime files in ZIP.' }
        Write-Host "Verified ZIP checksum, $count payload hashes, and Game resource contents."
    }
    finally { $zip.Dispose() }
}
if ($BaselineArchivePath) {
    if (-not $ArchivePath) { throw '-BaselineArchivePath requires -ArchivePath.' }
    function Get-RuntimeHashes([string]$Path) {
        $archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
        try {
            foreach ($entry in $archive.Entries) {
                $name = $entry.FullName.Replace('\', '/')
                if ($name -notmatch '^(gamedata|mods|bin_x64)/' -or $name.EndsWith('/')) { continue }
                $stream = $entry.Open()
                $sha = [Security.Cryptography.SHA256]::Create()
                try { $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
                finally { $stream.Dispose(); $sha.Dispose() }
                "$name $hash"
            }
        }
        finally { $archive.Dispose() }
    }
    $before = @(Get-RuntimeHashes $BaselineArchivePath)
    $after = @(Get-RuntimeHashes $ArchivePath)
    $difference = @(Compare-Object $before $after)
    if ($difference.Count) { throw "Runtime package changed during consolidation: $($difference | Out-String)" }
    Write-Host "Migration parity passed: all $($after.Count) runtime files are byte-identical."
}
Write-Host 'Radiophobia release UI checks passed.'
