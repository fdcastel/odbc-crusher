# Getting Started with ODBC Crusher

This guide will help you set up and run ODBC Crusher for the first time.

## Prerequisites

### 1. Python Installation

You need Python 3.12 or higher. Check your version:

```bash
python --version
```

If you need to install Python:
- Download from [python.org](https://www.python.org/downloads/)
- Or use a version manager like [pyenv](https://github.com/pyenv/pyenv)

### 2. uv Package Manager

ODBC Crusher uses `uv` for package management. Install it:

```bash
# On Windows (PowerShell)
powershell -c "irm https://astral.sh/uv/install.ps1 | iex"

# On macOS/Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
```

Verify installation:
```bash
uv --version
```

### 3. ODBC Driver

You need an ODBC driver for your target database. Common options:

**Windows:**
- SQL Server: Built-in or install [Microsoft ODBC Driver for SQL Server](https://learn.microsoft.com/en-us/sql/connect/odbc/download-odbc-driver-for-sql-server)
- PostgreSQL: [psqlODBC](https://www.postgresql.org/ftp/odbc/versions/)
- MySQL: [MySQL Connector/ODBC](https://dev.mysql.com/downloads/connector/odbc/)

**Verify installed drivers:**
```bash
# Windows: Open ODBC Data Source Administrator
odbcad32.exe
```

## Installation

### Option 1: Quick Start (Using uv)

```bash
# Clone the repository
git clone <repository-url>
cd odbc-crusher

# Install dependencies
uv sync

# Verify installation
uv run odbc-crusher --version
```

### Option 2: Development Setup

```bash
# Clone the repository
git clone <repository-url>
cd odbc-crusher

# Install with development dependencies
uv sync --dev

# Verify installation
uv run odbc-crusher --version

# Run tests to ensure everything works
uv run pytest
```

## Setting Up an ODBC Data Source (DSN)

### Windows

1. Open **ODBC Data Source Administrator**:
   - Press `Win + R`
   - Type `odbcad32.exe`
   - Press Enter

2. Click **Add** to create a new DSN

3. Select your driver and click **Finish**

4. Configure the connection:
   - **Data Source Name**: Choose a name (e.g., "TestDB")
   - **Server**: Your database server
   - **Database**: Database name
   - **Authentication**: Username/password or Windows auth

5. Click **Test Connection** to verify

6. Click **OK** to save

## Running Your First Test

### Test with a DSN

```bash
# Replace "TestDB" with your DSN name
uv run odbc-crusher "DSN=TestDB"
```

### Test with a Connection String

**SQL Server:**
```bash
uv run odbc-crusher "DRIVER={SQL Server};SERVER=localhost;DATABASE=master;Trusted_Connection=yes"
```

**PostgreSQL:**
```bash
uv run odbc-crusher "DRIVER={PostgreSQL Unicode};SERVER=localhost;PORT=5432;DATABASE=postgres;UID=postgres;PWD=password"
```

**MySQL:**
```bash
uv run odbc-crusher "DRIVER={MySQL ODBC 8.0 Driver};SERVER=localhost;DATABASE=test;UID=root;PWD=password"
```

### Enable Verbose Output

```bash
uv run odbc-crusher "DSN=TestDB" --verbose
```

### Save Results to JSON

```bash
uv run odbc-crusher "DSN=TestDB" --output json > report.json
```

## Understanding the Output

### Text Output (Default)

```
╭────────────────────────────────────────────────────────╮
│ ODBC Crusher - ODBC Driver Testing Tool               │
╰────────────────────────────────────────────────────────╯

Phase 1: Testing connection...
✓ Connection successful

Phase 2: Running ODBC tests...

╭─ Test Summary ─────────────────────────────────────────╮
│ Total: 3 | Passed: 3 | Failed: 0 | Errors: 0          │
╰────────────────────────────────────────────────────────╯
```

**Status Icons:**
- ✓ PASS - Test passed successfully
- ✗ FAIL - Test failed (expected behavior not met)
- ⚠ ERROR - Test encountered an error
- ○ SKIP - Test was skipped

### JSON Output

```json
{
  "summary": {
    "total": 3,
    "passed": 3,
    "failed": 0,
    "skipped": 0,
    "errors": 0
  },
  "results": [
    {
      "test_name": "test_basic_connection",
      "function": "SQLConnect/SQLDisconnect",
      "status": "PASS",
      "expected": "Connection established and closed successfully",
      "actual": "Connection established and closed successfully",
      "diagnostic": null,
      "severity": "INFO",
      "duration_ms": 45.23
    }
  ]
}
```

## Common Issues

### Issue: "Data source name not found"

**Error:**
```
✗ Connection failed: [IM002] Data source name not found
```

**Solution:**
- Verify DSN exists in ODBC Data Source Administrator
- Check spelling of DSN name (case-sensitive)
- Ensure you're using the correct architecture (32-bit vs 64-bit)

### Issue: "Driver not found"

**Error:**
```
✗ Connection failed: [IM003] Specified driver could not be loaded
```

**Solution:**
- Install the ODBC driver for your database
- Verify driver name matches exactly (check in odbcad32.exe)
- Check architecture mismatch (32-bit Python with 64-bit driver)

### Issue: "Login failed"

**Error:**
```
✗ Connection failed: [28000] Login failed for user
```

**Solution:**
- Verify username and password
- Check user has permission to access the database
- Try Windows authentication instead (Trusted_Connection=yes)

### Issue: "Connection timeout"

**Error:**
```
✗ Connection failed: Connection timeout
```

**Solution:**
- Check database server is running
- Verify network connectivity
- Check firewall settings
- Verify server name and port

## Next Steps

1. **Explore the codebase**: See [README.md](../README.md) for project structure
2. **Add custom tests**: Check [AGENT_INSTRUCTIONS.md](../AGENT_INSTRUCTIONS.md) for development guidelines
3. **Review the roadmap**: See [PROJECT_PLAN.md](../PROJECT_PLAN.md) for upcoming features
4. **Run the test suite**: Use `uv run pytest` to run unit tests

## Getting Help

- Check [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
- Review [pyodbc documentation](https://github.com/mkleehammer/pyodbc/wiki)
- Open an issue on the project repository

---

Happy testing! 🔨
