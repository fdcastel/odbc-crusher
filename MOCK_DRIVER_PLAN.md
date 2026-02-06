# Mock ODBC Driver — Improvement Plan

**Version**: 1.0  
**Based on**: Analysis of psqlodbc (PostgreSQL ODBC driver) — 2000+ commits, ~30 years of production use  
**Last Updated**: February 6, 2026

---

## 0. Status (February 6, 2026)

### Completed

- **Phase M1 (Unicode-First Architecture)** — DONE
  - All 34 W-variant entry points implemented in `unicode_wrappers.cpp`
  - Proper UTF-16 ↔ UTF-8 conversion utilities (`string_utils.cpp`) using Win32 `WideCharToMultiByte`/`MultiByteToWideChar` on Windows, portable BMP+surrogate implementation for Linux/macOS
  - Explicit `.def` file matching MySQL ODBC Unicode Driver export pattern (W-only for string functions, plain names for non-string functions)
  - `WINDOWS_EXPORT_ALL_SYMBOLS` removed — explicit `.def` file prevents DM symbol re-export
  - **DM crash fix**: `StatementHandle` constructor now allocates all four implicit descriptor handles (`app_param_desc_`, `imp_param_desc_`, `app_row_desc_`, `imp_row_desc_`); `SQLGetStmtAttr` returns them for `SQL_ATTR_APP_PARAM_DESC`/`SQL_ATTR_IMP_PARAM_DESC`/`SQL_ATTR_APP_ROW_DESC`/`SQL_ATTR_IMP_ROW_DESC`. This was the root cause of the DM access violation at `ODBC32.dll+0x3E48` — the DM queries these descriptors immediately after `SQLAllocHandle(SQL_HANDLE_STMT)`.

### Test Results

- **Mock driver unit tests**: 48/48 pass (100%)
- **odbc-crusher end-to-end**: 53/101 tests pass (52.5%), 2 fail, 46 skipped
  - Failures are pre-existing: mock driver doesn't support arbitrary `SELECT` queries with literal values
  - All connection, metadata, transaction, buffer validation, error queue, state machine, descriptor, and advanced feature tests pass
- **odbc-crusher unit tests**: 42/45 pass (93%), 3 fail (pre-existing: require real Firebird/MySQL drivers or arbitrary SQL execution)

### Remaining Work

- Phase M2: Internal API hardening (Unicode-aware `SQLGetData`, catalog column types)
- Phase M3: Spec compliance polish (SQLSTATEs, `DllMain`, connection attributes)
- Phase M4: Thread safety (per-handle locking)

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

- [ ] `SQLGetFunctions` bitmask matches actual exports
- [ ] Connection attributes expanded
- [ ] All SQLSTATEs reviewed against ODBC spec
- [ ] `DllMain` added for Windows

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

- [ ] Mutex added to all four handle types
- [ ] All API entry points acquire lock before work
- [ ] RAII lock guards prevent deadlocks

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
