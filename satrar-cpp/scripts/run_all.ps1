# Build, unit-test, and EPFL benchmark smoke (parse + stats via satrar.exe).
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root 'build-msvc'
$CmakeDir = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$Cmake = Join-Path $CmakeDir 'cmake.exe'
$Ctest = Join-Path $CmakeDir 'ctest.exe'
if (-not (Test-Path $Cmake)) {
    Write-Host '[satrar][ERROR] cmake not found at default VS BuildTools path' -ForegroundColor Red
    exit 2
}

Write-Host '[satrar][INFO] configuring...' -ForegroundColor Cyan
& $Cmake -S $Root -B $Build -G 'Visual Studio 17 2022' -A x64 | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host '[satrar][INFO] building Release...' -ForegroundColor Cyan
& $Cmake --build $Build --config Release | Out-Host
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host '[satrar][INFO] running CTest...' -ForegroundColor Cyan
Push-Location $Build
& $Ctest -C Release --output-on-failure
$ctestExit = $LASTEXITCODE
Pop-Location
if ($ctestExit -ne 0) {
    Write-Host "[satrar][ERROR] CTest failed exit=$ctestExit" -ForegroundColor Red
    exit $ctestExit
}
Write-Host '[satrar][OK] CTest passed' -ForegroundColor Green

$Exe = Join-Path $Build 'Release\satrar.exe'
$Inputs = Join-Path $Root 'benchmarks\inputs'
if (-not (Test-Path $Exe)) {
    Write-Host '[satrar][ERROR] satrar.exe missing' -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $Inputs)) {
    Write-Host '[satrar][WARN] benchmarks/inputs missing — skip EPFL smoke' -ForegroundColor Yellow
    exit 0
}

$files = Get-ChildItem $Inputs -Recurse -Filter '*.aag' | Sort-Object FullName
$ok = 0
$fail = 0
$skipLatch = 0
foreach ($f in $files) {
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $rel = $f.FullName.Substring($Root.Length + 1)
    $lines = @(
        'verbose on',
        "read `"$($f.FullName)`"",
        'stats',
        'quit'
    )
    $out = ($lines | & $Exe --verbose --shell *>&1 | Out-String)
    if ($out -like '*latches*' -or $out -like '*combinational-only parser*') {
        Write-Host "[satrar][SKIP] $rel (sequential/latches)" -ForegroundColor Yellow
        $skipLatch++
    } elseif ($out -match '\[satrar\]\[ERROR\]') {
        Write-Host "[satrar][FAIL] $rel" -ForegroundColor Red
        ($out -split "`n" | Where-Object { $_ -match '\[satrar\]\[ERROR\]' }) | ForEach-Object { Write-Host "  $_" }
        $fail++
    } elseif ($out -match '\[satrar\]\[OK\] loaded') {
        $m = [regex]::Match($out, 'AND=(\d+)')
        $ands = if ($m.Success) { $m.Groups[1].Value } else { '?' }
        Write-Host "[satrar][PASS] $rel (AND=$ands)" -ForegroundColor Green
        $ok++
    } else {
        Write-Host "[satrar][FAIL] $rel (unexpected output)" -ForegroundColor Red
        Write-Host $out
        $fail++
    }
    $ErrorActionPreference = $prevEap
}

Write-Host "---- summary: pass=$ok fail=$fail skip_latch=$skipLatch total=$($files.Count) ----"
if ($fail -gt 0) {
    exit 1
}
exit 0
