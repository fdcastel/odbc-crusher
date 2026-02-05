# ODBC Crusher Test Configuration for Mock Driver
# Source this file before running tests: . .\test_config_mock.ps1

# Mock ODBC Driver connection string
$env:MOCK_ODBC_CONNECTION = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=100;"

# For tests that check Firebird/MySQL, also set these to use Mock Driver
$env:FIREBIRD_ODBC_CONNECTION = $env:MOCK_ODBC_CONNECTION
$env:MYSQL_ODBC_CONNECTION = $env:MOCK_ODBC_CONNECTION

Write-Host "ODBC Crusher test environment configured for Mock Driver" -ForegroundColor Green
Write-Host "Connection string: $env:MOCK_ODBC_CONNECTION" -ForegroundColor Cyan
Write-Host ""
Write-Host "Run tests with:"
Write-Host "  cd build"
Write-Host "  ctest --output-on-failure" -ForegroundColor Yellow
