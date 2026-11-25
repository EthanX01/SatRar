# Reproduces the experiment matrix of SatRAR_Implementation_Spec.md section 8 (R7).
# Run from the EdaLab root after a Release build:  .\satrar-cpp\scripts\run_spec_experiments.ps1
# Every step writes to its own results\spec_* directory; existing directories are reused as checkpoints
# (identical settings only), so an interrupted run resumes. Use -Fresh to remove them first.
param([switch]$Fresh, [string[]]$Only = @())
$ErrorActionPreference = 'Continue'
$base = 'satrar-cpp'
$manifest = "$base/benchmarks/manifests/aspdac2009.json"
$logs = "$base/results/logs"
New-Item -ItemType Directory -Force $logs | Out-Null

function Step($name, [string[]]$arguments) {
    if ($Only.Count -and $Only -notcontains $name) { return }
    $out = "$base/results/$name"
    if ($Fresh -and (Test-Path $out)) { Remove-Item -Recurse -Force $out }
    $started = Get-Date
    Write-Host "[$name] start $started"
    & python -u "$base/scripts/rar_batch.py" @arguments --out $out *>&1 | Tee-Object -FilePath "$logs/$name.log"
    Write-Host "[$name] exit $LASTEXITCODE after $([int]((Get-Date) - $started).TotalSeconds)s"
}

# 1. Figure 1, all modes with two-stage CEC; then the same run in 4-pin shards (must be identical).
Step 'spec_fig1' @('--scope', 'all')
Step 'spec_fig1_sharded' @('--scope', 'all', '--shard-wires', '4')

# 2. Four-pin probe over every pinned input (12/12) in every mode: finds input and mode errors early.
Step 'spec_smoke' @('--manifest', $manifest, '--wire-limit', '4', '--discover-only')
Step 'spec_b19' @('--manifest', $manifest, '--case', 'b19', '--modes', 'wire', '--fetch-missing', '--wire-limit', '4', '--discover-only')

# 3. Complete discovery of the small circuits, seven paper comparison modes. The full-discovery steps use
#    --max-trials 1000000 (the smoke run keeps the default 20000); a case that still hits it is reported as partial.
$seven = @('wire', 'l0', 'l1', 'l2', 'gate', 'gate_nolearn', 'gate_nofilter')
Step 'spec_full_small' (@('--manifest', $manifest, '--case', 'C432', '--case', 'C499', '--case', 'C6288', '--case', 'b01', '--case', 'b13', '--modes') + $seven + @('--discover-only', '--summary-only', '--max-trials', '1000000', '--case-timeout', '900'))

# 4. Mid-size circuits, sharded by pin so unfinished ranges can be resumed independently.
Step 'spec_full_c7552_l2' @('--manifest', $manifest, '--case', 'C7552', '--modes', 'l2', '--discover-only', '--summary-only', '--max-trials', '1000000', '--shard-wires', '250', '--case-timeout', '300')
Step 'spec_full_medium' (@('--manifest', $manifest, '--case', 'C3540', '--case', 'C7552', '--modes') + $seven + @('--discover-only', '--summary-only', '--max-trials', '1000000', '--shard-wires', '250', '--case-timeout', '300'))
Step 'spec_full_large' (@('--manifest', $manifest, '--case', 'b14', '--case', 'b15', '--case', 'b17', '--modes', 'wire', 'l0', 'l1', '--discover-only', '--summary-only', '--max-trials', '1000000', '--shard-wires', '1000', '--case-timeout', '600'))
