function Get-RadiophobiaPayload {
    param([Parameter(Mandatory)][string]$ResourceRoot)

    $root = (Resolve-Path -LiteralPath $ResourceRoot).Path.TrimEnd('\')
    $manifest = Join-Path $root 'radiophobia-release-files.txt'
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in [IO.File]::ReadAllLines($manifest)) {
        $relative = $line.Trim().Replace('\', '/')
        if (-not $relative -or $relative.StartsWith('#')) { continue }
        if ($relative -notmatch '^(gamedata|mods)/' -or $relative -match '(^|/)\.\.?(/|$)|[:*?]' -or
            $relative.StartsWith('gamedata/shaders/') -or -not $seen.Add($relative)) {
            throw "Invalid or duplicate Radiophobia payload path: $relative"
        }
        $source = [IO.Path]::GetFullPath((Join-Path $root $relative))
        if (-not $source.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required Radiophobia resource is missing or outside its root: $relative"
        }
        [pscustomobject]@{ RelativePath = $relative; Source = $source }
    }
    if ($seen.Count -eq 0) { throw 'Radiophobia release file list is empty.' }
    $shaders = @(Get-ChildItem -LiteralPath (Join-Path $root 'gamedata/shaders') -Recurse -File -Force |
        Where-Object Name -NotIn @('.clang-format', '.gitattributes', 'clang_format.cmd'))
    if ($shaders.Count -eq 0) { throw 'Radiophobia shader tree is empty.' }
    foreach ($file in $shaders | Sort-Object FullName) {
        [pscustomobject]@{
            RelativePath = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
            Source = $file.FullName
        }
    }
}
