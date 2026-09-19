$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'release\hud-attachment-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$source = [IO.File]::ReadAllText((Join-Path $repo 'ogsr_engine\xrGame\player_hud.cpp'))
$start = $source.IndexOf('void player_hud::calc_transform(')
$end = $source.IndexOf('void player_hud::OnMovementChanged(', $start)
[IO.File]::WriteAllText((Join-Path $output 'hud-attachment-production.inl'), $source.Substring($start, $end - $start))
$msvc = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdk = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'ucrt') } | Sort-Object Name -Descending | Select-Object -First 1
$sdkLib = Join-Path (Split-Path (Split-Path $sdk.FullName)) "Lib\$($sdk.Name)"
$exe = Join-Path $output 'test.exe'
& (Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe') /nologo /EHsc /MT /std:c++20 /W4 "/I$output" "/I$($msvc.FullName)\include" "/I$($sdk.FullName)\ucrt" "/I$($sdk.FullName)\um" "/I$($sdk.FullName)\shared" "/Fo$output\test.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'HudItemAttachment.cpp') /link "/LIBPATH:$($msvc.FullName)\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64"
if ($LASTEXITCODE -ne 0) { throw 'HUD attachment test compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'HUD attachment regression test failed' }
