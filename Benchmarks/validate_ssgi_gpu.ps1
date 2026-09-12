param(
    [string]$Msvc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207',
    [string]$Sdk = 'C:/Program Files (x86)/Windows Kits/10',
    [string]$SdkVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$outputPath = Join-Path $repoPath 'ogsr_engine/_TEMP/ssgi_validation'
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$compiler = Join-Path $Msvc 'bin/Hostx64/x64/cl.exe'
$arguments = @('/nologo', '/std:c++17', '/EHsc', '/O2', '/MD',
    '/I', "$Msvc/include", '/I', "$Sdk/Include/$SdkVersion/ucrt", '/I', "$Sdk/Include/$SdkVersion/shared", '/I', "$Sdk/Include/$SdkVersion/um", '/I', "$Sdk/Include/$SdkVersion/winrt",
    "/Fo$outputPath/ssgi_gpu_validation.obj", "/Fe$outputPath/ssgi_gpu_validation.exe", "$PSScriptRoot/ssgi_gpu_validation.cpp",
    '/link', "/LIBPATH:$Msvc/lib/x64", "/LIBPATH:$Sdk/Lib/$SdkVersion/ucrt/x64", "/LIBPATH:$Sdk/Lib/$SdkVersion/um/x64",
    'd3d11.lib', 'd3dcompiler.lib', 'dxgi.lib')
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'SSGI validation executable compilation failed' }
& "$outputPath/ssgi_gpu_validation.exe" $outputPath
if ($LASTEXITCODE -ne 0) { throw 'SSGI WARP validation failed' }
