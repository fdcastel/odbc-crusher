#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Fetches the latest stress-test workflow results and clones driver source repositories.

.DESCRIPTION
    - Identifies the last run of .github/workflows/stress-test.yml
    - For each analyzed ODBC driver (excluding mock driver):
      - Clones (or pulls) the official GitHub repository into ./tmp/external/<REPO_NAME>
      - Checks out the exact tag matching the tested binary version
      - Downloads the workflow artifacts (crusher-report.txt / .json)
      - Writes an LLM analysis prompt into ./recommendations/prompts/<DRIVER>-ODBC-CRUSHER-PROMPT.md

    The script is idempotent: safe to run multiple times.
    Must be run from the project root directory.

.NOTES
    Binary versions tested in CI must match exactly the source tags checked out here.
    See .github/workflows/stress-test.yml for the pinned versions.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Configuration ──────────────────────────────────────────────
# F5: this used to be a hand-maintained $Drivers hashtable carrying each
# driver's repo and tag - the same data as `.github/drivers.json`, which is
# the declared single source of truth and which stress-test.yml reads at
# runtime. The two had already drifted: the manifest has seven drivers, the
# hashtable had five, and firebird was missing entirely, so this script would
# silently skip the driver the project cares most about.
#
# It reads the manifest now. A driver added there appears here with no edit.
$ProjectRoot   = $PSScriptRoot
$ManifestPath  = Join-Path $ProjectRoot '.github' 'drivers.json'

if (-not (Test-Path $ManifestPath)) {
    throw "Driver manifest not found at $ManifestPath - run this from the project root."
}

$Manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json

$Drivers = [ordered]@{}
foreach ($entry in $Manifest.drivers.PSObject.Properties) {
    $name   = $entry.Name
    $source = $entry.Value.source

    # mock-driver's "repo" is this repository; there is nothing to clone, and
    # the triage skill special-cases it for the same reason.
    if (-not $source -or $source.repo -eq 'self') { continue }

    $Drivers[$name] = @{
        RepoUrl  = "$($source.repo).git"
        RepoName = ($source.repo -split '/')[-1]
        Tag      = $source.tag
        Artifact = "report-$name"
    }
}

if ($Drivers.Count -eq 0) {
    throw "No clonable drivers in $ManifestPath - the manifest format may have changed."
}

Write-Host "Drivers from manifest: $($Drivers.Keys -join ', ')"

$ExternalDir   = Join-Path $ProjectRoot 'tmp' 'external'
$RecommDir     = Join-Path $ProjectRoot 'recommendations'
$PromptsDir    = Join-Path $RecommDir   'prompts'
$WorkflowFile  = '.github/workflows/stress-test.yml'

# ── Helpers ────────────────────────────────────────────────────

function Ensure-Directory([string]$Path) {
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
        Write-Host "  Created directory: $Path"
    }
}

function Get-LastWorkflowRunId {
    Write-Host "`n=== Finding last stress-test workflow run ===" -ForegroundColor Cyan
    $json = gh run list --workflow $WorkflowFile --repo fdcastel/odbc-crusher --limit 1 --json databaseId,status,conclusion,createdAt 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to list workflow runs: $json"
    }
    $runs = $json | ConvertFrom-Json
    if ($runs.Count -eq 0) {
        throw "No runs found for workflow $WorkflowFile"
    }
    $run = $runs[0]
    Write-Host "  Run ID    : $($run.databaseId)"
    Write-Host "  Status    : $($run.status)"
    Write-Host "  Conclusion: $($run.conclusion)"
    Write-Host "  Created   : $($run.createdAt)"
    return $run.databaseId
}

function Clone-OrFetch([string]$RepoUrl, [string]$DestPath) {
    if (Test-Path (Join-Path $DestPath '.git')) {
        Write-Host "  Repository exists – fetching..."
        git -C $DestPath fetch --all --tags 2>&1 | ForEach-Object { Write-Host "    $_" }
    } else {
        Write-Host "  Cloning $RepoUrl ..."
        Ensure-Directory $DestPath
        git clone $RepoUrl $DestPath 2>&1 | ForEach-Object { Write-Host "    $_" }
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Git operation returned exit code $LASTEXITCODE for $RepoUrl"
    }
}

function Checkout-Tag([string]$DestPath, [string]$Tag) {
    Write-Host "  Checking out tag: $Tag"
    git -C $DestPath checkout $Tag 2>&1 | ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to checkout tag '$Tag' (exit code $LASTEXITCODE)"
    }
}

function Download-Artifact([string]$RunId, [string]$ArtifactName, [string]$DestDir) {
    Write-Host "  Downloading artifact '$ArtifactName' ..."
    Ensure-Directory $DestDir

    # Remove existing report files so gh run download won't fail on overwrite
    foreach ($f in @('crusher-report.txt', 'crusher-report.json')) {
        $existing = Join-Path $DestDir $f
        if (Test-Path $existing) { Remove-Item $existing -Force }
    }

    # gh run download extracts into the destination directory
    gh run download $RunId --repo fdcastel/odbc-crusher --name $ArtifactName --dir $DestDir 2>&1 | ForEach-Object { Write-Host "    $_" }
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Failed to download artifact '$ArtifactName' (exit code $LASTEXITCODE). The job may not have produced artifacts."
    }
}

function Write-AnalysisPrompt([string]$DriverName, [string]$RepoName, [string]$Tag, [string]$PromptPath) {
    $repoRelPath   = "tmp/external/$RepoName"
    $reportTxt     = "$repoRelPath/crusher-report.txt"
    $reportJson    = "$repoRelPath/crusher-report.json"

    $prompt = @"
# ODBC Crusher Analysis Prompt – $DriverName

**Driver version**: ``$Tag``
**Source code**: ``$repoRelPath`` (checked out at tag ``$Tag``)
**Reports**:
- ``$reportTxt``
- ``$reportJson``

## Instructions

Do a critical analysis of what ``odbc-crusher`` says about the ``$DriverName`` driver and review its source code (at exactly this version) to see what the driver actually implements.

**GOAL:** For each **failed** or **skipped** test, investigate its result against the real ``$DriverName`` sources:

1. **If the odbc-crusher report is CORRECT** (the driver really has the deficiency):
    - Write a recommendation in ``recommendations/${DriverName}_ODBC_RECOMMENDATIONS.md`` explaining what the ``$DriverName`` developers should do to fix it.
    - Finish the recommendation with the following line: 
      ``````
      ---
      Generated by ODBC Crusher -- https://github.com/fdcastel/odbc-crusher/
      ``````

2. **If the odbc-crusher report is WRONG** (the driver is fine, odbc-crusher misjudged it):
    - Record it as a bug in ``PROJECT_PLAN.md`` for future fix.
"@

    Set-Content -Path $PromptPath -Value $prompt -Encoding UTF8 -Force
    Write-Host "  Wrote prompt: $PromptPath"
}

# ── Main ───────────────────────────────────────────────────────

Write-Host "`n╔══════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host   "║   fetch-stress-test.ps1 – ODBC Crusher       ║" -ForegroundColor Green
Write-Host   "╚══════════════════════════════════════════════╝`n" -ForegroundColor Green

# Ensure output directories exist
Ensure-Directory $ExternalDir
Ensure-Directory $PromptsDir

# Step 1 – Find the last workflow run
$runId = Get-LastWorkflowRunId

# Step 2 – Process each driver
foreach ($driverName in $Drivers.Keys) {
    $info     = $Drivers[$driverName]
    $repoDir  = Join-Path $ExternalDir $info.RepoName

    Write-Host "`n--- $($driverName.ToUpper()) ---" -ForegroundColor Yellow

    # 2a – Clone or fetch the driver source
    Clone-OrFetch -RepoUrl $info.RepoUrl -DestPath $repoDir

    # 2b – Checkout the exact tag matching the tested binary
    Checkout-Tag -DestPath $repoDir -Tag $info.Tag

    # 2c – Download artifacts into the repo directory
    Download-Artifact -RunId $runId -ArtifactName $info.Artifact -DestDir $repoDir

    # 2d – Write analysis prompt
    $promptFile = Join-Path $PromptsDir "$driverName-ODBC-CRUSHER-PROMPT.md"
    Write-AnalysisPrompt -DriverName $driverName -RepoName $info.RepoName -Tag $info.Tag -PromptPath $promptFile
}

Write-Host "`n✅ Done." -ForegroundColor Green
Write-Host "   Driver sources : $ExternalDir" -ForegroundColor Green
Write-Host "   Prompts        : $PromptsDir" -ForegroundColor Green
Write-Host "   Recommendations: $RecommDir`n" -ForegroundColor Green
