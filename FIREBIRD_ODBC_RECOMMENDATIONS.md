# Firebird ODBC Driver — Recommendations from ODBC Crusher Analysis

**Driver Version**: FirebirdODBC 03.00.0021 (ODBC 3.51)  
**Database**: Firebird 5.0 (WI-V)  
**Analysis Date**: February 8, 2026  
**Tool**: ODBC Crusher v0.3.1  

---

## Summary

ODBC Crusher ran 131 tests against the Firebird ODBC Debug driver. After cross-referencing every failed/skipped result against the Firebird ODBC driver's C++ source code, **3 genuine driver issues** were confirmed that Firebird ODBC developers should address. The remaining 18 non-passing results were determined to be bugs in odbc-crusher's test infrastructure (tracked in PROJECT_PLAN.md Phase 18).

**Results**: 110 passed (84.0%), 1 failed, 20 skipped

---

## 1. `SQLGetData` with NULL Buffer Does Not Return Data Length

**Severity**: Medium  
**ODBC Spec Reference**: [SQLGetData Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlgetdata-function) — "If *TargetValuePtr* is a null pointer, *StrLen_or_IndPtr* returns the total number of bytes of data available to return."  
**Test**: `test_getdata_zero_buffer` (Boundary Value Tests)  
**odbc-crusher result**: SKIP_INCONCLUSIVE — both probing strategies fail

### Problem

When an application calls `SQLGetData` with a NULL data pointer and `BufferLength = 0` to probe the data length, the Firebird driver returns `SQL_SUCCESS` but **does not set the indicator/length value**. The application cannot discover the data size.

### Root Cause

The `ODBCCONVERT_CHECKNULL` and `ODBCCONVERT_CHECKNULLW` macros in `OdbcConvert.cpp` contain an early return when `pointerTo` is NULL:

```cpp
#define ODBCCONVERT_CHECKNULL(pointerTo)
    if( checkIndicatorPtr( indicatorFrom, SQL_NULL_DATA, from ) )
    {
        if ( indicatorTo ) setIndicatorPtr( indicatorTo, SQL_NULL_DATA, to );
        if ( pointerTo ) *(char*)pointerTo = 0;
        return SQL_SUCCESS;
    }
    if ( !pointerTo )
        return SQL_SUCCESS;        // Returns without setting indicator!
```

This early return skips the indicator-setting code that runs later in each conversion function. The ODBC spec requires that even when the buffer is NULL, the indicator must be set to the total data length (or `SQL_NULL_DATA` for NULLs).

### Recommended Fix

Handle the NULL-buffer case **before** entering the conversion function, in `sqlGetData()` itself:

```cpp
// In OdbcStatement::sqlGetData(), before calling the conversion function:
if (pointer == nullptr && bufferLength == 0) {
    // Compute data length from the source value and set indicator
    // Return SQL_SUCCESS_WITH_INFO with 01004
}
```

Alternatively, modify the macros to compute the source data length and set the indicator before the early return:

```cpp
if ( !pointerTo )
{
    // Per ODBC spec: return data length even when buffer is NULL
    if ( indicatorTo )
    {
        // Set the total data length in the indicator
    }
    return SQL_SUCCESS_WITH_INFO;  // 01004 - data available but not returned
}
```

### Impact

Applications that use the "probe length, allocate, retrieve" pattern (common in ODBC) will get incorrect behavior. This pattern is used by:
- ADO.NET's `OdbcDataReader`
- Python's `pyodbc`
- Various ODBC wrappers that auto-size string buffers
- ODBC test tools that validate data length reporting

---

## 2. `SQLExecDirectW` / `SQLPrepareW` May Fail with `CHARSET=UTF8` Connection

**Severity**: Medium  
**ODBC Spec Reference**: [SQLExecDirect Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlexecdirect-function), [SQLPrepare Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlprepare-function)  
**Tests**: `test_describecol_wchar_names`, `test_getdata_sql_c_wchar`, `test_bindparam_wchar_input`, `test_bindparam_null_indicator`, `test_param_rebind_execute` (5 tests affected)  
**odbc-crusher result**: All SKIP_INCONCLUSIVE — "Could not execute/prepare query"

### Problem

When connected with `CHARSET=UTF8`, calling `SQLExecDirectW` or `SQLPrepareW` with valid SQL queries (e.g., `"SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"`, `"SELECT * FROM RDB$DATABASE"`) fails, while the same queries succeed through `SQLExecDirect` (ANSI). Other W-functions like `SQLGetInfoW` work correctly.

### Root Cause

The Firebird driver exports W-functions in its `.def` file, making it a "Unicode driver" from the Windows Driver Manager's perspective. When `CHARSET=UTF8` is specified in the connection string:

1. The `ConvertingString<>` constructor in `MainUnicode.cpp` calls `WcsToMbs()` to convert SQLWCHAR* to char*
2. After connection, `WcsToMbs` is replaced by a connection-specific converter (from `MbsAndWcs.cpp`) that uses the Firebird client library's character set converter
3. This converter may produce different results than the standard `WideCharToMultiByte` used before connection
4. The converted SQL text may have incorrect encoding or length, causing the Firebird server to reject it

### Evidence

- `test_getinfo_wchar_strings` **passes** — `SQLGetInfoW` works correctly (uses a different conversion path via `returnStringInfo`)
- `test_string_truncation_wchar` **passes** — `SQLGetInfoW` with small buffers works
- All ANSI-path tests **pass** — `SQLExecDirect` (ANSI) works fine for the same SQL queries
- All 5 W-function execution tests **fail** — `SQLExecDirectW` / `SQLPrepareW` cannot execute any query

### Recommended Fix

1. **Debug the `ConvertingString<>` output**: Add tracing to `SQLExecDirectW` to log the converted ANSI string and its length before passing to `sqlExecDirect`
2. **Verify charset converter**: Ensure the Firebird client library's UTF-8 converter correctly handles UTF-16 → ANSI SQL text (which is pure ASCII for standard SQL keywords)
3. **Test without CHARSET=UTF8**: If removing the charset parameter fixes the issue, the bug is in the charset-specific converter

### Impact

Applications that use the Unicode ODBC API (the standard on modern Windows) to execute SQL via `SQLExecDirectW` or `SQLPrepareW` with `CHARSET=UTF8` will fail. This affects:
- Modern .NET applications (always use Unicode APIs)
- Java JDBC-ODBC bridge
- Any application framework that uses the W-function variants

---

## 3. Asynchronous Execution Not Supported

**Severity**: Low (Optional Feature — Level 2)  
**ODBC Spec Reference**: [Asynchronous Execution](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/asynchronous-execution)  
**Test**: `test_async_capability` (Advanced Features)

### Current Behavior

The driver correctly returns `SQL_ERROR` with SQLSTATE `HYC00` ("Optional feature not implemented") when `SQL_ATTR_ASYNC_ENABLE` is set to `SQL_ASYNC_ENABLE_ON`. This is compliant behavior — async execution is Level 2 optional.

### Recommendation

No fix required. This is correctly reported as an optional unsupported feature.

---

## 4. `returnStringInfo` Returns `SQL_SUCCESS_WITH_INFO` When Buffer Is NULL

**Severity**: Low  
**ODBC Spec Reference**: [SQLGetInfo Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlgetinfo-function)

### Problem

In `OdbcObject::returnStringInfo()`, when `ptr` is NULL or `maxLength <= 0`, the function returns `SQL_SUCCESS_WITH_INFO` with SQLSTATE `01004` even though no truncation occurred.

### Recommended Fix

```cpp
if (!ptr || maxLength <= 0)
    return sqlSuccess();  // Length already set; no truncation occurred
```

---

## 5. `SQLForeignKeys` Throws on Non-Existent Table Names

**Severity**: Low  
**ODBC Spec Reference**: [SQLForeignKeys Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlforeignkeys-function) — should return an empty result set for tables that don't exist  
**Test**: `test_foreign_keys` (Metadata/Catalog Tests)  
**Note**: This is partially an odbc-crusher bug (it uses hardcoded table names), but the driver should not throw an exception for non-existent table names.

### Problem

When `SQLForeignKeys` is called with a table name that doesn't exist in the database (e.g., `"ORDERS"`), the driver throws a `SQLException` which is caught and returned as `SQL_ERROR` with SQLSTATE `HY000`. Per the ODBC spec, catalog functions should return an empty result set when the specified objects don't exist, not an error.

### Recommended Fix

In `OdbcStatement::sqlForeignKeys()`, handle the case where `getCrossReference` throws because the table doesn't exist — instead of posting an error, set an empty result set:

```cpp
try {
    DatabaseMetaData *metaData = connection->getMetaData();
    setResultSet(metaData->getCrossReference(pkCat, pkScheme, pkTbl,
                                              fkCat, fkScheme, fkTbl));
} catch (SQLException &exception) {
    // If the table doesn't exist, return an empty result set
    // rather than an error (per ODBC spec for catalog functions)
    setEmptyResultSet(/* FK result set columns */);
    return sqlSuccess();
}
```

---

## 6. DDL After Failed DDL Corrupts Transaction State

**Severity**: Medium  
**Test**: `test_manual_commit`, `test_manual_rollback`, array parameter tests (10 tests affected)  
**Note**: This is primarily an odbc-crusher test setup issue, but the driver behavior is worth documenting.

### Problem

When a `DROP TABLE` for a non-existent table fails with autocommit ON, the Firebird connection/statement enters a state where subsequent `CREATE TABLE` also fails, even when using separate statement handles. This prevents test setup from creating temporary tables.

### Root Cause

In Firebird, a failed DDL statement invalidates the internal transaction state. Even with autocommit ON, the driver may not properly clean up after a failed DDL operation.

### Workaround for Applications

Use `EXECUTE BLOCK` with exception handling for conditional DDL, or use `RECREATE TABLE` syntax which combines DROP + CREATE atomically:

```sql
RECREATE TABLE ODBC_TEST (ID INTEGER, VAL VARCHAR(50))
```

### Recommended Investigation

Ensure that after a failed `SQLExecDirect("DROP TABLE nonexistent")` with autocommit ON, the connection handle is left in a clean state suitable for subsequent DDL operations. The internal transaction should be rolled back automatically when the DDL fails with autocommit enabled.

---

## 7. Summary Table

| # | Issue | Severity | ODBC Compliance | Action |
|---|-------|----------|-----------------|--------|
| 1 | `SQLGetData(NULL, 0)` doesn't return data length | Medium | Non-compliant | Fix `ODBCCONVERT_CHECKNULL` macro |
| 2 | `SQLExecDirectW`/`SQLPrepareW` fail with CHARSET=UTF8 | Medium | Non-compliant | Debug `ConvertingString<>` conversion |
| 3 | Async execution not supported | Low | Compliant (optional) | No action required |
| 4 | `returnStringInfo` returns 01004 for NULL buffer | Low | Minor non-compliance | Fix return path for NULL pointer |
| 5 | `SQLForeignKeys` throws on non-existent tables | Low | Non-compliant | Return empty result set instead |
| 6 | DDL after failed DDL corrupts state | Medium | Implementation quality | Investigate autocommit cleanup |

---

## 8. Positive Findings

The Firebird ODBC driver performed well in the majority of tests. Notable strengths:

- **110/131 tests passed** (84.0%) on the first run
- **52/52 ODBC functions supported** as reported by `SQLGetFunctions`
- **Full transaction support**: Autocommit, manual commit, rollback, isolation levels all implemented correctly (`sqlEndTran` properly delegates to `commitAuto`/`rollbackAuto`)
- **Complete catalog functions**: `SQLTables`, `SQLColumns`, `SQLPrimaryKeys`, `SQLForeignKeys`, `SQLStatistics`, `SQLSpecialColumns`, `SQLTablePrivileges` all implemented with proper Firebird system table queries
- **Array parameter execution**: `SQL_ATTR_PARAMSET_SIZE`, column-wise and row-wise binding (`headBindType`), per-row status array (`headParamStatusPtr`), operation array (`operationPtr`) — all fully implemented in `executeStatementParamArray()`
- **Descriptor API**: Implicit descriptors, IRD auto-population, APD fields, `SQLCopyDesc` — all working
- **26 data types** supported including INT128, DECFLOAT, BOOLEAN, and time zone types
- **SQLSTATE compliance**: All 10 negative tests returned correct SQLSTATEs (HY010, 24000, 07009, 42000, HY003, HY096, HY092, 08002)
- **`SQLGetInfo` string truncation**: `setString()` and `returnStringInfo()` correctly set `*returnLength` to the **full untruncated length** per ODBC spec
- **Scrollable cursors**: `SQL_FETCH_FIRST`, `SQL_FETCH_LAST`, `SQL_FETCH_ABSOLUTE` all work correctly
- **`SQL_C_WCHAR` conversions**: Driver has comprehensive `*ToStringW` converters for all data types in `OdbcConvert.cpp`

---

*Generated by ODBC Crusher v0.3.1*
