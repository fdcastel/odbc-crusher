# Mock ODBC Driver

A configurable Mock ODBC Driver for testing ODBC applications without requiring a real database.

## Features

- **Full ODBC 3.x Implementation**: All core ODBC functions implemented
- **Configurable Behavior**: Control driver behavior via connection string parameters
- **Mock Data**: Built-in mock catalog with realistic data
- **Error Injection**: Simulate failures at specific functions
- **Zero Dependencies**: No database server required
- **Cross-Platform**: Windows, Linux, macOS

## Quick Start

### Connection String

```
Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=100;
```

### Configuration Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| `Mode` | Success, Failure, Random | Overall behavior mode |
| `Catalog` | Default, Empty, Large | Mock schema preset |
| `ResultSetSize` | Number | Rows to return |
| `FailOn` | Function names | Inject failures |
| `ErrorCode` | SQLSTATE | Error code to return |
| `Latency` | e.g., 10ms | Simulated delay |
| `SilentCorruption` | None, DropInserts, MangleVarchar, TruncateNumeric, NullAsEmpty, MangleUnicode | Silently tamper with stored data while keeping ODBC return codes successful — used to validate that round-trip / verify-rows-persisted tests detect a misbehaving driver. `NullAsEmpty` returns NULL char/wchar cells as empty string with indicator=0 (Oracle-style empty-vs-null conflation). `MangleUnicode` replaces every non-ASCII byte in a fetched char/wchar cell with `?` (codepage-bound driver pattern). |
| `NativeSqlPassThrough` | true, false | When `true`, `SQLNativeSql` returns its input verbatim without translating any ODBC escape sequence (`{fn ...}`, `{d ...}`, `{oj ...}`, `{CALL ...}`, etc.). Drives the PORT plan port 4 e2e canary; spec-compliant drivers must always translate. |
| `Procedures` | (default) / BrokenInout | When `BrokenInout`, the canonical `MOCK_INOUT(IN n, OUT m, INOUT s)` procedure runs but its callback returns no output values. Mocks drivers that accept `{?=CALL …}` syntactically but never write back to `SQL_PARAM_OUTPUT` / `SQL_PARAM_INPUT_OUTPUT` bound buffers. Drives the PORT plan port 3 e2e canary. |
| `ArrayBindRowFailsAt` | Number (default 0) | When `> 0`, the Nth row (1-indexed) of any array-parameter execute is forced to fail with SQLSTATE `23000`; surrounding rows execute normally. Drives the PORT plan port 6 per-row status probe. |
| `SupportsArrayBind` | true (default) / false | When `false`, `SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE, > 1)` returns SQL_ERROR with SQLSTATE `HYC00`. Mocks drivers that don't implement array-parameter execution. Drives the PORT plan port 6 SKIP_UNSUPPORTED canary. |

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Installation

### Windows
Register the driver in ODBC Administrator or use the registry template.

### Linux/macOS
Add to `/etc/odbcinst.ini`:
```ini
[Mock ODBC Driver]
Description = Mock ODBC Driver for Testing
Driver = /usr/local/lib/libmockodbc.so
```

## Mock Catalog

Default tables:
- `USERS` - User accounts
- `ORDERS` - Order records
- `PRODUCTS` - Product catalog
- `ORDER_ITEMS` - Order line items

## Contributing

### Thread-safety invariant: `HandleLock`

Every **public ODBC entry point** (anything declared `SQLRETURN SQL_API …`
and exported via `mockodbc.def`) must lock its primary handle and clear
its diagnostic stack on entry. The canonical pattern is:

```cpp
auto* stmt = validate_stmt_handle(hstmt);
if (!stmt) return SQL_INVALID_HANDLE;
HandleLock lock(stmt);           // Per-handle mutex from OdbcHandle
stmt->clear_diagnostics();       // Spec: most entry points clear on entry
```

Exceptions:

- **`SQLGetDiagRec` / `SQLGetDiagField`** — these *read* the diagnostic
  stack; they must never clear it.
- **`SQLAllocHandle(SQL_HANDLE_*)`** — the target handle does not exist yet.

Diagnostic clearing is a spec requirement (ODBC 3.x §15.2, SQLGetDiagRec):
all subsequent diagnostics from a function call replace the previous
stack unless the caller explicitly reads first. If a function forgets to
clear, state from the previous call leaks through and tests that check
`SQLGetDiagRec(..., 1, ...)` see the wrong error.

**Review checklist for new / changed entry points:**

1. Does the first line after the validate/`SQL_INVALID_HANDLE` check
   take `HandleLock lock(handle);`? If no — add it.
2. Does the next line call `handle->clear_diagnostics();`? If no — add it,
   unless this is a diagnostic-reading function.
3. If the function delegates to another public ODBC function (e.g. a
   `SQL…W` wrapper calling its ANSI sibling), make sure you don't
   re-lock the same handle — `HandleLock` is not recursive.

A CI check under `Build Mock ODBC Driver` (`Check mockodbc.def export drift`)
enforces that every export in `mockodbc.def` has a matching `SQLRETURN
SQL_API <name>(` implementation. There is no automated enforcement of the
lock/clear invariant yet — rely on code review.

## License

MIT License - See LICENSE file
