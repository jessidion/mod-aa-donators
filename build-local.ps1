param(
    [string]$ServerRoot = 'C:\Games\CoA Server Migrated Copy\CoA Server (modular)',
    [string]$VisualStudioRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
)
$ErrorActionPreference = 'Stop'
$moduleRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sdkRoot = Join-Path $ServerRoot '.manager\sdk'
$sdk = Get-ChildItem $sdkRoot -Directory | Where-Object {
    Test-Path (Join-Path $_.FullName 'cmake\CoAPluginSDKConfig.cmake')
} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $sdk) { throw "CoA Plugin SDK was not found in $sdkRoot" }
$vcvars = Join-Path $VisualStudioRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "MSVC vcvars64.bat missing: $vcvars" }
$cmake = Join-Path $VisualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $VisualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path $cmake)) { throw "CMake missing: $cmake" }
if (-not (Test-Path $ninja)) { throw "Ninja missing: $ninja" }
$build = Join-Path $moduleRoot 'build'
$cmdLine = 'call "' + $vcvars + '" && "' + $cmake + '" -S "' + $moduleRoot + '" -B "' + $build + '" -G Ninja -DCMAKE_MAKE_PROGRAM="' + $ninja + '" -DCOA_PLUGIN_SDK_DIR="' + $sdk.FullName + '" -DCMAKE_BUILD_TYPE=RelWithDebInfo && "' + $cmake + '" --build "' + $build + '" --parallel 4'
Write-Host "Building with SDK: $($sdk.FullName)"
& cmd.exe /d /c $cmdLine
if ($LASTEXITCODE -ne 0) { throw "Compilation failed with exit code $LASTEXITCODE" }
Write-Host "DLL built under: $build"
Write-Host 'Install through the Modules Preview plugin workflow on an isolated server; do not overwrite worldserver.exe.'
