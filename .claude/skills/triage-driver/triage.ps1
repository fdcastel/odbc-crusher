#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Prepares one driver for triage: resolve a stress-test run, clone the matching
    source, download the report, and gate it before any analysis happens.

.DESCRIPTION
    Everything the /triage-driver skill needs before an LLM looks at anything.
    Emits a flat KEY=VALUE block to stdout and to <OutDir>/preflight.txt, so the
    calling agent parses no JSON and runs no shell pipelines of its own.

    H11. This script exists because the skill used to inline all of this as bash
    + `jq` in SKILL.md, and three things were wrong with that:

      * `jq` is not installed on the primary development machine (neither shell),
        and SKILL.md read jq's exit 127 as "that driver is not in the manifest" -
        so the user was told the wrong thing and handed an empty list of valid
        names, because the recovery command was also jq.
      * The run-id capture was `sleep 8` then "take the most recent run". Losing
        that race silently triaged the *previous* run; if that run had been
        `driver=all` it even contained a matching artifact, so nothing downstream
        could notice.
      * The version cross-check could not fail for half the fleet. See
        Test-Provenance below.

    Idempotent: safe to re-run. An existing clone at the right tag is reused
    rather than treated as a clone failure (which is what the old skill did, and
    it degraded every second report on the same driver to "no source available").

.EXAMPLE
    ./triage.ps1 -Driver duckdb
    Dispatch a fresh single-driver stress-test run, wait for it, prepare it.

.EXAMPLE
    ./triage.ps1 -Driver duckdb -RunId 24943061694
    Re-analyse a run that already happened. Costs no CI time.

.NOTES
    Exit codes are contractual - SKILL.md maps each to a specific message:
      0  ready for analysis
      2  unknown driver
      3  dispatch or CI failure
      4  report artifact missing or unusable
      5  version provenance mismatch
      6  report is incomplete (partial run) and -AllowPartial was not given
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$Driver,

    # Reuse an existing stress-test run instead of dispatching a new one.
    [long]$RunId = 0,

    # Resolve the most recent successful run for this driver instead of dispatching.
    [switch]$NoDispatch,

    # Proceed even when the report is a partial snapshot. The report gets a banner.
    [switch]$AllowPartial,

    # Skip the clone entirely (source citations become unavailable).
    [switch]$SkipClone,

    # Never touch the network: analyse whatever is already in -OutDir. Used to
    # re-run analysis on a stored artifact, and by this skill's own test cases.
    [switch]$Offline,

    [string]$OutDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Contractual exit codes ─────────────────────────────────────
$EXIT_OK           = 0
$EXIT_BAD_DRIVER   = 2
$EXIT_CI           = 3
$EXIT_NO_REPORT    = 4
$EXIT_PROVENANCE   = 5
$EXIT_INCOMPLETE   = 6

# ── Output accumulator ─────────────────────────────────────────
# Everything the agent needs ends up here. One flat namespace, no nesting:
# the consumer is a language model reading a text file, not a program.
$script:Facts = [ordered]@{}

function Set-Fact([string]$Key, $Value) {
    if ($null -eq $Value) { $Value = '' }
    $script:Facts[$Key] = "$Value"
}

function Write-Step([string]$Message) {
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Note([string]$Message) {
    Write-Host "    $Message" -ForegroundColor DarkGray
}

function Write-Warn([string]$Message) {
    Write-Host "  ! $Message" -ForegroundColor Yellow
}

function Stop-Triage([int]$Code, [string]$Reason) {
    Set-Fact 'STATUS'       'ABORTED'
    Set-Fact 'ABORT_CODE'   $Code
    Set-Fact 'ABORT_REASON' $Reason
    Publish-Facts
    Write-Host ""
    Write-Host "ABORTED ($Code): $Reason" -ForegroundColor Red
    exit $Code
}

function Publish-Facts {
    # Written even on the abort paths - the abort reason is itself the most
    # useful thing the agent can relay to the user.
    if ($script:PreflightPath) {
        $lines = $script:Facts.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }
        Set-Content -Path $script:PreflightPath -Value $lines -Encoding UTF8 -Force
    }
    Write-Host ""
    Write-Host "----- BEGIN PREFLIGHT -----"
    foreach ($kv in $script:Facts.GetEnumerator()) { Write-Host "$($kv.Key)=$($kv.Value)" }
    Write-Host "----- END PREFLIGHT -----"
}

# ═══════════════════════════════════════════════════════════════
# Version comparison
# ═══════════════════════════════════════════════════════════════

function ConvertTo-VersionParts([string]$Raw) {
    if ([string]::IsNullOrWhiteSpace($Raw)) { return @() }
    $v = $Raw.Trim()
    $v = $v -replace '^\d+:', ''        # Debian epoch:   1:16.00.0000 -> 16.00.0000
    $v = $v -replace '-\d+$', ''        # Debian revision: 16.00.0000-1 -> 16.00.0000
    $v = $v -replace '^[vV]', ''        # tag prefix:     v1.5.2.0      -> 1.5.2.0
    # H17: semver prerelease suffix, 3.5.1-rc2 -> 3.5.1. A Windows VERSIONINFO
    # resource has four numeric fields and nowhere to put `rc2`, so the
    # firebird-patched MSI installs as `3.5.1.0` while the manifest pins
    # `3.5.1-rc2`; without this the observed value splits as `3`,`5`,`1-rc2`
    # and the comparer aborts the triage on a MISMATCH that is an artefact of
    # the format. What the suffix carries is not recoverable from the binary at
    # all - `install.sha256` is what identifies the build, and the entry's
    # provenance note says so rather than letting this look like a full check.
    $v = $v -replace '-[A-Za-z][0-9A-Za-z.]*$', ''
    return @($v.Split('.') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
}

function Test-IsCommitSha([string]$Ref) {
    # H17: `source.tag` may be a commit. Upstream firebird-odbc-driver never
    # tagged the 3.0.1.21 build - the version lives in WriteBuildNo.h and moves
    # by commit - so pinning the nearest tag would have cited source three
    # builds away from the binary being triaged.
    return ($Ref -match '^[0-9a-f]{7,40}$')
}

function Test-VersionMatch([string]$A, [string]$B) {
    # Returns MATCH | PARTIAL | MISMATCH | UNKNOWN.
    #
    # The old rule was "abort unless $MANIFEST is a substring of $RUNTIME" and it
    # contradicted the example given for it: `1.4.4.0` is not a substring of
    # `1.4.4`, so the documented case would have aborted. Component-wise prefix
    # matching in whichever direction is shorter handles that, the Debian epoch
    # form, and the `v` tag prefix - all three occur in this manifest.
    # H17: the `@()` are load-bearing. ConvertTo-VersionParts returns `@()` for
    # an empty input, PowerShell unrolls that to nothing on the way out, and
    # under the Set-StrictMode -Version Latest at the top of this file `.Count`
    # on the resulting $null throws - so the UNKNOWN branch below, which exists
    # precisely for an empty actual_version.txt, could never be reached. Same
    # unrolling hazard H11 hit with a single-element [string[]].
    $pa = @(ConvertTo-VersionParts $A)
    $pb = @(ConvertTo-VersionParts $B)
    if ($pa.Count -eq 0 -or $pb.Count -eq 0) { return 'UNKNOWN' }

    $short, $long = if ($pa.Count -le $pb.Count) { $pa, $pb } else { $pb, $pa }
    for ($i = 0; $i -lt $short.Count; $i++) {
        if ($short[$i] -ne $long[$i]) { return 'MISMATCH' }
    }
    # Trailing components the shorter form omitted. All-zero means the same
    # release written with fewer segments; anything else is a different build.
    for ($i = $short.Count; $i -lt $long.Count; $i++) {
        if ($long[$i] -notmatch '^0+$') { return 'PARTIAL' }
    }
    return 'MATCH'
}

function Test-VersionUsable([string]$Raw) {
    # A version string that is present but says nothing. Firebird's driver
    # answers `00.00.000`; an all-zero or empty answer is not evidence.
    if ([string]::IsNullOrWhiteSpace($Raw))       { return $false }
    if ($Raw.Trim() -match '^[0.\s]+$')           { return $false }
    return $true
}

# ═══════════════════════════════════════════════════════════════
# Phase 1 — manifest
# ═══════════════════════════════════════════════════════════════

Write-Step "Reading driver manifest"

$ProjectRoot  = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$ManifestPath = Join-Path $ProjectRoot '.github' 'drivers.json'

if (-not (Test-Path $ManifestPath)) {
    Write-Host "Driver manifest not found at $ManifestPath" -ForegroundColor Red
    Write-Host "Run this from a checkout of the odbc-crusher repository." -ForegroundColor Red
    exit $EXIT_BAD_DRIVER
}

$Manifest    = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$DriverNames = @($Manifest.drivers.PSObject.Properties.Name)

$entry = $Manifest.drivers.PSObject.Properties | Where-Object { $_.Name -eq $Driver }
if (-not $entry) {
    Write-Host "Unknown driver: '$Driver'" -ForegroundColor Red
    Write-Host "Available drivers: $($DriverNames -join ', ')" -ForegroundColor Yellow
    exit $EXIT_BAD_DRIVER
}
$d = $entry.Value

$IsMock     = ($d.source.repo -eq 'self')
$Provenance = if ($d.PSObject.Properties.Name -contains 'provenance') { $d.provenance } else { $null }

if (-not $OutDir) { $OutDir = Join-Path $ProjectRoot 'tmp' 'triage' $Driver }
$ArtifactDir = Join-Path $OutDir 'artifacts'
New-Item -ItemType Directory -Path $ArtifactDir -Force | Out-Null
$script:PreflightPath = Join-Path $OutDir 'preflight.txt'

Set-Fact 'DRIVER'          $Driver
Set-Fact 'DISPLAY'         $d.display_name
Set-Fact 'MANIFEST_VERSION' $d.version
Set-Fact 'REPO'            $d.source.repo
Set-Fact 'TAG'             $d.source.tag
Set-Fact 'PLATFORM'        $d.platform
Set-Fact 'OUT_DIR'         $OutDir
Set-Fact 'GENERATED_UTC'   ((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm') + ' UTC')
Set-Fact 'CRUSHER_COMMIT'  (git -C $ProjectRoot rev-parse --short HEAD)

$VersionSource     = if ($Provenance) { $Provenance.version_source }      else { 'unknown' }
$DriverVerReliable = if ($Provenance) { [bool]$Provenance.driver_ver_reliable } else { $true }
Set-Fact 'VERSION_SOURCE'      $VersionSource
Set-Fact 'DRIVER_VER_RELIABLE' $DriverVerReliable
if ($Provenance -and $Provenance.PSObject.Properties.Name -contains '_note') {
    Set-Fact 'PROVENANCE_NOTE' ($Provenance._note -replace '\r?\n', ' ')
}

Write-Note "$($d.display_name) — manifest version $($d.version), tag $($d.source.tag)"

# ═══════════════════════════════════════════════════════════════
# Phase 2 — resolve the CI run
# ═══════════════════════════════════════════════════════════════

Write-Step "Resolving stress-test run"

function Invoke-Gh {
    param([string[]]$GhArgs, [switch]$AllowFail)
    $out = & gh @GhArgs 2>&1
    if ($LASTEXITCODE -ne 0 -and -not $AllowFail) {
        Stop-Triage $EXIT_CI "gh $($GhArgs -join ' ') failed (exit $LASTEXITCODE): $out"
    }
    return $out
}

if ($Offline) {
    # Derive the slug from the git remote rather than asking GitHub.
    $origin   = (& git -C $ProjectRoot remote get-url origin 2>$null)
    $RepoSlug = if ($origin -match 'github\.com[:/](.+?)(\.git)?$') { $Matches[1] } else { 'unknown/unknown' }
} else {
    $RepoSlug = (Invoke-Gh @('repo', 'view', '--json', 'nameWithOwner', '-q', '.nameWithOwner')) -join ''
}
Set-Fact 'REPO_SLUG' $RepoSlug

if ($Offline) {
    $ResolvedRunId = $(if ($RunId -gt 0) { $RunId } else { 'offline' })
    Set-Fact 'RUN_CONCLUSION' 'not checked (offline)'
    Set-Fact 'RUN_CREATED'    ''
    Write-Note "Offline — using artifacts already in $ArtifactDir"
}
elseif ($RunId -gt 0) {
    Write-Note "Reusing run $RunId (no dispatch)"
    $info = (Invoke-Gh @('run', 'view', "$RunId", '--json', 'databaseId,workflowName,status,conclusion,url,createdAt')) -join '' | ConvertFrom-Json
    if ($info.workflowName -notmatch 'Stress') {
        Stop-Triage $EXIT_CI "Run $RunId belongs to workflow '$($info.workflowName)', not the stress test."
    }
    if ($info.status -ne 'completed') {
        Stop-Triage $EXIT_CI "Run $RunId is '$($info.status)', not completed. Wait for it, then re-run with the same -RunId."
    }
    $ResolvedRunId = $info.databaseId
    Set-Fact 'RUN_CONCLUSION' $info.conclusion
    Set-Fact 'RUN_CREATED'    $info.createdAt
}
elseif ($NoDispatch) {
    Write-Note "Looking for the most recent completed stress-test run"
    $runs = (Invoke-Gh @('run', 'list', '--workflow', 'stress-test.yml', '--limit', '20',
                         '--json', 'databaseId,status,conclusion,url,createdAt')) -join '' | ConvertFrom-Json
    $pick = @($runs | Where-Object { $_.status -eq 'completed' }) | Select-Object -First 1
    if (-not $pick) { Stop-Triage $EXIT_CI "No completed stress-test run found. Drop -NoDispatch to trigger one." }
    $ResolvedRunId = $pick.databaseId
    Set-Fact 'RUN_CONCLUSION' $pick.conclusion
    Set-Fact 'RUN_CREATED'    $pick.createdAt
    Write-Warn "Reusing run $ResolvedRunId — it may not have covered '$Driver'; the artifact download below is the real check."
}
else {
    # Snapshot the newest run id BEFORE dispatching, then wait for a *different*
    # one to appear. The old skill slept 8 seconds and took the most recent run,
    # which silently returned the previous run whenever the new one had not been
    # registered yet - and a previous `driver=all` run carries a matching
    # artifact, so the wrong data would have sailed through every later check.
    $before = 0
    $prev = (Invoke-Gh @('run', 'list', '--workflow', 'stress-test.yml', '--limit', '1', '--json', 'databaseId') -AllowFail) -join ''
    if ($LASTEXITCODE -eq 0 -and $prev) {
        # @() matters: `--limit 1` yields a one-element JSON array, which
        # ConvertFrom-Json unrolls to a bare object, and under StrictMode a bare
        # PSCustomObject has no .Count.
        $parsed = @($prev | ConvertFrom-Json)
        if ($parsed.Count -gt 0) { $before = [long]$parsed[0].databaseId }
    }
    Write-Note "Newest existing run before dispatch: $before"

    $defaultBranch = (Invoke-Gh @('repo', 'view', '--json', 'defaultBranchRef', '-q', '.defaultBranchRef.name')) -join ''
    Write-Note "Dispatching stress-test.yml on '$defaultBranch' for driver=$Driver"
    Invoke-Gh @('workflow', 'run', 'stress-test.yml', '--ref', $defaultBranch, '-f', "driver=$Driver") | Out-Null

    $ResolvedRunId = 0
    $deadline = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 3
        $listing = (Invoke-Gh @('run', 'list', '--workflow', 'stress-test.yml', '--event', 'workflow_dispatch',
                                '--limit', '5', '--json', 'databaseId,createdAt') -AllowFail) -join ''
        if ($LASTEXITCODE -ne 0 -or -not $listing) { continue }
        $candidate = @($listing | ConvertFrom-Json | Where-Object { [long]$_.databaseId -gt $before }) |
                     Sort-Object { [long]$_.databaseId } | Select-Object -First 1
        if ($candidate) { $ResolvedRunId = [long]$candidate.databaseId; break }
    }
    if ($ResolvedRunId -eq 0) {
        Stop-Triage $EXIT_CI "Dispatched stress-test.yml but no new run appeared within 120s. Check https://github.com/$RepoSlug/actions"
    }
    Write-Note "New run: $ResolvedRunId — waiting for it to finish"

    # `gh run watch` blocks until the run completes; it is the wait, so no
    # foreground sleep loop is needed here.
    & gh run watch "$ResolvedRunId" --interval 10 --compact
    $info = (Invoke-Gh @('run', 'view', "$ResolvedRunId", '--json', 'conclusion,createdAt')) -join '' | ConvertFrom-Json
    Set-Fact 'RUN_CONCLUSION' $info.conclusion
    Set-Fact 'RUN_CREATED'    $info.createdAt
}

Set-Fact 'RUN_ID'  $ResolvedRunId
Set-Fact 'RUN_URL' $(if ($ResolvedRunId -eq 'offline') { '' } else { "https://github.com/$RepoSlug/actions/runs/$ResolvedRunId" })
Write-Note "Run $ResolvedRunId ($($script:Facts['RUN_CONCLUSION']))"

# ═══════════════════════════════════════════════════════════════
# Phase 3 — clone or reuse the driver source
# ═══════════════════════════════════════════════════════════════

Write-Step "Preparing driver source"

$SrcPath = Join-Path $OutDir 'src'

if ($IsMock) {
    $SrcPath = Join-Path $ProjectRoot 'mock-driver'
    Set-Fact 'SRC_STATE' 'workspace'
    Write-Note "mock-driver builds from this repo — using mock-driver/ directly"
}
elseif ($SkipClone) {
    Set-Fact 'SRC_STATE' 'skipped'
    Write-Warn "-SkipClone given; the report will carry no driver-source citations"
}
else {
    $tag   = $d.source.tag
    $isSha = Test-IsCommitSha $tag
    $state = 'failed'

    if ($isSha) {
        # A commit cannot be reached by `clone --branch`, and `describe
        # --exact-match` has no tag to answer with, so the whole tag path below
        # is wrong for it end to end. github.com serves arbitrary reachable
        # object ids to `fetch`, which is all this needs; the shallow depth and
        # the reuse-before-refetch behaviour are the same as the tag path's.
        $head = if (Test-Path (Join-Path $SrcPath '.git')) {
            (& git -C $SrcPath rev-parse HEAD 2>$null)
        } else { $null }

        if ($head -and $head.StartsWith($tag)) {   # $head is always full-length
            $state = 'reused'
            Write-Note "Existing clone is already at $tag — reused"
        }
        elseif ($Offline) {
            Write-Warn "Clone is at '$head', want commit '$tag', and -Offline forbids fetching"
        }
        else {
            if (-not (Test-Path (Join-Path $SrcPath '.git'))) {
                if (Test-Path $SrcPath) {
                    Write-Warn "$SrcPath exists but is not a git repository — replacing it"
                    Remove-Item $SrcPath -Recurse -Force
                }
                & git init --quiet $SrcPath 2>&1 | ForEach-Object { Write-Note $_ }
                & git -C $SrcPath remote add origin $d.source.repo 2>&1 | ForEach-Object { Write-Note $_ }
            }
            Write-Note "Fetching $($d.source.repo) at commit $tag (depth 50)"
            & git -C $SrcPath fetch --depth 50 origin $tag 2>&1 | ForEach-Object { Write-Note $_ }
            if ($LASTEXITCODE -eq 0) {
                & git -C $SrcPath checkout --force FETCH_HEAD 2>&1 | ForEach-Object { Write-Note $_ }
                if ($LASTEXITCODE -eq 0) { $state = if ($head) { 'refetched' } else { 'cloned' } }
            }
        }
    }
    elseif (Test-Path (Join-Path $SrcPath '.git')) {
        # Reuse when the working tree is already at the tag we want. The old
        # skill ran a bare `git clone` into this path, which fails once the
        # directory exists - and then recorded "source clone failed", so every
        # repeat triage of the same driver produced a citation-free report while
        # a perfectly good clone sat on disk.
        $have     = (& git -C $SrcPath describe --tags --exact-match 2>$null)
        $haveNorm = (ConvertTo-VersionParts $have) -join '.'
        $wantNorm = (ConvertTo-VersionParts $tag)  -join '.'
        if ($have -and $haveNorm -eq $wantNorm) {
            $state = 'reused'
            Write-Note "Existing clone is already at $have — reused"
        } elseif ($Offline) {
            Write-Warn "Existing clone is at '$have', want '$tag', and -Offline forbids fetching"
        } else {
            Write-Note "Existing clone is at '$have', want '$tag' — fetching"
            & git -C $SrcPath fetch --depth 50 origin "refs/tags/${tag}:refs/tags/${tag}" 2>&1 | ForEach-Object { Write-Note $_ }
            & git -C $SrcPath checkout --force $tag 2>&1 | ForEach-Object { Write-Note $_ }
            if ($LASTEXITCODE -eq 0) { $state = 'refetched' }
        }
    }
    elseif ($Offline) {
        Write-Warn "No clone at $SrcPath and -Offline forbids cloning"
    }
    else {
        if (Test-Path $SrcPath) {
            Write-Warn "$SrcPath exists but is not a git repository — replacing it"
            Remove-Item $SrcPath -Recurse -Force
        }
        Write-Note "Cloning $($d.source.repo) at $tag (depth 50)"
        & git clone --depth 50 --branch $tag $d.source.repo $SrcPath 2>&1 | ForEach-Object { Write-Note $_ }
        if ($LASTEXITCODE -eq 0) { $state = 'cloned' }
    }

    Set-Fact 'SRC_STATE' $state
    if ($state -eq 'failed') {
        # Not fatal: crusher-side findings are still classifiable without the
        # driver source. The report says so instead of pretending otherwise.
        Write-Warn "Could not prepare driver source — findings will be caveated"
    }
}

Set-Fact 'SRC_PATH' $SrcPath
# The upstream base for permalinks. Reports get sent to driver maintainers, for
# whom a local path like ./tmp/triage/duckdb/src/... means nothing.
if (-not $IsMock) {
    Set-Fact 'PERMALINK_BASE' "$($d.source.repo)/blob/$($d.source.tag)"
} else {
    Set-Fact 'PERMALINK_BASE' ''
}

# ═══════════════════════════════════════════════════════════════
# Phase 4 — download the artifact into a cleaned directory
# ═══════════════════════════════════════════════════════════════

Write-Step "Downloading report artifact"

$ReportTxt   = Join-Path $ArtifactDir 'crusher-report.txt'
$ReportJson  = Join-Path $ArtifactDir 'crusher-report.json'
$VersionTxt  = Join-Path $ArtifactDir 'actual_version.txt'
# S2: crusher's stderr, kept separate by the run-crusher composite. Unbuffered,
# so it survives a SIGKILL that discards the report.
$ProgressTxt = Join-Path $ArtifactDir 'crusher-progress.txt'

if ($Offline) {
    Write-Note "Offline — using the artifacts already in $ArtifactDir"
}
else {
    # Delete first. On a connect failure crusher returns before report_end() and
    # writes no JSON at all, so a stale file from a previous run would otherwise
    # survive the download and be triaged as if it were this run's data.
    foreach ($f in @($ReportTxt, $ReportJson, $VersionTxt, $ProgressTxt)) {
        if (Test-Path $f) { Remove-Item $f -Force }
    }
    & gh run download "$ResolvedRunId" --repo $RepoSlug --name "report-$Driver" --dir $ArtifactDir 2>&1 |
        ForEach-Object { Write-Note $_ }
    if ($LASTEXITCODE -ne 0) {
        Stop-Triage $EXIT_NO_REPORT ("No artifact 'report-$Driver' in run $ResolvedRunId. " +
            "Either that run did not cover this driver, or the job died before the upload step. " +
            "Check https://github.com/$RepoSlug/actions/runs/$ResolvedRunId")
    }
}

Set-Fact 'REPORT_TXT_PRESENT'  (Test-Path $ReportTxt)
Set-Fact 'REPORT_JSON_PRESENT' (Test-Path $ReportJson)

$RuntimeVersion = ''
if (Test-Path $VersionTxt) { $RuntimeVersion = (Get-Content $VersionTxt -Raw).Trim() }
Set-Fact 'RUNTIME_VERSION' $RuntimeVersion

if (-not (Test-Path $ReportJson)) {
    # The old skill said "the workflow may have failed before the artifact-upload
    # step" here. That diagnosis is usually wrong: the artifact uploads with
    # `if-no-files-found: warn`, so it ships whatever exists. The real reason is
    # in the text report, which the old skill downloaded and never read.
    $reason = 'crusher-report.json is absent from the artifact.'
    if (Test-Path $ReportTxt) {
        $tail = (Get-Content $ReportTxt -Tail 25) -join ' '
        if ($tail -match 'DRIVER CRASH') {
            $reason += ' The text report ends in a DRIVER CRASH entry — the JSON run crashed before it could write a snapshot.'
        } elseif ($tail -match 'ODBC Error|could not connect|SQLSTATE') {
            $reason += ' The text report ends in an ODBC error — crusher never connected, so no JSON was written.'
        } else {
            $reason += ' The text report ends without a summary — the run did not finish.'
        }
        $reason += " Read $ReportTxt for the diagnostics."
        # The last 400 characters, not the first: the reason a run died is at
        # the end of the transcript.
        $keep = [Math]::Min(400, $tail.Length)
        Set-Fact 'NO_JSON_TAIL' ($tail.Substring($tail.Length - $keep))
    } else {
        $reason += ' No text report either — the job produced nothing.'
    }

    # S2. The text report is written to stdout, which is block-buffered on a
    # pipe, so a run killed at the wall-clock cap discards every probe result it
    # had already produced — which is why the H17 pair's first two runs were
    # indistinguishable from runs that did nothing. crusher names each probe on
    # *stderr* before running it, and the composite keeps that in its own file.
    # The last name in it is the probe that never returned.
    if (Test-Path $ProgressTxt) {
        $started = @(Get-Content $ProgressTxt | Where-Object { $_ -match '^\s*->\s' })
        if ($started.Count) {
            $last = $started[-1].Trim() -replace '^->\s*', ''
            Set-Fact 'LAST_PROBE_STARTED' $last
            Set-Fact 'PROBES_STARTED'     $started.Count
            $reason += " The last probe crusher started was '$last' ($($started.Count) started in total); if the run was killed at the cap, that is the one that did not return."
        }
    }

    Set-Fact 'NO_JSON_REASON' $reason
    Stop-Triage $EXIT_NO_REPORT $reason
}

# ═══════════════════════════════════════════════════════════════
# Phase 5 — preflight the report
# ═══════════════════════════════════════════════════════════════

Write-Step "Checking report integrity"

$report = Get-Content $ReportJson -Raw | ConvertFrom-Json
$keys   = @($report.PSObject.Properties.Name)

# --- schema version -------------------------------------------------------
# G4 asks consumers to reject a version they do not know. Artifacts predating
# G4 carry no schema_version at all (the archived duckdb report is one), so an
# absent key warns rather than aborts; an unknown *future* version aborts.
if ($keys -contains 'schema_version') {
    Set-Fact 'SCHEMA_VERSION' $report.schema_version
    if ([int]$report.schema_version -gt 1) {
        Stop-Triage $EXIT_NO_REPORT ("Report schema_version $($report.schema_version) is newer than this skill understands (1). " +
            "Update .claude/skills/triage-driver/ before trusting the field names.")
    }
} else {
    Set-Fact 'SCHEMA_VERSION' 'pre-v1'
    Write-Warn "No schema_version — artifact predates G4. Field names assumed to be the v1 set."
}

# --- completeness ---------------------------------------------------------
# Neither CI success nor crusher's exit code proves the report is whole: both
# crusher steps are continue-on-error and pass --fail-on=none, and a run killed
# at the 570s cap leaves a `complete:false` snapshot with no summary block.
$hasComplete = $keys -contains 'complete'
$isComplete  = if ($hasComplete) { [bool]$report.complete } else { $true }
$hasSummary  = $keys -contains 'summary'
Set-Fact 'COMPLETE'    $(if ($hasComplete) { $isComplete } else { 'unknown (pre-F2 artifact)' })
Set-Fact 'HAS_SUMMARY' $hasSummary

if ((-not $isComplete) -or (-not $hasSummary)) {
    $why = if (-not $isComplete) { 'complete:false' } else { 'no summary block' }
    if (-not $AllowPartial) {
        Stop-Triage $EXIT_INCOMPLETE ("Report is a partial snapshot ($why) — the run was killed before it finished. " +
            "Re-run the stress test, or pass -AllowPartial to triage it anyway (the report will be banner-marked).")
    }
    Set-Fact 'PARTIAL_REPORT' "true ($why)"
    Write-Warn "Proceeding on a PARTIAL report ($why) because -AllowPartial was given"
} else {
    Set-Fact 'PARTIAL_REPORT' 'false'
}

# --- filtered run ---------------------------------------------------------
# categories_selected is absent on a full run, so its presence is the signal.
if ($keys -contains 'categories_selected') {
    Set-Fact 'FILTERED_RUN'        'true'
    Set-Fact 'CATEGORIES_SELECTED' ($report.categories_selected -join ', ')
    Write-Warn "Filtered run — pass rate is not comparable with a full run"
} else {
    Set-Fact 'FILTERED_RUN' 'false'
}

# --- summary --------------------------------------------------------------
if ($hasSummary) {
    $s = $report.summary
    foreach ($k in @('total_tests','passed','failed','skipped','errors','informational','scored','pass_rate')) {
        if ($s.PSObject.Properties.Name -contains $k) { Set-Fact "SUMMARY_$($k.ToUpper())" $s.$k }
    }
}

# --- crashed categories ---------------------------------------------------
# A category crash discards every probe result in that category - the category
# object ends up holding only the crash entry. Nothing in the old report format
# said so, which made the pass rate quietly optimistic.
$allTests = @()
foreach ($cat in $report.categories) {
    foreach ($t in $cat.tests) {
        $allTests += [pscustomobject]@{ Category = $cat.name; Test = $t }
    }
}
$crashed = @($allTests | Where-Object { $_.Test.test_name -match 'DRIVER CRASH' })
Set-Fact 'CRASHED_CATEGORIES'      $crashed.Count
if ($crashed.Count -gt 0) {
    Set-Fact 'CRASHED_CATEGORY_NAMES' (($crashed | ForEach-Object { $_.Category }) -join ', ')
}

$availableCount = if ($keys -contains 'categories_available') { @($report.categories_available).Count } else { 0 }
Set-Fact 'CATEGORIES_RUN'       @($report.categories).Count
Set-Fact 'CATEGORIES_AVAILABLE' $(if ($availableCount) { $availableCount } else { 'unknown' })

# --- provenance -----------------------------------------------------------
$ReportedVersion = ''
if ($keys -contains 'driver_info') { $ReportedVersion = "$($report.driver_info.driver_version)".Trim() }
Set-Fact 'REPORTED_VERSION' $ReportedVersion
if (-not ($keys -contains 'driver_info')) {
    # Discovery is wrapped in one crash guard; if it faults, all four discovery
    # keys are skipped together.
    Write-Warn "No driver_info — the driver crashed during the discovery phase"
    Set-Fact 'DISCOVERY_CRASHED' 'true'
}

function Test-Provenance {
    # The old rule was: abort unless MANIFEST is a substring of RUNTIME *or*
    # REPORTED. For duckdb, clickhouse and firebird the CI job writes the
    # manifest's own `version` into actual_version.txt, so the first disjunct
    # compares the manifest with itself, always succeeds, and the only genuinely
    # observed value is never examined. It passed firebird while the driver was
    # reporting `00.00.000`. The manifest's `provenance` block now says which
    # value is real, and an unverifiable one is reported as such.
    $manifestVer = $script:Facts['MANIFEST_VERSION']

    if ($VersionSource -eq 'commit_sha') {
        Set-Fact 'PROVENANCE'        'N/A'
        Set-Fact 'PROVENANCE_DETAIL' "mock-driver tracks master; actual_version.txt is the CI commit SHA '$RuntimeVersion', not a version."
        return
    }

    if ($VersionSource -eq 'observed') {
        $verdict = Test-VersionMatch $manifestVer $RuntimeVersion
        switch ($verdict) {
            'MATCH'   { Set-Fact 'PROVENANCE' 'OK'
                        Set-Fact 'PROVENANCE_DETAIL' "Observed install '$RuntimeVersion' matches manifest '$manifestVer'." }
            'PARTIAL' { Set-Fact 'PROVENANCE' 'OK'
                        Set-Fact 'PROVENANCE_DETAIL' "Observed install '$RuntimeVersion' is a longer form of manifest '$manifestVer'." }
            'UNKNOWN' { Set-Fact 'PROVENANCE' 'UNVERIFIABLE'
                        Set-Fact 'PROVENANCE_DETAIL' "actual_version.txt is empty or unparseable; nothing to compare against '$manifestVer'." }
            default   { Set-Fact 'PROVENANCE' 'MISMATCH'
                        Set-Fact 'PROVENANCE_DETAIL' "CI installed '$RuntimeVersion' but the manifest pins '$manifestVer'." }
        }
        return
    }

    # manifest_echo (or an unannotated driver): actual_version.txt proves
    # nothing. driver_info.driver_version is the only real observation left.
    if (-not $DriverVerReliable) {
        Set-Fact 'PROVENANCE' 'UNVERIFIABLE'
        Set-Fact 'PROVENANCE_DETAIL' ("actual_version.txt is an echo of the manifest, and this driver's " +
            "SQLGetInfo(SQL_DRIVER_VER) is documented unreliable (it returned '$ReportedVersion'). " +
            "Nothing independently confirms the binary is $manifestVer — the pinned download URL is the only assurance.")
        return
    }
    if (-not (Test-VersionUsable $ReportedVersion)) {
        Set-Fact 'PROVENANCE' 'UNVERIFIABLE'
        Set-Fact 'PROVENANCE_DETAIL' ("actual_version.txt is an echo of the manifest, and the driver reported " +
            "'$ReportedVersion', which carries no information. Binary identity rests on the pinned URL alone.")
        return
    }
    $verdict = Test-VersionMatch $manifestVer $ReportedVersion
    switch ($verdict) {
        'MATCH'   { Set-Fact 'PROVENANCE' 'OK'
                    Set-Fact 'PROVENANCE_DETAIL' "Driver reported '$ReportedVersion', matching manifest '$manifestVer'. (actual_version.txt is an echo and was ignored.)" }
        'PARTIAL' { Set-Fact 'PROVENANCE' 'OK'
                    Set-Fact 'PROVENANCE_DETAIL' "Driver reported '$ReportedVersion', a longer form of manifest '$manifestVer'." }
        default   { Set-Fact 'PROVENANCE' 'MISMATCH'
                    Set-Fact 'PROVENANCE_DETAIL' "Driver reported '$ReportedVersion' but the manifest pins '$manifestVer'." }
    }
}

Test-Provenance
Write-Note "Provenance: $($script:Facts['PROVENANCE']) — $($script:Facts['PROVENANCE_DETAIL'])"

if ($script:Facts['PROVENANCE'] -eq 'MISMATCH') {
    Stop-Triage $EXIT_PROVENANCE ($script:Facts['PROVENANCE_DETAIL'] +
        " Fix .github/drivers.json (or the CI pin) so they agree, then re-run. A triage against the wrong binary is worse than none.")
}

# ═══════════════════════════════════════════════════════════════
# Phase 6 — extract the findings subset
# ═══════════════════════════════════════════════════════════════

Write-Step "Extracting findings"

# The sub-agent used to Read the whole report: 80,542 bytes for the archived
# duckdb run, of which the FAIL/ERROR entries are 12,301. type_info,
# function_info and scalar_functions are pure ballast for classification, and
# connection_string carries the password verbatim.
$interesting = @('FAIL', 'ERROR', 'SKIP_INCONCLUSIVE')
$findings = @()
$n = 0
foreach ($row in $allTests) {
    if ($interesting -notcontains $row.Test.status) { continue }
    $n++
    $o = [ordered]@{ index = $n; category = $row.Category }
    foreach ($p in $row.Test.PSObject.Properties) { $o[$p.Name] = $p.Value }
    $findings += [pscustomobject]$o
}

$bundle = [ordered]@{
    driver          = $Driver
    display_name    = $d.display_name
    manifest_version = $d.version
    repo            = $d.source.repo
    tag             = $d.source.tag
    permalink_base  = $script:Facts['PERMALINK_BASE']
    src_path        = $SrcPath
    run_url         = $script:Facts['RUN_URL']
    generated_utc   = $script:Facts['GENERATED_UTC']
    crusher_commit  = $script:Facts['CRUSHER_COMMIT']
    # connection_string is deliberately absent - it carries credentials verbatim.
    summary         = $(if ($hasSummary) { $report.summary } else { $null })
    driver_info     = $(if ($keys -contains 'driver_info') { $report.driver_info } else { $null })
    findings        = $findings
}

$FindingsPath = Join-Path $OutDir 'findings.json'
$bundle | ConvertTo-Json -Depth 12 | Set-Content -Path $FindingsPath -Encoding UTF8 -Force

Set-Fact 'FINDINGS_PATH'  $FindingsPath
Set-Fact 'FINDINGS_COUNT' $findings.Count
Set-Fact 'FINDINGS_FAIL'  @($findings | Where-Object { $_.status -eq 'FAIL' }).Count
Set-Fact 'FINDINGS_ERROR' @($findings | Where-Object { $_.status -eq 'ERROR' }).Count
Set-Fact 'FINDINGS_SKIP_INCONCLUSIVE' @($findings | Where-Object { $_.status -eq 'SKIP_INCONCLUSIVE' }).Count
Set-Fact 'REPORT_JSON_PATH' $ReportJson
Set-Fact 'REPORT_TXT_PATH'  $ReportTxt

$upper = $Driver.ToUpper()
Set-Fact 'OUTPUT_PATH' (Join-Path $OutDir "$upper-v$($d.version)-ODBC-CRUSHER-REPORT.md")

Write-Note "$($findings.Count) findings extracted -> $FindingsPath"

Set-Fact 'STATUS' 'READY'
Publish-Facts
exit $EXIT_OK
