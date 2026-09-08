# Register Mock ODBC Driver
# This script must be run as Administrator
#
# Usage:
#   .\register.ps1                   # Auto-detect DLL location
#   .\register.ps1 -DriverPath C:\path\to\mockodbc.dll  # Explicit path

param(
    [string]$DriverPath
)

$ErrorActionPreference = "Stop"

if (-not $DriverPath) {
    # Try: same directory as this script (release package layout)
    $candidate = Join-Path $PSScriptRoot "mockodbc.dll"
    if (Test-Path $candidate) {
        $DriverPath = (Resolve-Path $candidate).Path
    } else {
        # Try: build output relative to script (development layout).
        #
        # D30: this looked only in ..\build\Release\, while this repo builds
        # to build\Debug\ as often as not - so on a Debug tree the script
        # fell through to "Driver DLL not found" and the developer had to
        # discover -DriverPath. Both are searched now, newest first, because
        # whichever was built most recently is the one being worked on.
        $candidates = @(
            (Join-Path $PSScriptRoot "..\build\Release\mockodbc.dll"),
            (Join-Path $PSScriptRoot "..\build\Debug\mockodbc.dll")
        ) | Where-Object { Test-Path $_ } |
            Sort-Object { (Get-Item $_).LastWriteTime } -Descending

        if ($candidates) {
            $DriverPath = (Resolve-Path $candidates[0]).Path
            if ($candidates.Count -gt 1) {
                Write-Host "Found more than one build; using the most recent: $DriverPath"
            }
        }
    }
}

if (-not $DriverPath -or -not (Test-Path $DriverPath)) {
    Write-Error "Driver DLL not found. Please specify -DriverPath or place mockodbc.dll next to this script."
    exit 1
}

# D39: an explicitly supplied -DriverPath used to be written to the registry
# exactly as given, so `register.ps1 -DriverPath mock-driver/build/Debug/
# mockodbc.dll` - the form this repo's own docs use - registered a *relative*
# path. The Driver Manager then resolved it against each process's working
# directory: odbc-crusher run from the repo root loaded the driver, while the
# CTest suites, which run from build/tests, silently skipped every
# mock-backed test. The auto-detect branches above already resolved; this
# makes the explicit branch agree.
$DriverPath = (Resolve-Path $DriverPath).Path

Write-Host "Registering Mock ODBC Driver..."
Write-Host "Driver path: $DriverPath"

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    # D30: this used to fall back to HKCU\SOFTWARE\ODBC\ODBCINST.INI and then
    # print "registered successfully!". The Windows Driver Manager reads
    # ODBCINST.INI from HKLM only - the per-user hive holds DSNs (ODBC.INI),
    # not drivers - so that registration did nothing whatsoever, and the
    # developer went looking for the bug in their connection string.
    #
    # Failing is the honest answer. A fallback that cannot work is worse than
    # no fallback, because it costs the time it takes to find out.
    Write-Error @"
Administrator rights are required to register an ODBC driver.

The Windows Driver Manager reads driver registrations from
HKLM:\SOFTWARE\ODBC\ODBCINST.INI, which needs elevation. There is no
per-user equivalent for drivers - HKCU holds DSNs, not drivers - so an
unelevated registration would silently do nothing.

Re-run this from an elevated PowerShell prompt.
"@
    exit 1
}

# D30: WOW64. A 32-bit PowerShell writes HKLM:\SOFTWARE\... into
# WOW6432Node, where a 64-bit application's Driver Manager never looks - the
# registration appears to succeed and the driver is invisible. The mock is
# built 64-bit, so registering it from a 32-bit shell is always a mistake.
if (-not [Environment]::Is64BitProcess) {
    Write-Error @"
This is a 32-bit PowerShell process.

HKLM:\SOFTWARE\ODBC\ODBCINST.INI is redirected to WOW6432Node here, so the
driver would be registered where 64-bit applications never look. The mock
driver is built 64-bit.

Re-run this from a 64-bit PowerShell (%SystemRoot%\System32\WindowsPowerShell
or pwsh).
"@
    exit 1
}

# Register in HKEY_LOCAL_MACHINE for system-wide
$registryPath = "HKLM:\SOFTWARE\ODBC\ODBCINST.INI"

# Create driver entry
$driverKey = "$registryPath\Mock ODBC Driver"
if (-not (Test-Path $driverKey)) {
    New-Item -Path $driverKey -Force | Out-Null
}

Set-ItemProperty -Path $driverKey -Name "Driver" -Value $DriverPath
Set-ItemProperty -Path $driverKey -Name "Setup" -Value $DriverPath
Set-ItemProperty -Path $driverKey -Name "APILevel" -Value "2"
Set-ItemProperty -Path $driverKey -Name "ConnectFunctions" -Value "YYY"
Set-ItemProperty -Path $driverKey -Name "DriverODBCVer" -Value "03.80"
Set-ItemProperty -Path $driverKey -Name "FileUsage" -Value 0 -Type DWord
Set-ItemProperty -Path $driverKey -Name "SQLLevel" -Value "1"
Set-ItemProperty -Path $driverKey -Name "UsageCount" -Value 1 -Type DWord

# Add to installed drivers list
$driversKey = "$registryPath\ODBC Drivers"
if (-not (Test-Path $driversKey)) {
    New-Item -Path $driversKey -Force | Out-Null
}
Set-ItemProperty -Path $driversKey -Name "Mock ODBC Driver" -Value "Installed"

# D30: read it back rather than announce. Every failure this row lists shares
# one shape - the script says "registered" and the registration is not one the
# Driver Manager will use - so the last thing it does is ask the Driver
# Manager what it sees.
$seen = Get-OdbcDriver -Name "Mock ODBC Driver" -Platform "64-bit" -ErrorAction SilentlyContinue
if (-not $seen) {
    Write-Error "Registration wrote the keys but the Driver Manager does not list the driver. Check $driverKey."
    exit 1
}
$seenPath = $seen.Attribute["Driver"]
if ($seenPath -ne $DriverPath) {
    Write-Error @"
The Driver Manager lists a different binary than the one just registered.

  wanted:     $DriverPath
  registered: $seenPath

An earlier registration is probably still in place.
"@
    exit 1
}

Write-Host "Mock ODBC Driver registered successfully!" -ForegroundColor Green
Write-Host "Driver Manager reports: $seenPath"
Write-Host ""
Write-Host "You can now use it with connection strings like:"
Write-Host '  "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=100;"' -ForegroundColor Cyan
Write-Host ""
Write-Host "To verify installation, run:"
Write-Host '  Get-OdbcDriver | Where-Object {$_.Name -like "*Mock*"}' -ForegroundColor Cyan
