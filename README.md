# ODBC Crusher 🔨

A comprehensive CLI tool for testing and debugging ODBC drivers. Built for ODBC driver developers to identify issues, validate implementations, and ensure compliance with the ODBC specification.

## Features

- ✅ **Connection Testing**: Validate ODBC connection strings and driver connectivity
- 🧪 **Function Testing**: Test ODBC API functions against expected behavior
- 📊 **Detailed Reports**: Human-readable reports with actionable diagnostics
- 🎯 **Incremental Design**: Start simple, expand over time
- 🚀 **Easy to Use**: Single command to run all tests
- 📋 **Multiple Output Formats**: Text and JSON output supported

## Current Status

**Version**: 0.1.0 (Alpha)  
**Phase**: Phase 1 - Foundation

Currently implemented:
- Basic connection testing
- Connection attribute retrieval
- Multiple connection tests
- Rich terminal output with color coding
- JSON export capability

See [PROJECT_PLAN.md](PROJECT_PLAN.md) for the full roadmap.

## Installation

### Prerequisites

- Python 3.12 or higher
- ODBC driver installed for your target database
- Windows (currently), Linux/macOS support coming later

### Install with uv (recommended)

```bash
# Clone the repository
git clone <repository-url>
cd odbc-crusher

# Install dependencies with uv
uv sync

# Run the tool
uv run odbc-crusher "DSN=YourDSN"
```

### Install for development

```bash
# Clone and navigate to directory
git clone <repository-url>
cd odbc-crusher

# Install with development dependencies
uv sync --dev

# Run tests
uv run pytest

# Format code
uv run black src/

# Lint code
uv run ruff check src/
```

## Usage

### Basic Usage

```bash
# Test a DSN-based connection
uv run odbc-crusher "DSN=PostgreSQL"

# Test with a full connection string
uv run odbc-crusher "DRIVER={SQL Server};SERVER=localhost;DATABASE=test;UID=sa;PWD=password"

# Get verbose output
uv run odbc-crusher "DSN=MyDatabase" --verbose

# Output as JSON
uv run odbc-crusher "DSN=MyDatabase" --output json
```

### Examples

**Test PostgreSQL ODBC driver:**
```bash
uv run odbc-crusher "DSN=PostgreSQL;UID=postgres;PWD=mypassword"
```

**Test SQL Server with connection string:**
```bash
uv run odbc-crusher "DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost;DATABASE=testdb;UID=sa;PWD=YourPassword"
```

**Save results to JSON:**
```bash
uv run odbc-crusher "DSN=MyDB" --output json > report.json
```

## Command-Line Options

```
Usage: odbc-crusher [OPTIONS] CONNECTION_STRING

Arguments:
  CONNECTION_STRING  ODBC connection string (e.g., "DSN=mydb;UID=user;PWD=pass")

Options:
  -o, --output [text|json]  Output format for the test report (default: text)
  -v, --verbose            Enable verbose output
  --version                Show version and exit
  --help                   Show this message and exit
```

## Output Example

```
╭────────────────────────────────────────────────────────╮
│ ODBC Crusher - ODBC Driver Testing Tool               │
│                                                  v0.1.0│
╰────────────────────────────────────────────────────────╯

Phase 1: Testing connection...

✓ Connection successful
  driver_name: PostgreSQL Unicode
  dbms_name: PostgreSQL
  dbms_version: 14.5

Phase 2: Running ODBC tests...

╭─ Test Summary ─────────────────────────────────────────╮
│ Total: 3 | Passed: 3 | Failed: 0 | Errors: 0 | Skipped: 0
╰────────────────────────────────────────────────────────╯

┏━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━┓
┃ Status ┃ Test Name             ┃ Function            ┃
┡━━━━━━━━╇━━━━━━━━━━━━━━━━━━━━━━━╇━━━━━━━━━━━━━━━━━━━━━┩
│ ✓ PASS │ test_basic_connection │ SQLConnect          │
│ ✓ PASS │ test_conn_attributes  │ SQLGetInfo          │
│ ✓ PASS │ test_multi_conn       │ SQLConnect (multi)  │
└────────┴───────────────────────┴─────────────────────┘
```

## Project Structure

```
odbc-crusher/
├── src/
│   └── odbc_crusher/
│       ├── __init__.py         # Package initialization
│       ├── cli.py              # CLI entry point
│       ├── connection.py       # Connection testing
│       ├── test_runner.py      # Test execution engine
│       ├── reporter.py         # Report generation
│       └── tests/              # Test modules
│           ├── __init__.py
│           ├── base.py         # Base test classes
│           └── connection_tests.py
├── tests/                      # Unit tests for the tool
├── docs/                       # Documentation
├── PROJECT_PLAN.md             # Development roadmap
├── AGENT_INSTRUCTIONS.md       # AI agent guidelines
├── pyproject.toml              # Project configuration
└── README.md                   # This file
```

## Development

### Adding New Tests

To add new ODBC tests:

1. Create a new test class in `src/odbc_crusher/tests/`
2. Inherit from `ODBCTest` base class
3. Implement the `run()` method
4. Register the test class in `test_runner.py`
5. Update `PROJECT_PLAN.md`

Example:

```python
from .base import ODBCTest, TestResult, TestStatus, Severity

class MetadataTests(ODBCTest):
    def run(self) -> List[TestResult]:
        self.results = []
        # Add your tests here
        return self.results
```

See [AGENT_INSTRUCTIONS.md](AGENT_INSTRUCTIONS.md) for detailed development guidelines.

### Running Tests

```bash
# Run all unit tests
uv run pytest

# Run with coverage
uv run pytest --cov=odbc_crusher --cov-report=html

# Run specific test file
uv run pytest tests/test_connection.py
```

### Code Quality

```bash
# Format code
uv run black src/ tests/

# Check linting
uv run ruff check src/ tests/

# Type checking
uv run mypy src/
```

## Roadmap

See [PROJECT_PLAN.md](PROJECT_PLAN.md) for the complete development roadmap.

**Upcoming Phases:**
- **Phase 2**: Core ODBC Functions (handles, statements, errors)
- **Phase 3**: Data Retrieval (queries, fetching, result sets)
- **Phase 4**: Metadata Functions (tables, columns, keys)
- **Phase 5**: Advanced Features (transactions, prepared statements)
- **Phase 6**: Data Type Testing (comprehensive type support)
- **Phase 7**: Performance & Compliance testing
- **Phase 8**: Enhanced reporting (HTML, XML, filtering)

## Contributing

This project follows an incremental development approach. When contributing:

1. Read [AGENT_INSTRUCTIONS.md](AGENT_INSTRUCTIONS.md) for guidelines
2. Check [PROJECT_PLAN.md](PROJECT_PLAN.md) for current priorities
3. Keep the plan updated with your changes
4. Follow the established code style
5. Add tests for new functionality

## References

- [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
- [ODBC API Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference)
- [pyodbc Documentation](https://github.com/mkleehammer/pyodbc/wiki)

## License

[Add your license here]

## Support

For issues, questions, or contributions, please [open an issue](link-to-issues).

---

**ODBC Crusher** - Making ODBC driver development easier, one test at a time.
