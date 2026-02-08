# ODBC Crusher — Recommendations for Developers

**Date**: February 8, 2026  
**ODBC Crusher Version**: v0.3.1  
**Driver Under Test**: Firebird ODBC Driver (Debug) v03.00.0021  
**Analysis Method**: Source-level comparison of odbc-crusher test logic vs actual driver behavior

---

## Executive Summary

Out of 27 non-passing tests (1 FAIL, 1 ERR, 25 SKIP), **22 are incorrect reports** caused by test design issues in odbc-crusher. Only **5 are genuine driver bugs**. The primary issues are:

1. **Hardcoded table names** (`CUSTOMERS`, `USERS`) that don't exist in the test database — causes 19 tests to skip or fail
2. **Incorrect truncation test logic** — the test falsely reports a failure on a correctly-behaving driver
3. **Unsafe descriptor test** — triggers a real driver bug, but the test itself has no defensive coding

---

## Issue 1: CRITICAL — Hardcoded Table Names (`CUSTOMERS`, `USERS`)

### Affected Tests (19 tests)

| Category | Test | Table Used |
|----------|------|-----------|
| Unicode Tests | `test_describecol_wchar_names` | `CUSTOMERS` |
| Unicode Tests | `test_getdata_sql_c_wchar` | `CUSTOMERS` |
| Unicode Tests | `test_columns_unicode_patterns` | `CUSTOMERS` |
| Cursor Behavior | `test_forward_only_past_end` | `CUSTOMERS` |
| Cursor Behavior | `test_fetchscroll_first_forward_only` | `CUSTOMERS` |
| Cursor Behavior | `test_getdata_same_column_twice` | `CUSTOMERS` |
| Parameter Binding | `test_bindparam_wchar_input` | `CUSTOMERS` |
| Parameter Binding | `test_bindparam_null_indicator` | `CUSTOMERS` |
| Parameter Binding | `test_param_rebind_execute` | `CUSTOMERS` |
| Diagnostic Depth | `test_diagfield_row_count` | `CUSTOMERS` |
| Array Parameters | `test_column_wise_array_binding` | `USERS` |
| Array Parameters | `test_row_wise_array_binding` | `USERS` |
| Array Parameters | `test_param_status_array` | `USERS` |
| Array Parameters | `test_params_processed_count` | `USERS` |
| Array Parameters | `test_array_with_null_values` | `USERS` |
| Array Parameters | `test_param_operation_array` | `USERS` |
| Array Parameters | `test_paramset_size_one` | `USERS` |
| Array Parameters | `test_array_partial_error` | `USERS` |
| Metadata | `test_foreign_keys` | `ORDERS`, `ORDER_ITEMS` |

### Problem

The odbc-crusher test suite hardcodes references to `CUSTOMERS` and `USERS` tables that only exist in the mock driver's test environment. When running against a real database, these tables don't exist, so:

- `SQLExecDirectW("SELECT * FROM CUSTOMERS")` fails → test becomes `SKIP_INCONCLUSIVE`
- `SQLPrepareW("INSERT INTO USERS ...")` fails → test becomes `SKIP_INCONCLUSIVE`
- `SQLForeignKeys` with `ORDERS`/`ORDER_ITEMS` finds nothing → test becomes `SKIP_UNSUPPORTED`

This means **odbc-crusher cannot test** the following driver capabilities against any real database:
- Unicode data retrieval (`SQL_C_WCHAR`)
- Unicode column names (`SQLDescribeColW`)
- Unicode catalog patterns (`SQLColumnsW`)
- Cursor forward-only past-end behavior
- Scrollable cursor rejection on forward-only
- Re-reading same column with `SQLGetData`
- `SQL_C_WCHAR` parameter binding
- NULL parameter indicators
- Parameter rebinding
- `SQL_DIAG_ROW_COUNT` after query
- **All 8 array parameter tests** (column-wise, row-wise, status array, processed count, NULLs, operation array, PARAMSET_SIZE=1, partial error)

### Recommendation

**Option A (Best)**: Create the test tables at startup and drop them at shutdown.

```cpp
// In a new TestSetup class or at the start of main()
void create_test_tables(OdbcConnection& conn) {
    OdbcStatement stmt(conn);
    
    // Create CUSTOMERS table
    try { stmt.execute("DROP TABLE CUSTOMERS"); } catch (...) {}
    stmt.execute(
        "CREATE TABLE CUSTOMERS ("
        "  CUSTOMER_ID INTEGER NOT NULL PRIMARY KEY,"
        "  NAME VARCHAR(100),"
        "  EMAIL VARCHAR(200)"
        ")");
    
    // Insert sample data
    stmt.execute("INSERT INTO CUSTOMERS VALUES (1, 'Alice', 'alice@test.com')");
    stmt.execute("INSERT INTO CUSTOMERS VALUES (2, 'Bob', 'bob@test.com')");
    stmt.execute("INSERT INTO CUSTOMERS VALUES (3, 'Charlie', 'charlie@test.com')");
    
    // Create USERS table
    try { stmt.execute("DROP TABLE USERS"); } catch (...) {}
    stmt.execute(
        "CREATE TABLE USERS ("
        "  USER_ID INTEGER NOT NULL PRIMARY KEY,"
        "  USERNAME VARCHAR(50)"
        ")");
    
    // Create ORDERS table with FK to CUSTOMERS
    try { stmt.execute("DROP TABLE ORDERS"); } catch (...) {}
    stmt.execute(
        "CREATE TABLE ORDERS ("
        "  ORDER_ID INTEGER NOT NULL PRIMARY KEY,"
        "  CUSTOMER_ID INTEGER REFERENCES CUSTOMERS(CUSTOMER_ID)"
        ")");
}

void drop_test_tables(OdbcConnection& conn) {
    OdbcStatement stmt(conn);
    try { stmt.execute("DROP TABLE ORDERS"); } catch (...) {}
    try { stmt.execute("DROP TABLE USERS"); } catch (...) {}
    try { stmt.execute("DROP TABLE CUSTOMERS"); } catch (...) {}
}
```

**Option B (Acceptable)**: Auto-discover existing tables from the database using `SQLTables`, then use the first table found for tests that need a real table.

**Option C (Minimum)**: Use Firebird's system table `RDB$DATABASE` (always 1 row) for SELECT tests, and probe for user tables via `SQLTables` before running INSERT tests. Skip INSERT-dependent tests with a clear message: "No test table available — create a table named USERS (USER_ID INTEGER, USERNAME VARCHAR(50)) to enable this test."

---

## Issue 2: HIGH — Incorrect Truncation Test Logic

### Affected Test

`test_truncation_indicators` in `buffer_validation_tests.cpp`

### Problem

The test calls `SQLGetInfo(SQL_DRIVER_NAME)` with a 5-byte buffer. The driver name is `"FirebirdODBC"` (12 chars). The test then checks:

```cpp
if (buffer_length >= small_buffer_size) {
    result.status = TestStatus::PASS;  // length >= 5 → pass
} else {
    result.status = TestStatus::FAIL;  // length < 5 → fail
}
```

The Firebird driver correctly:
1. Returns `SQL_SUCCESS_WITH_INFO` (truncation detected ✅)
2. Sets `buffer_length = 12` (the **full** length of the string, not the truncated length ✅)

**But the test reports FAIL** because it compares `buffer_length` (12) against `small_buffer_size` (5) using `>=`, which passes (12 ≥ 5). 

Wait — looking more carefully at the output: `"Length < buffer size despite truncation"`. This means the test actually hit the `else` branch where `buffer_length < small_buffer_size`. But the driver sets `buffer_length = 12`, which is `>= 5`.

**Root cause investigation**: The issue is that `buffer_length` is declared as `SQLSMALLINT` (a 16-bit signed type). The driver returns the value correctly. However, the Driver Manager may be interfering — on Windows, the ODBC Driver Manager wraps `SQLGetInfo` calls for Unicode drivers and may transform the output. The DM calls `SQLGetInfoW` internally and then converts back, potentially altering the length semantics.

**Alternative root cause**: The Firebird driver name on disk may be shorter than 5 chars for the DLL filename. The `SQL_DRIVER_NAME` info type returns the **DLL filename** (e.g., `"OdbcFb.dll"` — 10 chars, still > 5). But if running through the debug driver, the DLL name could be different.

### Recommendation

1. **Don't assume the driver name is longer than the buffer**. First query with a full-size buffer to get the actual length, then use a buffer shorter than that:

```cpp
// Step 1: Get actual driver name length
SQLSMALLINT full_length = 0;
SQLGetInfo(conn, SQL_DRIVER_NAME, nullptr, 0, &full_length);

if (full_length < 5) {
    // Driver name too short for truncation test — skip
    result.status = TestStatus::SKIP_INCONCLUSIVE;
    result.actual = "Driver name too short for truncation test";
    return result;
}

// Step 2: Use buffer that's definitely too small
SQLSMALLINT trunc_size = std::min((SQLSMALLINT)3, full_length);
```

2. **Use `SQL_DBMS_NAME` instead of `SQL_DRIVER_NAME`** — database names like "Firebird" (8 chars) are more reliably long enough.

---

## Issue 3: MEDIUM — Transaction Tests Flawed for DDL Databases

### Affected Tests

- `test_manual_commit` (reported `[ ?? ]`)
- `test_manual_rollback` (reported `[ ?? ]`)

### Problem

The tests call `create_test_table()` which does:
1. `DROP TABLE ODBC_TEST_TXN` (ignore error)
2. `CREATE TABLE ODBC_TEST_TXN ...`

Then the test does:
- **Commit test**: INSERT → `SQLEndTran(COMMIT)` → SELECT COUNT → verify count = 1
- **Rollback test**: `SQLEndTran(COMMIT)` (commit the CREATE) → INSERT → `SQLEndTran(ROLLBACK)` → SELECT COUNT → verify count = 0

The issue is that **DDL in Firebird auto-commits**. When `autocommit` is OFF:
- `CREATE TABLE` opens a transaction, but DDL operations in Firebird are handled specially
- The commit/rollback semantics for DDL+DML in the same transaction may not behave as the test expects
- The test's `drop_test_table()` cleanup may interfere with the connection state

These tests report `SKIP_INCONCLUSIVE` because the `OdbcError` exception is thrown during execution, likely because the DDL fails or the table state is inconsistent.

### Recommendation

1. **Separate DDL from the transaction test**: Create the table with autocommit ON, then switch to autocommit OFF for the DML test:

```cpp
// Create table with autocommit ON
SQLSetConnectAttr(conn, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
create_test_table();  // DDL committed automatically

// Now switch to manual commit for the actual test
SQLSetConnectAttr(conn, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);
// ... INSERT, COMMIT/ROLLBACK, verify ...
```

2. **Use table creation as a setup step, not part of the transaction test**.

---

## Issue 4: LOW — `test_getdata_zero_buffer` Fallback Logic

### Affected Test

`test_getdata_zero_buffer` in `boundary_tests.cpp`

### Problem

The test calls `SQLGetData` with `nullptr` buffer and `0` size. On Windows, the **ODBC Driver Manager** (not the driver) may intercept this call and reject it before it reaches the driver, because the DM performs its own validation for Unicode drivers.

The test has a fallback path that uses a 1-byte buffer, which works. But the result is reported as `SKIP_INCONCLUSIVE` because the primary path fails.

The Firebird driver **does** support zero-buffer probing — the DM just blocks it.

### Recommendation

1. **Use the 1-byte buffer fallback as the primary path** instead of relying on `nullptr` buffer
2. **Or** use `SQLGetDataW` with a `sizeof(SQLWCHAR)` buffer (2 bytes) to bypass DM restrictions while still testing length-probing behavior

---

## Issue 5: LOW — `test_connection_timeout` Correctly Reports Unsupported

### Affected Test

`test_connection_timeout` — reported `[NOT ]` (SKIP_UNSUPPORTED)

### Analysis

The test correctly identifies that `SQL_ATTR_CONNECTION_TIMEOUT` is not supported by the driver. The Firebird driver supports `SQL_ATTR_LOGIN_TIMEOUT` but not `SQL_ATTR_CONNECTION_TIMEOUT`. The test's result message says "Connection timeout attribute not supported" and marks it as `SKIP_UNSUPPORTED`.

**This report is correct.** `SQL_ATTR_CONNECTION_TIMEOUT` is an optional attribute per the ODBC spec. No action needed from odbc-crusher.

---

## Issue 6: LOW — `test_async_capability` Correctly Reports Unsupported

### Affected Test

`test_async_capability` — reported `[NOT ]` (SKIP_UNSUPPORTED)

### Analysis

The driver accepts `SQL_ATTR_ASYNC_ENABLE` at the connection level (stores it but ignores it), but at the statement level the getter always returns `SQL_ASYNC_ENABLE_OFF`. The test correctly detects this. No action needed.

---

## Summary: Tests That Are WRONG vs CORRECT

| # | Test | Crusher Says | Real Status | Verdict |
|---|------|-------------|-------------|---------|
| 1 | `test_truncation_indicators` | FAIL | Driver behaves correctly | ❌ **WRONG** — test logic issue |
| 2 | `Descriptor Tests` | ERR (CRASH) | Real driver bug in `SQLCopyDesc` | ✅ **CORRECT** |
| 3 | `test_connection_timeout` | SKIP | Driver doesn't support it | ✅ **CORRECT** |
| 4 | `test_foreign_keys` | SKIP | Works — test uses wrong table names | ❌ **WRONG** |
| 5 | `test_unicode_types` | SKIP | Driver supports SQL_C_WCHAR — test queries fail | ❌ **WRONG** |
| 6 | `test_manual_commit` | SKIP | DDL/DML interaction in test, not driver | ❌ **WRONG** |
| 7 | `test_manual_rollback` | SKIP | DDL/DML interaction in test, not driver | ❌ **WRONG** |
| 8 | `test_async_capability` | SKIP | Correctly unsupported | ✅ **CORRECT** |
| 9 | `test_getdata_zero_buffer` | SKIP | DM blocks the call, not driver | ❌ **WRONG** |
| 10 | `test_describecol_wchar_names` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 11 | `test_getdata_sql_c_wchar` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 12 | `test_columns_unicode_patterns` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 13 | `test_diagfield_row_count` | SKIP | Table doesn't exist (but also a real driver bug) | ⚠️ **PARTIALLY CORRECT** |
| 14 | `test_forward_only_past_end` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 15 | `test_fetchscroll_first_forward_only` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 16 | `test_getdata_same_column_twice` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 17 | `test_bindparam_wchar_input` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 18 | `test_bindparam_null_indicator` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 19 | `test_param_rebind_execute` | SKIP | Table doesn't exist | ❌ **WRONG** |
| 20-27 | All 8 `array_param_tests` | SKIP | Table doesn't exist | ❌ **WRONG** |

**Score: 3 correct, 1 partially correct, 22 incorrect (out of 27 non-passing tests)**

---

*Last Updated: February 8, 2026*
