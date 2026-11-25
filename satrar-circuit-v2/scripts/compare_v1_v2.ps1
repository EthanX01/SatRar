# Figure 1 discovery comparison for the gate-native solver.
# Run from the EdaLab root. Large Table I/II cases use the same CLI with a higher trial cap.
param(
    [string]$Exe = "",
    [string]$V1Exe = ""
)
$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
if (-not $Exe) {
    $Exe = Join-Path $root "satrar-circuit-v2\build-msvc\Release\satrar_v2.exe"
}
$fig = Join-Path $root "satrar-circuit-v2\tests\fixtures\paper\fig1.aag"
$outDir = Join-Path $root "satrar-circuit-v2\results\v5"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$modes = @("l0", "l1", "l2", "wire", "gate", "gate_nolearn", "gate_nofilter", "sub")
$rows = @("mode`tcandidates`ttrials`tlearned`tgate_visits`tseconds`tlimited")
foreach ($mode in $modes) {
    $commands = "read $fig`r`nrar_search 13 0 sa1 $mode`r`n"
    $inputFile = Join-Path $outDir "commands.txt"
    [System.IO.File]::WriteAllText($inputFile, $commands, [System.Text.UTF8Encoding]::new($false))
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $text = cmd /c "`"$Exe`" < `"$inputFile`""
    $clock.Stop()
    $output = @($text -split "`r?`n")
    $watch = $clock.Elapsed
    $summary = $output | Where-Object { $_ -like "mode=*" } | Select-Object -First 1
    if (-not $summary) { throw "no summary for $mode from '$Exe' output=[$($output -join ' | ')]" }
    $fields = @{}
    foreach ($part in ($summary -split " ")) {
        if ($part -match "=") {
            $pair = $part -split "=", 2
            $fields[$pair[0]] = $pair[1]
        }
    }
    $rows += ("{0}`t{1}`t{2}`t{3}`t{4}`t{5:N3}`t{6}" -f $mode, $fields["candidates"], $fields["trials"],
        $fields["learned"], $fields["gate_visits"], $watch.TotalSeconds, $fields["limited"])
    $keys = $output | Where-Object { $_ -like "cand *" }
    Set-Content -Path (Join-Path $outDir "fig1_$mode.keys.txt") -Value $keys -Encoding ascii
}
$report = Join-Path $outDir "fig1_modes.tsv"
Set-Content -Path $report -Value $rows -Encoding ascii
Write-Output "wrote $report"
if ($V1Exe -and (Test-Path $V1Exe)) {
    Write-Output "V1 binary noted at $V1Exe. Compare canonical keys with the V2 key files; order is not a compat profile."
}
