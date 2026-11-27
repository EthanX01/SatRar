# Phase 0 probe for the ABC CEC plan.
# Captures ABC help and command output, checks Figure 1 equivalence, and times C3540.
# Usage (from anywhere):
#   .\satrar-cpp\scripts\abc_probe.ps1
#   .\satrar-cpp\scripts\abc_probe.ps1 -Abc C:\path\to\yosys-abc.exe
#   .\satrar-cpp\scripts\abc_probe.ps1 -SkipTiming

param(
    [string]$Abc = $env:SATRAR_ABC_EXE,
    [switch]$SkipTiming
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Probe = Join-Path $Root "results\abc_probe"
$Fixtures = Join-Path $Root "tests\fixtures\abc"
New-Item -ItemType Directory -Force -Path $Probe, $Fixtures | Out-Null

function Find-Abc {
    param([string]$Requested)
    $candidates = @()
    if ($Requested) { $candidates += $Requested }
    $candidates += (Join-Path $Root "tools\abc\bin\yosys-abc.exe")
    $candidates += (Join-Path $Root "tools\abc\bin\abc.exe")
    $candidates += (Join-Path $Root "tools\abc\oss-cad-suite\bin\yosys-abc.exe")
    foreach ($path in $candidates) {
        if ($path -and (Test-Path -LiteralPath $path)) { return (Resolve-Path -LiteralPath $path).Path }
    }
    throw "ABC executable not found. Set SATRAR_ABC_EXE or pass -Abc."
}

function Add-AbcPath {
    param([string]$Executable)
    $bin = Split-Path -Parent $Executable
    $lib = Join-Path (Split-Path -Parent $bin) "lib"
    $prefix = @($bin)
    if (Test-Path -LiteralPath $lib) { $prefix += $lib }
    $env:PATH = ($prefix + $env:PATH) -join ";"
}

function Write-Utf8 {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

function Format-AbcArguments {
    param([string[]]$Arguments)
    # Start-Process splits on spaces, so a command such as "cec a.aig b.aig" never reaches ABC.
    ($Arguments | ForEach-Object {
        if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
    }) -join " "
}

function Invoke-Abc {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$Log,
        [string]$WorkDir
    )
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = Format-AbcArguments (@("-s") + $Arguments)
    $info.WorkingDirectory = $WorkDir
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    $started = [Diagnostics.Stopwatch]::StartNew()
    [void]$process.Start()
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $started.Stop()
    $text = $stdoutTask.Result + $stderrTask.Result
    Write-Utf8 $Log $text
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Milliseconds = $started.Elapsed.TotalMilliseconds
        Text = $text
    }
}

function Classify-Cec {
    param([string]$Text)
    $lower = $Text.ToLowerInvariant()
    # Match the cec sentence only. The executable usage also contains the word "equivalent".
    $notEq = ([regex]::Matches($lower, "not equivalent")).Count
    $eq = ([regex]::Matches($lower, "networks are equivalent")).Count
    $undecided = ([regex]::Matches($lower, "undecided")).Count
    if ($notEq -ge 1 -and $undecided -eq 0) { return "not_equivalent" }
    if ($eq -ge 1 -and $notEq -eq 0 -and $undecided -eq 0) { return "equivalent" }
    if ($undecided -ge 1) { return "undecided" }
    return "unparsed"
}

$Abc = Find-Abc $Abc
Add-AbcPath $Abc
Write-Host "ABC: $Abc"

$runs = @()
function Save-Run {
    param([string]$Name, [string[]]$Arguments, [string]$WorkDir = $Root)
    $log = Join-Path $Probe "$Name.txt"
    Write-Host "probe $Name"
    $result = Invoke-Abc -Executable $Abc -Arguments $Arguments -Log $log -WorkDir $WorkDir
    $script:runs += [pscustomobject]@{
        Name = $Name
        ExitCode = $result.ExitCode
        Milliseconds = [math]::Round($result.Milliseconds, 1)
        Class = (Classify-Cec $result.Text)
    }
    return $result
}

$banner = Save-Run "banner" @("-c", "quit")
$help = Save-Run "cec_help" @("-c", "cec -h")
$gia = Save-Run "gcec_help" @("-c", "&cec -h")
$echo = Save-Run "echo" @("-c", "echo SATRAR_BEGIN 7")
$fig1Aag = (Join-Path $Root "tests\fixtures\paper\fig1.aag").Replace("\", "/")
$readAag = Save-Run "read_aag" @("-c", "read_aiger $fig1Aag")

$python = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $python) { throw "python is required to convert AAG to AIG" }

function Convert-Aig {
    param([string]$Source, [string]$Destination)
    & python (Join-Path $Root "scripts\aag_to_aig.py") $Source $Destination --check
    if ($LASTEXITCODE -ne 0) { throw "aag_to_aig failed for $Source" }
}

Convert-Aig (Join-Path $Root "tests\fixtures\paper\fig1.aag") (Join-Path $Probe "fig1.aig")
Convert-Aig (Join-Path $Root "..\satrar-circuit-v2\results\fig1-rewritten.aag") (Join-Path $Probe "fig1-rewritten.aig")
& python (Join-Path $Root "scripts\aag_to_aig.py") (Join-Path $Root "tests\fixtures\paper\fig1.aag") `
    (Join-Path $Probe "fig1-bad.aig") --invert-output 0 --check
if ($LASTEXITCODE -ne 0) { throw "aag_to_aig failed for the inverted Figure 1 output" }

$commands = @(
    "cec fig1.aig fig1-rewritten.aig",
    "cec -n fig1.aig fig1-rewritten.aig",
    "&r fig1.aig; &cec fig1-rewritten.aig"
)
$winner = $null
$fig1Class = "unparsed"
$fig1Log = $null
foreach ($command in $commands) {
    $name = "fig1_" + ($command -replace '[^A-Za-z0-9]+', '_').Trim('_')
    $result = Save-Run $name @("-c", $command) $Probe
    $fig1Class = Classify-Cec $result.Text
    $fig1Log = Join-Path $Probe "$name.txt"
    if ($fig1Class -eq "equivalent") {
        $winner = $command
        break
    }
}
if (-not $winner) { $winner = $commands[0] }
$badCommand = $winner -replace "fig1-rewritten", "fig1-bad"
$bad = Save-Run "fig1_bad" @("-c", $badCommand) $Probe

$selfMean = $null
$batchMean = $null
$optClass = "not_run"
if (-not $SkipTiming) {
    $c3540 = Join-Path $Root "results\aspdac_wire_cec\converted\71fc7945792964ca8081cd02df0bc500cb2839d1f48d727d0815c5fda1681b32.aag"
    Convert-Aig $c3540 (Join-Path $Probe "c3540.aig")
    $selfCommand = $winner -replace "fig1\.aig", "c3540.aig" -replace "fig1-rewritten", "c3540"
    $selfTimes = @()
    for ($i = 1; $i -le 10; $i++) {
        $result = Save-Run ("c3540_self_{0:d2}" -f $i) @("-c", $selfCommand) $Probe
        $selfTimes += $result.Milliseconds
    }
    $selfMean = ($selfTimes | Measure-Object -Average).Average

    $opt = Save-Run "c3540_opt" @("-c", "read_aiger c3540.aig; strash; balance; rewrite; refactor; balance; rewrite -z; write_aiger c3540_opt.aig; quit") $Probe
    if (Test-Path -LiteralPath (Join-Path $Probe "c3540_opt.aig")) {
        $optCompare = $winner -replace "fig1\.aig", "c3540.aig" -replace "fig1-rewritten", "c3540_opt"
        $optResult = Save-Run "c3540_opt_cec" @("-c", $optCompare) $Probe
        $optClass = Classify-Cec $optResult.Text
    } else {
        $optClass = "no_output"
    }

    $builder = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt 100; $i++) {
        [void]$builder.AppendLine("echo SATRAR_BEGIN $i")
        [void]$builder.AppendLine($selfCommand)
        [void]$builder.AppendLine("echo SATRAR_END $i")
    }
    [void]$builder.AppendLine("quit")
    $scriptPath = Join-Path $Probe "batch100.abc"
    Write-Utf8 $scriptPath $builder.ToString()
    $batch = Save-Run "batch100" @("-f", "batch100.abc") $Probe
    $batchMean = $batch.Milliseconds / 100.0
}

$echoWorks = ($echo.Text -match "(?m)^SATRAR_BEGIN 7\s*$") -and ($echo.Text -notmatch "unknown command")
$readsAag = ($readAag.Text -notmatch "Wrong input|has failed|unknown command|Cannot read") -and ($readAag.Text -match "and =|and gates|Latches")
$badClass = Classify-Cec $bad.Text
$gateSpeed = if ($null -eq $batchMean) { "not_run" } elseif ($batchMean -le 40) { "pass" } else { "fail" }
$gateCorrect = ($fig1Class -eq "equivalent" -and $badClass -eq "not_equivalent")

$summary = @"
abc: $Abc
banner_exit: $($banner.ExitCode)
echo_works: $echoWorks
read_aag_ok: $readsAag
fig1_command: $winner
fig1_vs_rewritten: $fig1Class
fig1_vs_bad: $badClass
c3540_self_mean_ms: $(if ($null -eq $selfMean) { "not_run" } else { "{0:N1}" -f $selfMean })
c3540_opt_cec: $optClass
batch100_per_job_ms: $(if ($null -eq $batchMean) { "not_run" } else { "{0:N2}" -f $batchMean })
gate_correctness: $(if ($gateCorrect) { "pass" } else { "fail" })
gate_speed_40ms: $gateSpeed
"@
Write-Utf8 (Join-Path $Probe "SUMMARY.txt") ($summary.Trim() + "`n")
Write-Host $summary

Copy-Item -Force (Join-Path $Probe "cec_help.txt") (Join-Path $Fixtures "cec_help.txt")
Copy-Item -Force (Join-Path $Probe "gcec_help.txt") (Join-Path $Fixtures "gcec_help.txt")
Copy-Item -Force (Join-Path $Probe "echo.txt") (Join-Path $Fixtures "echo.txt")
if ($fig1Log) { Copy-Item -Force $fig1Log (Join-Path $Fixtures "fig1_equivalent.log") }
Copy-Item -Force (Join-Path $Probe "fig1_bad.txt") (Join-Path $Fixtures "fig1_not_equivalent.log")

if (-not $gateCorrect) { exit 1 }
exit 0
