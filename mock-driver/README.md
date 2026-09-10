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
| `Mode` | Success, Failure, Random, Partial | Overall behavior mode. `Partial` fails only what `FailOn` names — but `FailOn` works in every mode, so `Partial` is now just the mode that does nothing else. |
| `Catalog` | Default, Empty, Large | Mock schema preset. A name that is not one of the three is **refused at connect** with `08001` — this driver's databases are its presets, and asking for one it does not have is the ordinary reason a connect fails. |
| `Database`, `DBNAME` | Default, Empty, Large | Accepted spellings of `Catalog`, so a caller can spoil the database name without knowing this driver's own vocabulary. `Catalog` wins where both are given. |
| `ConnectDiagnostics` | Wellformed (default), Garbled | How a refused connect describes itself. `Garbled` returns no SQLSTATE, one byte of the message with the length of the whole of it, and a native code that moves between identical attempts — the Firebird ODBC PR #298 shape, and the lever `test_failed_connect_diagnostics_are_wellformed` fails against. |
| `ColumnWiseStride` | Correct (default), BufferLength | How a column-wise parameter array is stepped. `BufferLength` reproduces #299 (Firebird ODBC PR #308): the application's `BufferLength` is used as the element stride for **every** C type, including the fixed-length ones where the specification says it is ignored - so `SQL_C_SLONG` bound with `BufferLength=0` gets a stride of zero and every parameter set reads element 0. The execute still returns `SQL_SUCCESS` with a correct processed count and the right number of rows; only their contents are wrong. The indicator array keeps striding correctly on purpose, which is why the original bug report read as a NULL wandering between rows. This is the only configuration anywhere that can fail `test_column_wise_array_binding`'s key assertion - the Firebird pair cannot, because 3.0.1.21 masks the stride behind an executor defect that runs one set at all. |
| `CrashOn` | Function names | Comma-separated functions that **crash** rather than fail — today only `SQLCopyDesc`. Faults the process the way a real driver does (access violation on Windows, `SIGTRAP` on POSIX, matching what the crash guard catches on each), *and* poisons the connection so every later `SQLAllocHandle(SQL_HANDLE_STMT)` on it returns `08S01`. Both halves matter: the fault is what the guard catches, and the dead connection is what **S7** exists to replace. Models Firebird ODBC 3.0.1.21's `test_copy_desc` crash on Linux, where the connection does not survive — on Windows the same build's connection does, which is why the entire Windows pair never exposed the defect. It returns an error rather than hanging, deliberately: hanging is what the real driver does and a test that reproduced it would hang CI. Measured on this build: with S7, 204 passed / 1 error; with the reconnect removed, 90 passed / 63 errors. |
| `ResultSetSize` | Number | Rows to return |
| `FailOn` | Function names | Comma-separated functions to fail, in any mode including the default `Success`. Naming a function also narrows `Failure`/`Random` to the named ones. **A `…W` name** (D78) fails only calls that entered through that Unicode entry point; an ANSI name fails both widths. Through a driver manager this driver receives both widths as W, so the W names are a lever for these tests rather than something an application can distinguish. A name this driver cannot fail on — a typo, or an entry point with no `should_fail` call — is reported: the connection returns `SQL_SUCCESS_WITH_INFO` with an `01000` record naming it, and the entries spelt right are unaffected (D87). The accepted set is generated into `src/driver/fail_on_names.hpp` by `tools/check_fail_on_names.py`, which CI runs to catch drift. |
| `ErrorCode` | SQLSTATE | Error code to return |
| `Latency` | e.g., 10ms, 500us, 2s | Simulated per-call delay. A bare number is milliseconds; `us` and `s` are honoured rather than silently read as ms. |
| `FetchReturnsWarning` | false (default) / true | Every `SQLFetch` that returns a row also posts SQLSTATE `01004` and returns `SQL_SUCCESS_WITH_INFO` instead of `SQL_SUCCESS`. Catches fetch loops written `SQLFetch(h) == SQL_SUCCESS`, which stop mid-result-set on any warned row. |
| `BufferValidation` | Strict (default) / Lenient | When `Lenient`, string values come back **without** their NUL terminator — the classic careless-driver behaviour, and the lever a null-termination probe needs in order to be able to fail. Since D62 it is applied in the one copy every string return passes through, so it reaches `SQLGetData`, `SQLDescribeCol`, `SQLGetCursorName`, `SQLNativeSql` and `SQLGetDiagRec` as well as `SQLGetInfo`. |
| `SilentCorruption` | None, DropInserts, DropUpdates, MangleVarchar, TruncateNumeric, NullAsEmpty, MangleUnicode, SkewNumeric, SkewNumericBound, StaleGetDataOffset | Silently tamper with stored data while keeping ODBC return codes successful — used to validate that round-trip / verify-rows-persisted tests detect a misbehaving driver. `NullAsEmpty` returns NULL char/wchar cells as empty string with indicator=0 (Oracle-style empty-vs-null conflation). `MangleUnicode` replaces every non-ASCII byte in a fetched char/wchar cell with `?` (codepage-bound driver pattern). `SkewNumeric` returns every numeric cell +1 on both the bound-column and `SQLGetData` paths; `SkewNumericBound` skews only the bound-column path, so an application reading one column both ways sees them disagree. `DropUpdates` makes `UPDATE` report the true number of matched rows and write none of them; the row count stays correct deliberately, since that is what makes the loss silent. `StaleGetDataOffset` stops the `SQLGetData` retrieval offset being reset between result sets — the defect D85 fixed in this driver, kept available so the probe written for it (D86) has a configuration it can fail on. |
| `NativeSqlPassThrough` | true, false | When `true`, `SQLNativeSql` returns its input verbatim without translating any ODBC escape sequence (`{fn ...}`, `{d ...}`, `{oj ...}`, `{CALL ...}`, etc.). Drives the PORT plan port 4 e2e canary; spec-compliant drivers must always translate. |
| `Procedures` | (default) / BrokenInout | When `BrokenInout`, the canonical `CRUSHER_PROC(IN n, OUT m, INOUT s)` procedure runs but its callback returns no output values. Mocks drivers that accept `{?=CALL …}` syntactically but never write back to `SQL_PARAM_OUTPUT` / `SQL_PARAM_INPUT_OUTPUT` bound buffers. Drives the PORT plan port 3 e2e canary. |
| `ArrayBindRowFailsAt` | Number (default 0) | When `> 0`, the Nth row (1-indexed) of any array-parameter execute is forced to fail with SQLSTATE `23000`; surrounding rows execute normally. Drives the PORT plan port 6 per-row status probe. |
| `ArrayErrorBreaksHandle` | false (default) / true | When `true`, an array execute in which any parameter set failed leaves the **statement handle unusable**: the next `SQLExecute` on it returns `HY000` instead of running. Reproduces #309 (Firebird ODBC PR #313), where the driver restored the descriptor's bind-offset pointer on the success path only, so after a mid-array error it pointed into a dead stack frame and the next execute read a garbage offset. The mock returns an error rather than reproducing the access violation, for the same reason `CrashOn` returns rather than hangs. This is the only configuration anywhere that can fail `test_handle_reuse_after_array_error`: the Firebird pair cannot, because P11 crashes 3.0.1.21 earlier in the same category and the probe never runs on the baseline. |
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
