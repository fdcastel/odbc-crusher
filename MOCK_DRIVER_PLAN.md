# Mock ODBC Driver — Improvement Plan

**Version**: 1.0  
**Based on**: Analysis of psqlodbc (PostgreSQL ODBC driver) — 2000+ commits, ~30 years of production use  
**Last Updated**: February 7, 2026

---

## 0. Status (February 7, 2026)

### Completed

- **Phase M1 (Unicode-First Architecture)** — DONE
  - All 34 W-variant entry points implemented in `unicode_wrappers.cpp`
  - Proper UTF-16 ↔ UTF-8 conversion utilities (`string_utils.cpp`) using Win32 `WideCharToMultiByte`/`MultiByteToWideChar` on Windows, portable BMP+surrogate implementation for Linux/macOS
  - Explicit `.def` file matching MySQL ODBC Unicode Driver export pattern (W-only for string functions, plain names for non-string functions)
  - `WINDOWS_EXPORT_ALL_SYMBOLS` removed — explicit `.def` file prevents DM symbol re-export
  - **DM crash fix**: `StatementHandle` constructor now allocates all four implicit descriptor handles (`app_param_desc_`, `imp_param_desc_`, `app_row_desc_`, `imp_row_desc_`); `SQLGetStmtAttr` returns them for `SQL_ATTR_APP_PARAM_DESC`/`SQL_ATTR_IMP_PARAM_DESC`/`SQL_ATTR_APP_ROW_DESC`/`SQL_ATTR_IMP_ROW_DESC`. This was the root cause of the DM access violation at `ODBC32.dll+0x3E48` — the DM queries these descriptors immediately after `SQLAllocHandle(SQL_HANDLE_STMT)`.
- **Phase M2 (Internal API Hardening)** — DONE
- **Phase M3 (Spec Compliance Polish)** — DONE
- **Phase M4 (Thread Safety)** — DONE
- **Phase M5 (Arrays of Parameter Values)** — DONE
- **Phase M6 (Full SQL Support & Catalog Alignment)** — DONE
  - Literal `SELECT` queries without `FROM` clause (integers, floats, strings, NULL, CAST, N'...', X'...', DATE '...', UUID(), parameter markers `?`)
  - `CREATE TABLE` / `DROP TABLE` with runtime catalog modification
  - `INSERT INTO` with data persistence + `SELECT COUNT(*)` aggregate
  - `SQLEndTran(SQL_ROLLBACK)` clears inserted data
  - Scrollable cursors (`SQL_FETCH_FIRST`, `SQL_FETCH_LAST`, `SQL_FETCH_ABSOLUTE`, `SQL_FETCH_RELATIVE`, `SQL_FETCH_PRIOR`)
  - `CUSTOMERS` table added to default catalog with FK from `ORDERS`
  - `SQLDescribeParam` validates parameter number range
  - `SQLForeignKeys` returns all FKs when iterating tables
  - Unicode W-wrapper string length bug fixed (`sqlw_to_string` was dividing character count by `sizeof(SQLWCHAR)`)
  - `SQLGetData` with `cbValueMax=0` returns `SQL_SUCCESS_WITH_INFO` with data length
  - `SQL_C_TYPE_DATE` conversion from string values

### Test Results (Post-M6)

- **Mock driver unit tests**: 48/48 pass (100%)
- **odbc-crusher end-to-end**: **131/131 tests pass (100.0%)** — 0 failures, 0 skips
- **odbc-crusher unit tests**: 60/60 pass (100%)

### Remaining Work

No pending phases. The mock driver is feature-complete for odbc-crusher's current test suite.

---

## 1. Executive Summary

The mock ODBC driver is the cornerstone of odbc-crusher's testing infrastructure. Without a correct, spec-compliant mock driver, the application cannot distinguish its own bugs from driver bugs.

A study of the psqlodbc codebase reveals that the mock driver has **three architectural flaws** that cause crashes on Windows and would cause subtle test failures everywhere:

1. **No Unicode entry points** — The Windows Driver Manager (DM) calls W-variant functions; the mock driver doesn't export them.
2. **WINDOWS_EXPORT_ALL_SYMBOLS re-exports DM symbols** — Creates infinite recursion when the DM calls back into itself.
3. **ANSI-first architecture** — All internal strings are `SQLCHAR*`/`std::string`; Unicode is an afterthought wrapper.

The psqlodbc driver solves these problems with a battle-tested layered architecture that we should adopt.

---

## 2. Findings from psqlodbc

### 2.1 Three-Layer Architecture

psqlodbc separates concerns into three distinct layers:

```
┌─────────────────────────────────────────┐
│  ODBC API Layer (odbcapi.c/odbcapiw.c)  │  ← DM calls these
│  - SQLColumnsW, SQLGetInfoW, etc.       │
│  - Unicode ↔ UTF-8 conversion           │
│  - Thread safety (ENTER_STMT_CS/LEAVE)  │
│  - Error clearing + savepoint mgmt      │
├─────────────────────────────────────────┤
│  Internal API Layer (PGAPI_*)           │  ← All logic lives here
│  - PGAPI_Columns, PGAPI_GetInfo, etc.  │
│  - Works with UTF-8 strings internally  │
│  - Never called by DM directly          │
├─────────────────────────────────────────┤
│  Backend Layer (connection.c, etc.)     │  ← Database communication
│  - PostgreSQL wire protocol             │
│  - (Mock driver: in-memory data)        │
└─────────────────────────────────────────┘
```

**Key insight**: The ODBC API functions (the ones the DM calls) are *thin wrappers*. They handle:
1. Unicode conversion (W → UTF-8 → PGAPI → UTF-8 → W)
2. Thread locking (critical sections)
3. Error state management (clear errors, savepoint)
4. Then delegate all logic to `PGAPI_*` functions

The mock driver currently has only two layers: ODBC API functions that directly contain business logic. This makes adding Unicode support invasive.

### 2.2 Unicode Strategy: Unicode-First with UTF-8 Core

psqlodbc builds two separate DLLs from the same source:
- `psqlodbc35w.dll` (Unicode) — exports W functions, compiled with `UNICODE_SUPPORT`
- `psqlodbc30a.dll` (ANSI) — exports ANSI functions only, no W variants

The Unicode build:
- **Exports W functions** in the `.def` file (e.g., `SQLColumnsW @103`)
- **Also exports ANSI functions** for backward compatibility, but with `;` prefix in `.def` to comment them out for string-accepting functions
- **Internally uses UTF-8** for all string processing
- Converts `SQLWCHAR* → UTF-8` on entry, `UTF-8 → SQLWCHAR*` on exit

**Critical design decision**: In the Unicode build, the `PGAPI_GetInfo` function *detects it's running in a Unicode driver* via `CC_is_in_unicode_driver(conn)` and converts its string output to UTF-16 internally. This means `SQLGetInfoW` doesn't need to do any conversion — it just calls `PGAPI_GetInfo` and the output is already Unicode.

```c
// From info.c — inside PGAPI_GetInfo
if (p)  // string result
{
    if (CC_is_in_unicode_driver(conn))
    {
        len = utf8_to_ucs2(p, len, (SQLWCHAR *) rgbInfoValue, cbInfoValueMax / WCLEN);
        len *= WCLEN;
    }
    else
        strncpy_null((char *) rgbInfoValue, p, cbInfoValueMax);
}
```

**Our approach should be simpler**: Since we're Unicode-only (no ANSI build), we can make our internal API accept `std::string` (UTF-8) and have the W entry points do the conversion. No need for the `CC_is_in_unicode_driver` flag — we always are.

### 2.3 Module Definition (.def) File Strategy

psqlodbc's Unicode `.def` file (`psqlodbc.def`) is instructive:

```def
LIBRARY psqlodbc35w
EXPORTS
;;SQLAllocConnect @1          ; commented out entirely (ODBC 2.x)
SQLBindCol @4                 ; no W variant needed (no string params)
;SQLConnect @7                ; ANSI version commented out (has W variant)
SQLConnectW @104              ; W variant explicitly exported
SQLGetTypeInfoW @128          ; W variant even though no string params!
```

The pattern:
- Functions with **no string parameters** (e.g., `SQLBindCol`, `SQLFetch`): export ANSI name only
- Functions with **string parameters** (e.g., `SQLConnect`, `SQLColumns`): export *only* the W variant; comment out the ANSI version with `;`
- Functions that the **DM needs as W** regardless: `SQLGetTypeInfoW`, `SQLGetStmtAttrW`, `SQLSetStmtAttrW` — exported even though they don't have string parameters
- **`SQLAllocHandle` / `SQLFreeHandle`**: exported without W variants (the DM resolves `SQLAllocHandle` by name)

The mock driver's current `.def` file exports all ANSI functions but only `SQLAllocHandleW` and `SQLFreeHandleW`. This is **backwards** — it should export W variants for all string-accepting functions.

### 2.4 Thread Safety

psqlodbc uses `CRITICAL_SECTION` (Windows) / `pthread_mutex_t` (POSIX) on every handle:

```c
ENTER_STMT_CS(stmt);         // Lock
SC_clear_error(stmt);        // Clear previous errors
StartRollbackState(stmt);    // Transaction savepoint
ret = PGAPI_Columns(...);    // Actual work
ret = DiscardStatementSvp(stmt, ret, FALSE);  // Commit/rollback savepoint
LEAVE_STMT_CS(stmt);         // Unlock
```

The mock driver currently has **no thread safety**. While the mock driver won't face concurrent access in typical test scenarios, thread safety is required by the ODBC spec and its absence means:
- The mock driver can't be used for multi-threaded testing
- It misrepresents real-world driver behavior

### 2.5 Handle Validation

psqlodbc casts handles directly to internal types:

```c
StatementClass *stmt = (StatementClass *) StatementHandle;
```

No magic numbers, no dynamic_cast. It relies on the DM to provide valid handles.

The mock driver uses a `HANDLE_MAGIC` (0x4D4F434B) validation — this is a reasonable approach for a mock/test driver and should be kept.

### 2.6 Error / Diagnostic Architecture

psqlodbc stores diagnostics per-handle with `PG_ErrorInfo`:
- Each handle (env, conn, stmt, desc) has its own error fields
- Errors are cleared at the start of each API call: `SC_clear_error(stmt)`
- `SQLGetDiagRec` / `SQLGetDiagField` read from the handle's error store
- Multiple diagnostic records are supported via linked list

The mock driver has a similar `DiagnosticRecord` vector per handle — this is correct. The W variants for `SQLGetDiagRecW` / `SQLGetDiagFieldW` need to convert strings (SQLSTATE, message text) to UTF-16.

### 2.7 Catalog Function Pattern

Every catalog W function in psqlodbc follows the same pattern:

```c
RETCODE SQL_API SQLColumnsW(HSTMT hstmt,
    SQLWCHAR *CatalogName, SQLSMALLINT NameLength1, ...)
{
    // 1. Convert W → UTF-8
    ctName = ucs2_to_utf8(CatalogName, NameLength1, &nmlen1, lower_id);
    
    // 2. Lock
    ENTER_STMT_CS(stmt);
    SC_clear_error(stmt);
    
    // 3. Delegate to internal API
    ret = PGAPI_Columns(hstmt, (SQLCHAR*)ctName, nmlen1, ...);
    
    // 4. Unlock
    LEAVE_STMT_CS(stmt);
    
    // 5. Free converted strings
    free(ctName);
    return ret;
}
```

The mock driver should follow this exact pattern.

---

## 3. Current Mock Driver Problems

### P1: WINDOWS_EXPORT_ALL_SYMBOLS Re-Export Bug (CRITICAL)

**What happens**: `WINDOWS_EXPORT_ALL_SYMBOLS ON` exports everything — including W-variant function symbols imported from `odbc32.lib`. When the DM calls `SQLGetTypeInfoW` on the driver, it resolves to the DM's own `SQLGetTypeInfoW` → infinite recursion → stack overflow → crash.

**psqlodbc solution**: Explicit `.def` file listing only implemented functions.

**Status**: Fixed with explicit `.def` file. `WINDOWS_EXPORT_ALL_SYMBOLS` removed from CMakeLists.txt.

### P2: No W-Variant Functions (CRITICAL)

**What happens**: The DM on Windows detects a Unicode driver by looking for `SQLConnectW`. If found, it calls W variants for all functions. If not found, it falls back to ANSI, but still calls `SQLAllocHandleW`, `SQLGetTypeInfoW`, `SQLGetStmtAttrW`, `SQLSetStmtAttrW` as W variants regardless.

**Current state**: All W-variant entry points implemented in `unicode_wrappers.cpp`. `.def` file exports W variants for string functions and plain names for non-string functions, following the MySQL ODBC Unicode Driver pattern.

**psqlodbc solution**: Dedicated `odbcapiw.c` and `odbcapi30w.c` files with all 34 W-variant functions. Our approach uses a single `unicode_wrappers.cpp` file.

### P3: ANSI-First Internal Design (MEDIUM)

**What happens**: The mock driver was designed with `SQLCHAR*` as the primary string type. All internal functions use `SQLCHAR*` parameters. When UNICODE is undefined (as in `common.hpp`), ODBC header macros like `SQLDriverConnect` resolve to the ANSI version. This is correct for the internal implementation but means the driver can't cleanly support a Unicode-first API.

**psqlodbc approach**: Internal functions (`PGAPI_*`) always accept `SQLCHAR*` (UTF-8). The W wrapper layer converts `SQLWCHAR*` → `SQLCHAR*` on input and `SQLCHAR*` → `SQLWCHAR*` on output. This is the cleanest approach and what we should adopt.

### P4: No `DllMain` (LOW)

**What happens**: psqlodbc has a `DllMain` in `loadlib.c` for initialization (critical sections, delay-load handling). The mock driver has none. This is fine for simple testing but means:
- No initialization of thread-safety primitives
- No cleanup on DLL unload

### P5: Missing `SQLError` / Legacy Functions (LOW)

**What happens**: ODBC 2.x applications call `SQLError`, `SQLGetConnectOption`, `SQLSetConnectOption`, `SQLTransact`, `SQLColAttributes`. The mock driver doesn't implement these. While ODBC 3.x DM maps them to ODBC 3.x functions, some older DMs or applications may call them directly.

---

## 4. Improvement Plan

### Phase M1: Unicode-First Architecture (CRITICAL)

**Goal**: The mock driver works correctly with the Windows ODBC Driver Manager.

#### M1.1 Adopt Three-Layer Architecture

Restructure the mock driver source files:

```
src/
  driver/           ← Handle management, validation, common types
    common.hpp       ← (keep) Includes, constants, forward declarations
    handles.hpp/cpp  ← (keep) Handle classes with magic validation
    diagnostics.hpp/cpp ← (keep) Diagnostic record storage
    config.hpp/cpp   ← (keep) Connection string parsing
    
  api/               ← NEW: ODBC API entry points (what the DM calls)
    odbcapi.cpp      ← ANSI ODBC 1.0/2.0 entry points  
    odbcapi_30.cpp   ← ANSI ODBC 3.0 entry points (SQLAllocHandle, etc.)
    odbcapiw.cpp     ← Unicode (W) entry points for ODBC 1.0/2.0
    odbcapi_30w.cpp  ← Unicode (W) entry points for ODBC 3.0
    
  impl/              ← NEW: Internal implementation (all logic lives here)
    impl_connection.cpp   ← Internal connect/disconnect logic
    impl_statement.cpp    ← Internal execute/fetch/bind logic
    impl_info.cpp         ← Internal SQLGetInfo/SQLGetTypeInfo/SQLGetFunctions
    impl_catalog.cpp      ← Internal catalog functions
    impl_descriptor.cpp   ← Internal descriptor operations
    impl_diagnostic.cpp   ← Internal diagnostic retrieval
    impl_transaction.cpp  ← Internal transaction management
    
  mock/              ← (keep) Mock data generation
    mock_catalog.cpp
    mock_types.cpp
    mock_data.cpp
    behaviors.cpp
    
  utils/             ← (keep) Utility functions
    string_utils.cpp ← Unicode conversion: ucs2_to_utf8, utf8_to_ucs2
```

#### M1.2 Unicode Conversion Utilities

Replace the current naive `sql_to_string(SQLWCHAR*, ...)` with proper UTF-16 ↔ UTF-8 conversion:

```cpp
// Input: SQLWCHAR* (UTF-16LE on Windows) → std::string (UTF-8)
std::string ucs2_to_utf8(const SQLWCHAR* wstr, SQLSMALLINT length);

// Output: std::string (UTF-8) → SQLWCHAR* buffer (UTF-16LE)
SQLRETURN utf8_to_ucs2(const std::string& src, SQLWCHAR* target, 
                        SQLSMALLINT buffer_length, SQLSMALLINT* string_length);
```

The current `sql_to_string(SQLWCHAR*, ...)` only handles ASCII (drops high bytes). This must be replaced with real UTF-16 ↔ UTF-8 conversion using:
- `WideCharToMultiByte` / `MultiByteToWideChar` on Windows
- `<codecvt>` (deprecated but functional) or ICU on Linux/macOS
- Manual UTF-16 → UTF-8 conversion (as psqlodbc does in `win_unicode.c`) for portability

#### M1.3 Module Definition File

Replace the current `.def` file with a psqlodbc-style export list:

```def
LIBRARY mockodbc
EXPORTS
    ; === Functions with NO string parameters (export ANSI name only) ===
    SQLAllocHandle
    SQLFreeHandle
    SQLBindCol
    SQLBindParameter
    SQLCancel
    SQLCloseCursor
    SQLCopyDesc
    SQLDescribeParam
    SQLEndTran
    SQLExecute
    SQLFetch
    SQLFetchScroll
    SQLFreeStmt
    SQLGetData
    SQLGetFunctions
    SQLMoreResults
    SQLNumParams
    SQLNumResultCols
    SQLParamData
    SQLPutData
    SQLRowCount
    SQLSetPos
    SQLBulkOperations
    
    ; === Functions WITH string parameters (export W variant) ===
    SQLBrowseConnectW
    SQLColAttributeW
    SQLColumnPrivilegesW
    SQLColumnsW
    SQLConnectW
    SQLDescribeColW
    SQLDriverConnectW
    SQLExecDirectW
    SQLForeignKeysW
    SQLGetConnectAttrW
    SQLGetCursorNameW
    SQLGetDescFieldW
    SQLGetDescRecW
    SQLGetDiagFieldW
    SQLGetDiagRecW
    SQLGetInfoW
    SQLGetStmtAttrW
    SQLNativeSqlW
    SQLPrepareW
    SQLPrimaryKeysW
    SQLProcedureColumnsW
    SQLProceduresW
    SQLSetConnectAttrW
    SQLSetCursorNameW
    SQLSetDescFieldW
    SQLSetDescRecW
    SQLSetEnvAttrW
    SQLGetEnvAttrW
    SQLSetStmtAttrW
    SQLSpecialColumnsW
    SQLStatisticsW
    SQLTablePrivilegesW
    SQLTablesW
    SQLGetTypeInfoW
    SQLErrorW
    
    ; === Legacy (ODBC 2.x) ===
    SQLAllocConnect
    SQLAllocEnv
    SQLAllocStmt
    SQLFreeConnect
    SQLFreeEnv
```

#### M1.4 W-Variant Entry Points

Create `odbcapiw.cpp` and `odbcapi_30w.cpp` following the psqlodbc pattern. Each W function:

1. Converts `SQLWCHAR*` inputs to `std::string` (UTF-8)
2. Calls the internal `IMPL_*` function with `std::string` parameters
3. Converts `std::string` outputs to `SQLWCHAR*` buffers
4. Returns

Example:

```cpp
// In odbcapiw.cpp
RETCODE SQL_API SQLColumnsW(
    HSTMT hstmt,
    SQLWCHAR *CatalogName, SQLSMALLINT NameLength1,
    SQLWCHAR *SchemaName, SQLSMALLINT NameLength2,
    SQLWCHAR *TableName, SQLSMALLINT NameLength3,
    SQLWCHAR *ColumnName, SQLSMALLINT NameLength4)
{
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    auto cat = ucs2_to_utf8(CatalogName, NameLength1);
    auto sch = ucs2_to_utf8(SchemaName, NameLength2);
    auto tab = ucs2_to_utf8(TableName, NameLength3);
    auto col = ucs2_to_utf8(ColumnName, NameLength4);
    
    stmt->clear_diagnostics();
    return IMPL_Columns(stmt, cat, sch, tab, col);
}
```

#### M1.5 Deliverables

- [x] ~~Restructure source into `api/` + `impl/` layers~~ (Kept simpler: W wrappers in `unicode_wrappers.cpp` delegate to ANSI implementations)
- [x] Implement proper UTF-16 ↔ UTF-8 conversion utilities  
- [x] Create all 34 W-variant entry points
- [x] Create `.def` file with psqlodbc-style exports
- [x] Remove `WINDOWS_EXPORT_ALL_SYMBOLS` from CMakeLists.txt
- [x] Test: odbc-crusher connects and completes all phases against the mock driver on Windows
- [x] Test: Mock driver unit tests still pass
- [x] Fix: DM crash — allocate implicit descriptor handles in `StatementHandle` constructor; return them from `SQLGetStmtAttr`

---

### Phase M2: Internal API Hardening (HIGH)

**Goal**: The internal implementation correctly handles all ODBC semantics.

#### M2.1 SQLGetInfo Unicode-Aware Output

Following psqlodbc's pattern, `IMPL_GetInfo` should know whether it's returning a string and write the output correctly:

- **String info types**: Write `SQLWCHAR*` to the output buffer (since we're Unicode-only)
- **Numeric info types**: Write the raw integer/bitmask value
- **Buffer length**: For strings, `pcbInfoValue` returns length in **bytes** (not characters) including the null terminator

The current mock driver writes all strings as `SQLCHAR*` and the W wrapper tries to convert. This is fragile. Following psqlodbc, the internal function should produce the final output format.

#### M2.2 Catalog Result Set Column Types

Catalog functions (`SQLTables`, `SQLColumns`, etc.) return result sets. The ODBC spec defines exact column types for each:

- `SQLTables`: 5 VARCHAR columns (TABLE_CAT, TABLE_SCHEM, TABLE_NAME, TABLE_TYPE, REMARKS)
- `SQLColumns`: 18 columns with specific types (COLUMN_NAME VARCHAR, DATA_TYPE SMALLINT, etc.)

For a Unicode driver, string columns in catalog result sets should be `SQL_WVARCHAR` / `SQL_WCHAR`, not `SQL_VARCHAR` / `SQL_CHAR`. The mock driver currently returns `SQL_VARCHAR` for all string columns.

#### M2.3 SQLGetData Unicode Output

When `SQLGetData` is called with `SQL_C_WCHAR` target type, the driver must:
1. Convert the internal UTF-8 string to UTF-16
2. Write `SQLWCHAR` data to the buffer
3. Return `pcbValue` in **bytes** (not characters)
4. Handle truncation with `SQL_SUCCESS_WITH_INFO` + SQLSTATE `01004`

Current behavior: `SQLGetData` only handles `SQL_C_CHAR` for strings. `SQL_C_WCHAR` is not supported.

#### M2.4 SQLDescribeCol Unicode Column Names

`SQLDescribeColW` must return column names as `SQLWCHAR*`. The current implementation returns `SQLCHAR*` names.

#### M2.5 Deliverables

- [x] `SQLGetInfoW` produces Unicode output for string info types (explicit string-type list, not heuristic)
- [x] Catalog result sets use `SQL_WVARCHAR` for string columns (all 10 catalog functions updated)
- [x] `SQLGetData` supports `SQL_C_WCHAR` target type (strings, integers, doubles → UTF-16)
- [x] `SQLDescribeColW` returns Unicode column names (via W wrapper + `SQL_WVARCHAR` column types)
- [x] `SQLColAttribute` handles `SQL_WVARCHAR` in length/display-size switches
- [x] All buffer length calculations are in bytes (not characters)

---

### Phase M3: Spec Compliance Polish (MEDIUM)

**Goal**: The mock driver's behavior matches what real drivers do, so odbc-crusher test results are meaningful.

#### M3.1 SQLGetFunctions Accuracy

`SQLGetFunctions` with `SQL_API_ODBC3_ALL_FUNCTIONS` must return a bitmask of exactly the functions the driver implements. After Phase M1, update the bitmask to include the W-variant functions and remove any functions that aren't actually implemented.

#### M3.2 Connection Attribute Completeness

Real drivers report many attributes that the mock driver doesn't. Add support for:

| Attribute | Value |
|-----------|-------|
| `SQL_ATTR_DRIVER_THREADING` | `SQL_OV_ODBC3` |
| `SQL_ATTR_CONNECTION_DEAD` | `SQL_CD_FALSE` (when connected) |
| `SQL_ATTR_CURRENT_CATALOG` | The mock catalog name |

#### M3.3 SQLSTATE Completeness

psqlodbc has extensive SQLSTATE mapping. Ensure the mock driver returns correct SQLSTATEs for:

| Situation | Required SQLSTATE |
|-----------|------------------|
| Connection not open | 08003 |
| Invalid string length | HY090 |
| Driver does not support this function | IM001 |
| Optional feature not implemented | HYC00 |
| Buffer too small (string truncation) | 01004 |
| Invalid handle | HY000 |
| Numeric value out of range | HY019 or 22003 |

#### M3.4 DllMain for Initialization

Add a `DllMain` / constructor to:
- Initialize any global state
- Log driver load/unload for debugging
- Set up critical sections if thread safety is added

```cpp
#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
#endif
```

#### M3.5 Deliverables

- [x] `SQLGetFunctions` bitmask matches actual exports (added `SQLBrowseConnect`, `SQLBulkOperations`, `SQLSetPos`)
- [x] Connection attributes expanded (`SQL_ATTR_CONNECTION_DEAD` added)
- [x] All SQLSTATEs reviewed against ODBC spec (`IM001` added; `HY092` verified for invalid attrs)
- [x] `DllMain` updated with `DisableThreadLibraryCalls` for Windows

---

### Phase M4: Thread Safety (LOW)

**Goal**: The mock driver can be used for multi-threaded testing scenarios.

#### M4.1 Per-Handle Locking

Following psqlodbc's pattern, add a mutex to each handle class:

```cpp
class StatementHandle : public OdbcHandle {
    std::mutex cs_;  // Critical section for this handle
    // ...
};

// RAII lock guard
class StmtLock {
public:
    StmtLock(StatementHandle* stmt) : stmt_(stmt) { stmt_->cs_.lock(); }
    ~StmtLock() { stmt_->cs_.unlock(); }
private:
    StatementHandle* stmt_;
};
```

Every API entry point wraps its work in a lock:

```cpp
RETCODE SQL_API SQLColumnsW(...) {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    StmtLock lock(stmt);
    stmt->clear_diagnostics();
    return IMPL_Columns(stmt, ...);
}
```

#### M4.2 Deliverables

- [x] Mutex added to all four handle types (via `OdbcHandle` base class)
- [x] Key API entry points acquire `HandleLock` before work (statement, catalog, connection, info APIs)
- [x] RAII `HandleLock` guard prevents deadlocks

---

### Phase M5: Arrays of Parameter Values

**Goal**: Support executing a prepared statement with arrays of parameter values, enabling batch INSERT/UPDATE/DELETE operations per ODBC spec.

**ODBC Spec References**:
- [Arrays of Parameter Values](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/arrays-of-parameter-values)
- [Binding Arrays of Parameters](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/binding-arrays-of-parameters)
- [Using Arrays of Parameters](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/using-arrays-of-parameters)

#### M5.1 Statement Handle Extensions

Add new members to `StatementHandle` for array parameter support:

| Member | Type | Default | Purpose |
|--------|------|---------|---------|
| `param_status_ptr_` | `SQLUSMALLINT*` | `nullptr` | Application-allocated array for per-row status |
| `params_processed_ptr_` | `SQLULEN*` | `nullptr` | Application-allocated variable for processed count |
| `param_bind_type_` | `SQLULEN` | `SQL_PARAM_BIND_BY_COLUMN` (0) | Column-wise (0) or row-wise (sizeof struct) |
| `param_bind_offset_ptr_` | `SQLULEN*` | `nullptr` | Optional offset added to all bound addresses |
| `param_operation_ptr_` | `SQLUSMALLINT*` | `nullptr` | Array marking rows as `SQL_PARAM_PROCEED` or `SQL_PARAM_IGNORE` |

#### M5.2 SQLSetStmtAttr / SQLGetStmtAttr

Handle the following attributes:
- `SQL_ATTR_PARAM_STATUS_PTR` → store/retrieve `param_status_ptr_`
- `SQL_ATTR_PARAMS_PROCESSED_PTR` → store/retrieve `params_processed_ptr_`
- `SQL_ATTR_PARAM_BIND_TYPE` → store/retrieve `param_bind_type_`
- `SQL_ATTR_PARAM_BIND_OFFSET_PTR` → store/retrieve `param_bind_offset_ptr_`
- `SQL_ATTR_PARAM_OPERATION_PTR` → store/retrieve `param_operation_ptr_`

#### M5.3 SQLExecute Array Execution Loop

When `paramset_size_ > 1`, `SQLExecute` must:

1. Loop from `i = 0` to `paramset_size_ - 1`
2. For each parameter set `i`:
   a. Check `param_operation_ptr_[i]` — if `SQL_PARAM_IGNORE`, set status to `SQL_PARAM_UNUSED` and skip
   b. Calculate data address for each bound parameter:
      - **Column-wise** (`param_bind_type_ == 0`): `address = base + i * element_size`
      - **Row-wise** (`param_bind_type_ > 0`): `address = base + i * param_bind_type_`
   c. If `param_bind_offset_ptr_` is set, add `*param_bind_offset_ptr_` to all addresses
   d. Execute the statement with the current parameter values
   e. Set `param_status_ptr_[i]` to `SQL_PARAM_SUCCESS` or `SQL_PARAM_ERROR`
3. Set `*params_processed_ptr_` to the number of sets processed
4. Return `SQL_SUCCESS` if all succeeded, `SQL_SUCCESS_WITH_INFO` if some failed, `SQL_ERROR` if all failed

#### M5.4 SQLGetInfo Updates

Report array parameter capabilities:
- `SQL_PARAM_ARRAY_ROW_COUNTS` → `SQL_PARC_BATCH` (individual row counts available)
- `SQL_PARAM_ARRAY_SELECTS` → `SQL_PAS_NO_SELECT` (SELECT with arrays not supported)

#### M5.5 Deliverables

- [x] `StatementHandle` has all 5 new array parameter members
- [x] `SQLSetStmtAttr` / `SQLGetStmtAttr` handle all 5 new attributes
- [x] `SQLExecute` loops over parameter sets with column-wise addressing
- [x] `SQLExecute` loops over parameter sets with row-wise addressing
- [x] Parameter status array populated correctly
- [x] Params processed count set correctly
- [x] `SQL_PARAM_IGNORE` skips rows, marks them `SQL_PARAM_UNUSED`
- [x] Partial failure returns `SQL_SUCCESS_WITH_INFO` with mixed status
- [x] `SQLGetInfo` reports `SQL_PARC_BATCH` and `SQL_PAS_NO_SELECT`
- [x] Mock driver unit tests cover array parameter execution

---

### Phase M6: Full SQL Support & Catalog Alignment (HIGH)

**Goal**: The mock driver with `Mode=Success` should be a reference implementation — every odbc-crusher test should pass. The mock driver must handle all SQL patterns the test suite emits, support scrollable cursors, and have a catalog that matches what the tests expect.

**Root Cause Analysis**: 54 of 131 tests skip or fail because:

| Problem | Impact | Tests Affected |
|---------|--------|----------------|
| `SELECT literal` (no FROM) fails with "SELECT without FROM clause" | All statement/datatype/edge-case tests | ~35 tests |
| `CUSTOMERS` table missing from catalog | Unicode, cursor, param binding, diagnostic, catalog depth tests reference it | ~14 tests |
| `CREATE TABLE`/`DROP TABLE` not supported | Transaction commit/rollback tests | 2 tests |
| Scrollable cursors not implemented | `SQLFetchScroll(FIRST/LAST/ABSOLUTE)` tests | 3 tests |
| `SQLDescribeParam` doesn't validate parameter number | Reported as unsupported | 1 test |
| `SQLForeignKeys` has no data for `CUSTOMERS` table | Reported as unsupported | 1 test |

#### M6.1 SELECT Literal Queries (Critical)

The SQL parser must handle `SELECT` without a `FROM` clause. These patterns are the most common in the test suite:

| Pattern | Expected Behavior |
|---------|-------------------|
| `SELECT 1` | 1 row, 1 column (INTEGER), value = 1 |
| `SELECT 42` | 1 row, 1 column (INTEGER), value = 42 |
| `SELECT -2147483648` | 1 row, 1 column (INTEGER), value = INT_MIN |
| `SELECT 3.14` | 1 row, 1 column (DOUBLE), value = 3.14 |
| `SELECT 'hello'` | 1 row, 1 column (VARCHAR), value = "hello" |
| `SELECT ''` | 1 row, 1 column (VARCHAR), value = "" |
| `SELECT NULL` | 1 row, 1 column (VARCHAR), value = NULL |
| `SELECT 1, 'text'` | 1 row, 2 columns |
| `SELECT CAST(42 AS INTEGER)` | 1 row, 1 column (INTEGER), value = 42 |
| `SELECT CAST(NULL AS INTEGER)` | 1 row, 1 column (INTEGER), value = NULL |
| `SELECT CAST('...' AS VARCHAR(n))` | 1 row, 1 column (VARCHAR), value = literal |
| `SELECT CAST('...' AS DATE)` | 1 row, 1 column (DATE), value = literal |
| `SELECT CAST(x AS DOUBLE PRECISION)` | 1 row, 1 column (DOUBLE), value = x |
| `SELECT CAST(0x... AS VARBINARY(n))` | 1 row, 1 column (VARBINARY), value = hex |
| `SELECT ?` | 1 row, 1 column, type from bound param |
| `SELECT DATE '2026-02-05'` | 1 row, 1 column (DATE), value = literal |
| `SELECT N'...'` | 1 row, 1 column (WVARCHAR), value = literal |
| `SELECT X'48656C6C6F'` | 1 row, 1 column (VARBINARY), value = hex |
| `SELECT UUID()` / `SELECT GEN_UUID()` | 1 row, 1 column (GUID/CHAR), value = generated UUID |

Implementation:
- [x] In `parse_sql()`: When `SELECT` is detected and no `FROM` is found, parse the expression list and set `is_literal_select = true`
- [x] Add literal expression parser that handles: integers, floats, quoted strings, `NULL`, `CAST(expr AS type)`, parameter markers `?`, `N'...'`, `X'...'`, `DATE '...'`, `UUID()`/`GEN_UUID()`
- [x] In `execute_query()`: For literal SELECTs, generate a single row with inferred column types and the literal values
- [x] Column names for literals: `EXPR_1`, `EXPR_2`, etc. (or alias if `AS name` present)

#### M6.2 Catalog Alignment (Critical)

The test suite references `CUSTOMERS` extensively but the mock catalog only has `USERS`, `ORDERS`, `PRODUCTS`, `ORDER_ITEMS`. The tests also reference columns like `NAME`, `CUSTOMER_ID`, `EMAIL`.

Add a `CUSTOMERS` table to the default catalog:

```
CUSTOMERS (
    CUSTOMER_ID  INTEGER     PK, auto-increment
    NAME         VARCHAR(100) NOT NULL
    EMAIL        VARCHAR(100) NULLABLE
    CREATED_DATE DATE         NULLABLE
    IS_ACTIVE    BIT          NULLABLE, default 1
    BALANCE      DECIMAL(10,2) NULLABLE, default 0.00
)
```

Also add foreign key from `ORDERS.USER_ID` → `CUSTOMERS.CUSTOMER_ID` (renaming the FK target) so `SQLForeignKeys` returns data for `CUSTOMERS`.

- [x] Add `CUSTOMERS` table to `create_default_catalog()`
- [x] Add FK relationship from `ORDERS` to `CUSTOMERS`
- [x] Add indexes for `CUSTOMERS` (PK, unique index on EMAIL)

#### M6.3 CREATE TABLE / DROP TABLE Support

Transaction tests need `CREATE TABLE ODBC_TEST_TXN (ID INTEGER, VALUE VARCHAR(50))` and `DROP TABLE ODBC_TEST_TXN`.

Make the catalog mutable:

- [x] Add `add_table()` method to `MockCatalog` — creates a new table with given columns
- [x] Add `remove_table()` method to `MockCatalog` — removes a table by name
- [x] In `parse_sql()`: Recognize `CREATE TABLE name (col_defs)` and `DROP TABLE name`
- [x] Add `QueryType::CreateTable` and `QueryType::DropTable` to `ParsedQuery`
- [x] In `execute_query()`: Call `MockCatalog::add_table()` / `remove_table()` for DDL
- [x] Parse column definitions: `name TYPE` pairs, support `INTEGER`, `INT`, `VARCHAR(n)`, `DECIMAL(p,s)`, `DATE`, `TIMESTAMP`, `BIT`

#### M6.4 Scrollable Cursors

The mock driver stores result data in `std::vector<MockRow>` which supports random access. Only the fetch logic needs updating.

- [x] `SQLFetchScroll(SQL_FETCH_NEXT)`: Already works (delegates to `SQLFetch`)
- [x] `SQLFetchScroll(SQL_FETCH_FIRST)`: Set `current_row_ = 0`, return first row
- [x] `SQLFetchScroll(SQL_FETCH_LAST)`: Set `current_row_ = result_data_.size() - 1`, return last row
- [x] `SQLFetchScroll(SQL_FETCH_ABSOLUTE, n)`: Set `current_row_ = n - 1` (1-based), return row at that position; handle out-of-range with `SQL_NO_DATA`
- [x] `SQLFetchScroll(SQL_FETCH_RELATIVE, n)`: Set `current_row_ += n`, return row; handle bounds
- [x] Honor `cursor_type_` attribute: if `SQL_CURSOR_FORWARD_ONLY`, reject non-NEXT scroll types with HY106; if `SQL_CURSOR_STATIC` or higher, allow all scroll types
- [x] Transfer data to bound columns (same logic as `SQLFetch`)

#### M6.5 SQLDescribeParam Accuracy

The current implementation returns hardcoded values for all parameters without validating the parameter number. Fix:

- [x] Count `?` markers in the prepared SQL string
- [x] Validate `ParameterNumber` is in range `[1, num_params]`; return HY000 if out of range
- [x] Return reasonable type info based on context (SQL_VARCHAR/255/SQL_NULLABLE is acceptable for a mock driver)

#### M6.6 INSERT Data Persistence (for Transaction Tests)

Transaction commit/rollback tests need:
1. `INSERT INTO ODBC_TEST_TXN VALUES (1, 'test')` — actually insert data
2. `SELECT COUNT(*) FROM ODBC_TEST_TXN` — return the count of inserted rows
3. `SQLEndTran(SQL_ROLLBACK)` — revert inserts

- [x] Add mutable data storage to `MockCatalog`: `std::unordered_map<string, vector<MockRow>> inserted_data_`
- [x] `INSERT` statements parse `VALUES (...)` and store the row
- [x] `SELECT COUNT(*)` recognized as an aggregate — returns count from `inserted_data_` + `result_set_size` for static data
- [x] `SQLEndTran(SQL_COMMIT)`: No-op (data already stored)
- [x] `SQLEndTran(SQL_ROLLBACK)`: Clear `inserted_data_` for all tables modified in the current transaction
- [x] Track pending transaction data separately so rollback can discard it

#### M6.7 Mock Driver Unit Tests

- [x] Test `SELECT 1`, `SELECT 'hello'`, `SELECT 3.14`, `SELECT NULL` parse and execute correctly
- [x] Test `SELECT CAST(42 AS INTEGER)` returns INTEGER type
- [x] Test `SELECT CAST(NULL AS VARCHAR(50))` returns NULL with SQL_NULL_DATA
- [x] Test `CREATE TABLE` / `DROP TABLE` add and remove tables from catalog
- [x] Test `INSERT INTO` + `SELECT COUNT(*)` returns correct count
- [x] Test `SQLEndTran(SQL_ROLLBACK)` clears inserted data
- [x] Test `SQLFetchScroll` with all fetch orientations
- [x] Test `SQLDescribeParam` validates parameter number range
- [x] Test `CUSTOMERS` table exists in default catalog with expected columns

#### M6.8 Deliverables

- [x] `SELECT literal` queries work (integers, floats, strings, NULL, CAST, parameters, N'...', X'...', UUID)
- [x] `CUSTOMERS` table in default catalog with FK relationship from ORDERS
- [x] `CREATE TABLE` / `DROP TABLE` modify the catalog at runtime
- [x] `INSERT INTO` persists data; `SELECT COUNT(*)` returns actual count
- [x] `SQLEndTran(SQL_ROLLBACK)` reverts inserted data
- [x] Scrollable cursors work: FIRST, LAST, ABSOLUTE, RELATIVE
- [x] `SQLDescribeParam` validates parameter number
- [x] All 131 odbc-crusher tests pass (0 failures, 0 skips) ✅
- [x] Mock driver unit tests cover all new functionality (48/48 pass)

---

## 5. Testing Strategy

### 5.1 Local Build Verification

After each phase:
```powershell
# Build mock driver
cmake --build mock-driver/build --config Release

# Run mock driver unit tests
ctest --test-dir mock-driver/build -C Release

# Run odbc-crusher against mock driver
./build/src/Release/odbc-crusher.exe "Driver={Mock ODBC Driver};Mode=Success;" -v

# Verify full completion (no crash, exit code 0 or 1)
echo $LASTEXITCODE
```

### 5.2 CI Verification

The stress-test workflow downloads the mock driver from the release and runs odbc-crusher against it. After Phase M1, the mock driver job should produce a non-empty report with all 9 test categories.

### 5.3 Cross-Platform

- **Windows**: Primary target. DM calls W functions. This is where the Unicode architecture matters most.
- **Linux (unixODBC)**: unixODBC also supports W functions when the driver exports `SQLConnectW`. The same `.def`-style approach works via symbol visibility.
- **macOS (iODBC)**: Similar to unixODBC. The mock driver should work but is lower priority.

---

## 6. Priority & Dependencies

```
Phase M1 (Unicode-First)     ← MUST DO FIRST — driver crashes without this
    ↓
Phase M2 (Internal Hardening) ← Correctness of test results depends on this
    ↓
Phase M3 (Spec Compliance)    ← Polish for realistic behavior
    ↓
Phase M4 (Thread Safety)      ← Nice to have for advanced testing
```

**Phase M1 is the critical path.** Without it, the mock driver crashes on Windows and produces no test output in CI. Phases M2–M4 improve quality but don't block basic functionality.

---

## 7. Key Lessons from psqlodbc

1. **The DM determines Unicode capability by probing for `SQLConnectW`.** If the driver exports `SQLConnectW`, the DM calls W variants for everything. If not, it falls back to ANSI. There is no middle ground — you're either a full Unicode driver or a full ANSI driver.

2. **Never link a driver against `odbc32.lib` with auto-export.** The driver's DLL will re-export the DM's own functions, causing infinite recursion when the DM calls them. Use an explicit `.def` file.

3. **String-accepting functions need W variants; non-string functions don't.** But some non-string functions (`SQLGetTypeInfoW`, `SQLGetStmtAttrW`) need W variants anyway because the DM resolves them by W name.

4. **The W function layer is thin.** It does three things: convert input strings to UTF-8, call the internal function, convert output strings to UTF-16. Nothing else. Keep it thin.

5. **`SQLGetInfo` is special.** For a Unicode driver, string info values must be returned as `SQLWCHAR*` even when the ANSI `SQLGetInfo` function is called (because the DM might call either). psqlodbc handles this by checking `CC_is_in_unicode_driver(conn)` inside `PGAPI_GetInfo`. Our simplification: since we're Unicode-only, always return `SQLWCHAR*` for string info types when called via W entry point.

6. **Buffer lengths are in bytes, not characters.** When returning string data in W functions, `pcbValue` / `pcbInfoValue` must report the length in **bytes** (`char_count * sizeof(SQLWCHAR)`), not character count. Getting this wrong causes buffer overflows in applications.

7. **Catalog result sets don't need W conversion.** The data inside result set rows is returned via `SQLGetData` / bound columns, which handle conversion based on the target C type (`SQL_C_CHAR` vs `SQL_C_WCHAR`). The catalog W functions only need to convert their *input* parameters (catalog/schema/table names).

8. **The `.def` file is the driver's contract with the DM.** What you export is what you claim to support. Export too little → DM can't call you. Export too much → infinite recursion or incorrect behavior. The `.def` file must exactly match the driver's implementation.
