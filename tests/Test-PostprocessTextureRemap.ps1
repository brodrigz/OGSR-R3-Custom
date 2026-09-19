[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo 'release\postprocess-remap-test'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$backend = [IO.File]::ReadAllText((Join-Path $repo 'ogsr_engine\Layers\xrRender\R_Backend_Runtime.cpp'))
$remapPath = 'ogsr_engine/Layers/xrRender/RenderTargetRenderScreenQuad.cpp'
$remap = [IO.File]::ReadAllText((Join-Path $repo $remapPath))
$start = $backend.IndexOf('void CBackend::set_Textures(STextureList* _T)')
$end = $backend.IndexOf('extern float r__dtex_range;', $start)
$methods = $backend.Substring($start, $end - $start)
$start = $remap.IndexOf('void CRenderTarget::pp_remap_scene_srv(')
$end = $remap.IndexOf('void CRenderTarget::RenderScreenTriangle(', $start)
$methods += $remap.Substring($start, $end - $start)
[IO.File]::WriteAllText((Join-Path $output 'postprocess-production.inl'), $methods)
$msvc = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC' -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdk = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'ucrt') } | Sort-Object Name -Descending | Select-Object -First 1
$sdkLib = Join-Path (Split-Path (Split-Path $sdk.FullName)) "Lib\$($sdk.Name)"
$compiler = Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe'
$exe = Join-Path $output 'test.exe'
& $compiler /nologo /EHsc /MT /std:c++20 /W4 "/I$output" "/I$($msvc.FullName)\include" "/I$($sdk.FullName)\ucrt" "/I$($sdk.FullName)\um" "/I$($sdk.FullName)\shared" "/Fo$output\test.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'PostprocessTextureRemap.cpp') /link "/LIBPATH:$($msvc.FullName)\lib\x64" "/LIBPATH:$sdkLib\ucrt\x64" "/LIBPATH:$sdkLib\um\x64"
if ($LASTEXITCODE -ne 0) { throw 'Regression test compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) {
    throw "Texture cache regression test failed: $LASTEXITCODE"
}
