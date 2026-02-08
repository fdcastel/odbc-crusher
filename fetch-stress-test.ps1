#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Fetches the latest stress-test workflow results and clones driver source repositories.

.DESCRIPTION
    - Identifies the last run of .github/workflows/stress-test.yml
    - For each analyzed ODBC driver (excluding MSSQL and mock driver):
      - Clones (or pulls) the official GitHub repository into ./tmp/external/<REPO_NAME>
      - Downloads the workflow artifacts (crusher-report.txt / .json)
      - Writes an LLM analysis prompt into ./tmp/external/<DRIVER>-ODBC-CRUSHER-PROMPT.md

    The script is idempotent: safe to run multiple times.
    Must be run from the project root directory.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ── Configuration ──────────────────────────────────────────────
# Each entry maps a workflow job / artifact name to its official GitHub repo.
$Drivers = [ordered]@{
    postgresql = @{
        RepoUrl  = 'https://github.com/postgresql-interfaces/psqlodbc.git'
        RepoName = 'psqlodbc'
        Artifact = 'report-postgresql'
    }
    mariadb = @{
        RepoUrl  = 'https://github.com/mariadb-corporation/mariadb-connector-odbc.git'
        RepoName = 'mariadb-connector-odbc'
        Artifact = 'report-mariadb'
    }
    mysql = @{
        RepoUrl  = 'https://github.com/mysql/mysql-connector-odbc.git'
        RepoName = 'mysql-connector-odbc'
        Artifact = 'report-mysql'
    }
    duckdb = @{
        RepoUrl  = 'https://github.com/duckdb/duckdb-odbc.git'
        RepoName = 'duckdb-odbc'
        Artifact = 'report-duckdb'
    }
    oracle = @{
        # Oracle Instant Client ODBC is closed-source; link to docs instead
        RepoUrl  = 'https://github.com/oracle/odpi.git'
        RepoName = 'odpi'
        Artifact = 'report-oracle'
    }
    db2 = @{
        # IBM DB2 CLI/ODBC is closed-source; link to community samples
        RepoUrl  = 'https://github.com/ibmdb/db2drivers.git'
        RepoName = 'db2drivers'
        Artifact = 'report-db2'
    }
    clickhouse = @{
        RepoUrl  = 'https://github.com/ClickHouse/clickhouse-odbc.git'
        RepoName = 'clickhouse-odbc'
        Artifact = 'report-clickhouse'
    }
    firebird = @{
        RepoUrl  = 'https://github.com/FirebirdSQL/firebird-odbc-driver.git'
        RepoName = 'firebird-odbc-driver'
        Artifact = 'report-firebird'
    }
}

$ProjectRoot   = $PSScriptRoot
$ExternalDir   = Join-Path $ProjectRoot 'tmp' 'external'
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

function Clone-OrPull([string]$RepoUrl, [string]$DestPath) {
    if (Test-Path (Join-Path $DestPath '.git')) {
        Write-Host "  Repository exists – fetching and pulling latest..."
        # Fetch all remotes first
        git -C $DestPath fetch --all 2>&1 | ForEach-Object { Write-Host "    $_" }
        # Determine default branch (main or master)
        $defaultBranch = git -C $DestPath symbolic-ref refs/remotes/origin/HEAD 2>$null
        if ($defaultBranch) {
            $defaultBranch = $defaultBranch -replace '^refs/remotes/origin/', ''
        } else {
            # Fallback: try common branch names
            $branches = git -C $DestPath branch -r 2>$null
            if ($branches -match 'origin/main') { $defaultBranch = 'main' }
            elseif ($branches -match 'origin/master') { $defaultBranch = 'master' }
            else { $defaultBranch = 'main' }
        }
        Write-Host "    Default branch: $defaultBranch"
        # Checkout default branch (in case of detached HEAD) and pull
        git -C $DestPath checkout $defaultBranch 2>&1 | ForEach-Object { Write-Host "    $_" }
        git -C $DestPath pull --ff-only 2>&1 | ForEach-Object { Write-Host "    $_" }
    } else {
        Write-Host "  Cloning $RepoUrl ..."
        Ensure-Directory $DestPath
        # Clone into existing (possibly empty) directory
        git clone $RepoUrl $DestPath 2>&1 | ForEach-Object { Write-Host "    $_" }
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Git operation returned exit code $LASTEXITCODE for $RepoUrl"
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

function Write-AnalysisPrompt([string]$DriverName, [string]$RepoName, [string]$PromptPath) {
    $repoRelPath   = "tmp/external/$RepoName"
    $reportTxt     = "$repoRelPath/crusher-report.txt"
    $reportJson    = "$repoRelPath/crusher-report.json"

    $prompt = @"
# ODBC Crusher Analysis Prompt – $DriverName

The source code for the ``$DriverName`` ODBC driver is at ``$repoRelPath``.

The odbc-crusher reports for the ``$DriverName`` driver are at:
- ``$reportTxt``
- ``$reportJson``

## Instructions

Do a critical analysis of what ``odbc-crusher`` says about the driver and review its source code to see what the driver actually implements.

### GOAL

For each **failed** or **skipped** test, investigate its result against the real ``$DriverName`` sources:

1. **If the odbc-crusher report is CORRECT** (the driver really has the deficiency):
   Write a recommendation in ``${DriverName}_ODBC_RECOMMENDATIONS.md`` explaining what the ``$DriverName`` developers should do to fix it.

2. **If the odbc-crusher report is WRONG** (the driver is fine, odbc-crusher misjudged it):
   Record it as a bug in ``PROJECT_PLAN.md`` for future fix (aggregate all these bugs in a new **Phase 20**).
"@

    Set-Content -Path $PromptPath -Value $prompt -Encoding UTF8 -Force
    Write-Host "  Wrote prompt: $PromptPath"
}

# ── Main ───────────────────────────────────────────────────────

Write-Host "`n╔══════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host   "║   fetch-stress-test.ps1 – ODBC Crusher       ║" -ForegroundColor Green
Write-Host   "╚══════════════════════════════════════════════╝`n" -ForegroundColor Green

# Ensure tmp/external exists
Ensure-Directory $ExternalDir

# Step 1 – Find the last workflow run
$runId = Get-LastWorkflowRunId

# Step 2 – Process each driver
foreach ($driverName in $Drivers.Keys) {
    $info     = $Drivers[$driverName]
    $repoDir  = Join-Path $ExternalDir $info.RepoName

    Write-Host "`n--- $($driverName.ToUpper()) ---" -ForegroundColor Yellow

    # 2a – Clone or pull the driver source
    Clone-OrPull -RepoUrl $info.RepoUrl -DestPath $repoDir

    # 2b – Download artifacts into the repo directory
    Download-Artifact -RunId $runId -ArtifactName $info.Artifact -DestDir $repoDir

    # 2c – Write analysis prompt
    $promptFile = Join-Path $ExternalDir "$driverName-ODBC-CRUSHER-PROMPT.md"
    Write-AnalysisPrompt -DriverName $driverName -RepoName $info.RepoName -PromptPath $promptFile
}

Write-Host "`n✅ Done. All driver repositories and reports are in: $ExternalDir`n" -ForegroundColor Green
