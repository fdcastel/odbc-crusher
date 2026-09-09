#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Prepares every driver in the manifest for triage from one stress-test run.

.DESCRIPTION
    The all-drivers counterpart to `/triage-driver`, for use outside Claude Code
    or when you want the whole matrix at once. It resolves a stress-test run,
    then calls .claude/skills/triage-driver/triage.ps1 once per driver to clone
    the matching source, download that driver's report, and gate it.

    Afterwards each driver has a ./tmp/triage/<driver>/ directory holding
    preflight.txt, findings.json and the raw artifacts - ready to hand to an
    analysis agent, or to read yourself.

    H11: this script used to carry its own copy of the clone / download / prompt
    logic, which had drifted from the skill in every direction that mattered: a
    different directory layout (tmp/external/<repo>), a two-label taxonomy
    instead of four, output written to a `recommendations/` directory that no
    longer exists, and instructions to file crusher bugs in PROJECT_PLAN.md when
    the live work queue has been docs/IMPROVEMENT_PLAN.md since H8. Two of its
    behaviours were better than the skill's and survive in triage.ps1: reusing
    an existing clone instead of failing on it, and deleting stale reports
    before downloading.

    The script is idempotent: safe to run repeatedly.

.PARAMETER RunId
    Stress-test run to pull artifacts from. Defaults to the most recent
    completed run.

.PARAMETER Dispatch
    Trigger a fresh `driver=all` stress-test run and wait for it first.

.PARAMETER Driver
    Limit to these drivers instead of the whole manifest.

.EXAMPLE
    ./fetch-stress-test.ps1
    Prepare every driver from the latest completed stress-test run.

.EXAMPLE
    ./fetch-stress-test.ps1 -Dispatch
    Run the full matrix in CI first, then prepare everything from it.

.NOTES
    Requires an authenticated `gh` and `git`. Does not require `jq`.
#>

[CmdletBinding()]
param(
    [long]$RunId = 0,
    [switch]$Dispatch,
    [string[]]$Driver,
    [switch]$AllowPartial
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot  = $PSScriptRoot
$ManifestPath = Join-Path $ProjectRoot '.github' 'drivers.json'
$TriageScript = Join-Path $ProjectRoot '.claude' 'skills' 'triage-driver' 'triage.ps1'

foreach ($required in @($ManifestPath, $TriageScript)) {
    if (-not (Test-Path $required)) {
        throw "Not found: $required - run this from the project root."
    }
}

$Manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$AllNames = @($Manifest.drivers.PSObject.Properties.Name)

# @() around the whole expression: a single-element [string[]] unrolls to a bare
# String on the way out of an `if`, and under StrictMode a String has no .Count -
# so `-Driver mock-driver` crashed while `-Driver a,b` worked.
$Targets = @(if ($Driver) {
    $unknown = @($Driver | Where-Object { $AllNames -notcontains $_ })
    if ($unknown) { throw "Unknown driver(s): $($unknown -join ', '). Available: $($AllNames -join ', ')" }
    $Driver
} else {
    $AllNames
})

# T3/T4: an entry that declares `pair_with` brings its sibling along. Asking
# for half a pair is almost always a slip - the report of one half on its own
# answers nothing the pair was set up to answer - and triaging the other half
# from an existing run costs nothing, since both artifacts came out of the same
# stress-test run.
$PairOf = @{}
foreach ($n in $AllNames) {
    $e = $Manifest.drivers.$n
    if ($e.PSObject.Properties.Name -contains 'pair_with') { $PairOf[$n] = [string]$e.pair_with }
}
$added = @($Targets | ForEach-Object { if ($PairOf.ContainsKey($_)) { $PairOf[$_] } } |
          Where-Object { $_ -and $Targets -notcontains $_ } | Sort-Object -Unique)
if ($added.Count -gt 0) {
    Write-Host "Pulling in the other half of the pair: $($added -join ', ')" -ForegroundColor DarkGray
    $Targets = @($Targets) + $added
}

Write-Host ""
Write-Host "fetch-stress-test — preparing $($Targets.Count) driver(s) for triage" -ForegroundColor Green
Write-Host "  $($Targets -join ', ')" -ForegroundColor DarkGray

# ── Resolve one run for the whole matrix ───────────────────────
# One run, not one per driver: a `driver=all` run carries every report-<driver>
# artifact, so seven separate dispatches would be six wasted CI runs.
if ($Dispatch) {
    $defaultBranch = gh repo view --json defaultBranchRef -q '.defaultBranchRef.name'
    $before = 0
    # @() likewise: ConvertFrom-Json unrolls a one-element JSON array to a bare
    # object, and `--limit 1` always returns exactly one.
    $prev = @(gh run list --workflow stress-test.yml --limit 1 --json databaseId | ConvertFrom-Json)
    if ($prev.Count -gt 0) { $before = [long]$prev[0].databaseId }

    Write-Host "`nDispatching stress-test.yml (driver=all) on '$defaultBranch'..." -ForegroundColor Cyan
    gh workflow run stress-test.yml --ref $defaultBranch -f driver=all
    if ($LASTEXITCODE -ne 0) { throw "gh workflow run failed (exit $LASTEXITCODE)" }

    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $deadline -and $RunId -eq 0) {
        Start-Sleep -Seconds 3
        $found = gh run list --workflow stress-test.yml --event workflow_dispatch --limit 5 --json databaseId |
                 ConvertFrom-Json | Where-Object { [long]$_.databaseId -gt $before }
        if ($found) { $RunId = [long]($found | Sort-Object { [long]$_.databaseId } | Select-Object -First 1).databaseId }
    }
    if ($RunId -eq 0) { throw "Dispatched, but no new run appeared within 120s." }
    Write-Host "Run $RunId started — waiting for the full matrix (this takes a while)..." -ForegroundColor Cyan
    gh run watch "$RunId" --interval 15 --compact
}
elseif ($RunId -eq 0) {
    $runs = gh run list --workflow stress-test.yml --limit 20 --json databaseId,status,conclusion,createdAt | ConvertFrom-Json
    $pick = @($runs | Where-Object { $_.status -eq 'completed' }) | Select-Object -First 1
    if (-not $pick) { throw "No completed stress-test run found. Use -Dispatch to trigger one." }
    $RunId = [long]$pick.databaseId
    Write-Host "`nUsing run $RunId ($($pick.conclusion), $($pick.createdAt))" -ForegroundColor Cyan
}

# ── Prepare each driver ────────────────────────────────────────
# triage.ps1's exit codes are contractual; a driver that fails its gate is
# recorded and the loop continues, because one bad job should not cost you the
# other six.
$Reasons = @{
    0 = 'ready'; 2 = 'unknown driver'; 3 = 'CI failure'
    4 = 'no usable report'; 5 = 'version mismatch'; 6 = 'partial report'
}

$results = @()
foreach ($name in $Targets) {
    Write-Host "`n─── $($name.ToUpper()) ───────────────────────────────" -ForegroundColor Yellow

    $argv = @('-NoProfile', '-File', $TriageScript, '-Driver', $name, '-RunId', "$RunId")
    if ($AllowPartial) { $argv += '-AllowPartial' }
    & pwsh @argv
    $code = $LASTEXITCODE

    $outDir = Join-Path $ProjectRoot 'tmp' 'triage' $name
    $findings = ''
    $preflight = Join-Path $outDir 'preflight.txt'
    if (Test-Path $preflight) {
        $line = Select-String -Path $preflight -Pattern '^FINDINGS_COUNT=' | Select-Object -First 1
        if ($line) { $findings = ($line.Line -split '=', 2)[1] }
    }

    $results += [pscustomobject]@{
        Driver   = $name
        Status   = $(if ($Reasons.ContainsKey($code)) { $Reasons[$code] } else { "exit $code" })
        Findings = $findings
        OutDir   = $outDir
    }
}

# ── Summary ────────────────────────────────────────────────────
Write-Host "`n"
$results | Format-Table -AutoSize Driver, Status, Findings

$ready = @($results | Where-Object { $_.Status -eq 'ready' })
Write-Host "$($ready.Count)/$($results.Count) driver(s) ready for analysis." -ForegroundColor Green
if ($ready.Count -gt 0) {
    Write-Host "Each has preflight.txt + findings.json under ./tmp/triage/<driver>/." -ForegroundColor DarkGray
    Write-Host "To analyse one: /triage-driver <driver> $RunId  (or point an agent at" -ForegroundColor DarkGray
    Write-Host ".claude/skills/triage-driver/analyze.md with OUT_DIR set)." -ForegroundColor DarkGray
}
$stuck = @($results | Where-Object { $_.Status -ne 'ready' })
if ($stuck.Count -gt 0) {
    Write-Host "`nNot ready — see each driver's preflight.txt for ABORT_REASON:" -ForegroundColor Yellow
    foreach ($r in $stuck) { Write-Host "  $($r.Driver): $($r.Status)" -ForegroundColor Yellow }
}

# ── T3: the diff, out of the same command ──────────────────────
# A pair's two reports are not the deliverable; the diff between them is. It
# was being produced by hand every time, which is how a pair gets compared
# against the wrong run. Emitted here for every pair where both halves came
# back ready, printed in the order the manifest declares (the entry naming a
# sibling is the OLD side - `firebird-official` names `firebird-patched`, and
# official is the baseline).
$Compare = Join-Path $ProjectRoot 'tools' 'compare_reports.py'
$readyNames = @($ready | ForEach-Object { $_.Driver })
$done = @()
foreach ($name in $readyNames) {
    if (-not $PairOf.ContainsKey($name)) { continue }
    $sib = $PairOf[$name]
    if ($readyNames -notcontains $sib) { continue }
    $key = (@($name, $sib) | Sort-Object) -join '|'
    if ($done -contains $key) { continue }
    $done += $key

    $oldJson = Join-Path $ProjectRoot 'tmp' 'triage' $name 'artifacts/crusher-report.json'
    $newJson = Join-Path $ProjectRoot 'tmp' 'triage' $sib  'artifacts/crusher-report.json'
    if (-not (Test-Path $oldJson) -or -not (Test-Path $newJson)) { continue }
    if (-not (Test-Path $Compare)) {
        Write-Host "`n$Compare is missing; skipping the pair diff." -ForegroundColor Yellow
        continue
    }

    Write-Host "`n─── $($name.ToUpper()) vs $($sib.ToUpper()) ──────────────" -ForegroundColor Yellow
    & python $Compare $oldJson $newJson
    if ($LASTEXITCODE -ne 0) {
        Write-Host "compare_reports.py exited $LASTEXITCODE" -ForegroundColor Yellow
    }
}
Write-Host ""
