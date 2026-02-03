# Quick Start Guide

This is a quick reference for getting started with ODBC Crusher.

## Installation (5 minutes)

```bash
# Install uv if not already installed
powershell -c "irm https://astral.sh/uv/install.ps1 | iex"

# Clone and setup
git clone <repository-url>
cd odbc-crusher
uv sync

# Verify installation
uv run odbc-crusher --version
```

## First Test (2 minutes)

```bash
# Test with a DSN
uv run odbc-crusher "DSN=YourDSN"

# Test with connection string
uv run odbc-crusher "DRIVER={SQL Server};SERVER=localhost;Trusted_Connection=yes"
```

## Common Commands

```bash
# Verbose output
uv run odbc-crusher "DSN=MyDB" --verbose

# JSON output
uv run odbc-crusher "DSN=MyDB" --output json

# Save to file
uv run odbc-crusher "DSN=MyDB" --output json > report.json

# Run unit tests
uv run pytest

# Run with coverage
uv run pytest --cov
```

## Development Commands

```bash
# Format code
uv run black src/ tests/

# Lint code
uv run ruff check src/ tests/

# Type check
uv run mypy src/

# Run all quality checks
uv run black src/ tests/ && uv run ruff check src/ tests/ && uv run pytest
```

## Setting Up a Test DSN (Windows)

1. Open ODBC Data Source Administrator: `odbcad32.exe`
2. Click "Add"
3. Select your driver (e.g., SQL Server)
4. Configure connection details
5. Test connection
6. Click OK

## Connection String Examples

**SQL Server (Windows Auth):**
```
DRIVER={SQL Server};SERVER=localhost;Trusted_Connection=yes
```

**SQL Server (SQL Auth):**
```
DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost;DATABASE=testdb;UID=sa;PWD=password
```

**PostgreSQL:**
```
DRIVER={PostgreSQL Unicode};SERVER=localhost;PORT=5432;DATABASE=postgres;UID=postgres;PWD=password
```

**MySQL:**
```
DRIVER={MySQL ODBC 8.0 Driver};SERVER=localhost;DATABASE=test;UID=root;PWD=password
```

## Understanding Output

- **✓ PASS** (green) - Test passed successfully
- **✗ FAIL** (red) - Test failed, see details
- **⚠ ERROR** (yellow) - Test encountered an error
- **○ SKIP** (gray) - Test was skipped

## Getting Help

- Full documentation: [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)
- Project plan: [PROJECT_PLAN.md](PROJECT_PLAN.md)
- Development guide: [AGENT_INSTRUCTIONS.md](AGENT_INSTRUCTIONS.md)

## Next Steps

1. Try running against your ODBC driver
2. Review the test results
3. Check [PROJECT_PLAN.md](PROJECT_PLAN.md) for upcoming features
4. Consider contributing new tests!
