
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Exe = Join-Path $Root 'build-msvc\Release\satrar.exe'
$Fixture = Join-Path $Root 'tests\fixtures\paper\fig1.aag'
& (Join-Path $Root 'build-msvc\Release\test_rar_discover.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Commands = @("read `"$Fixture`"", 'rar_verify 13 0 sa1 wire 10', 'quit')
$Output = ($Commands | & $Exe --shell 2>&1 | Out-String)
$Output
if ($LASTEXITCODE -ne 0 -or $Output -notmatch '(?s)candidate\[(\d+)\] attach=15 rule=Theorem-1 source_raw=23\s+rar_verify=verified_equivalent candidate=\1') {
    throw 'Figure 1 g5 -> g9 did not pass both CEC stages.'
}
