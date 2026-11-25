
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root 'build-msvc'
$Cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $Cmake -S $Root -B $Build -G 'Visual Studio 17 2022' -A x64 -DSATRAR_BUILD_CADICAL=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Cmake --build $Build --config Release --target satrar_cadical
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $Build 'Release\satrar_cadical.exe') --version
exit $LASTEXITCODE
