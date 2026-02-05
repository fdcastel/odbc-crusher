# Register Mock ODBC Driver
# This script must be run as Administrator

$ErrorActionPreference = "Stop"

$driverPath = Join-Path $PSScriptRoot "..\build\Release\mockodbc.dll"
$driverPath = Resolve-Path $driverPath

if (-not (Test-Path $driverPath)) {
    Write-Error "Driver DLL not found at: $driverPath"
    Write-Host "Please build the driver first using CMake"
    exit 1
}

Write-Host "Registering Mock ODBC Driver..."
Write-Host "Driver path: $driverPath"

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Warning "This script should be run as Administrator for system-wide installation."
    Write-Host "Attempting user-level registration instead..."
    
    # Register in HKEY_CURRENT_USER instead
    $registryPath = "HKCU:\SOFTWARE\ODBC\ODBCINST.INI"
} else {
    # Register in HKEY_LOCAL_MACHINE for system-wide
    $registryPath = "HKLM:\SOFTWARE\ODBC\ODBCINST.INI"
}

# Create driver entry
$driverKey = "$registryPath\Mock ODBC Driver"
if (-not (Test-Path $driverKey)) {
    New-Item -Path $driverKey -Force | Out-Null
}

Set-ItemProperty -Path $driverKey -Name "Driver" -Value $driverPath
Set-ItemProperty -Path $driverKey -Name "Setup" -Value $driverPath
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

Write-Host "Mock ODBC Driver registered successfully!" -ForegroundColor Green
Write-Host ""
Write-Host "You can now use it with connection strings like:"
Write-Host '  "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=100;"' -ForegroundColor Cyan
Write-Host ""
Write-Host "To verify installation, run:"
Write-Host '  Get-OdbcDriver | Where-Object {$_.Name -like "*Mock*"}' -ForegroundColor Cyan
