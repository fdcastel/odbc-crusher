# PostgreSQL ODBC Driver (psqlodbc) — Recommendations from ODBC Crusher Analysis

**Driver**: psqlodbcw.so v16.00.0000 (ODBC 3.51)  
**Database**: PostgreSQL 16.0.11  
**Test Environment**: Linux (unixODBC 3.52 DM)  
**Test Tool**: ODBC Crusher v0.4.5  
**Analysis Date**: February 8, 2026  

## Summary

ODBC Crusher ran **131 tests** against the PostgreSQL ODBC driver (psqlodbc).  
**129 passed (98.5%), 0 failed, 2 skipped.**

This is an outstanding result — the psqlodbc driver is one of the most ODBC-conformant
drivers tested. Both non-passing results are genuine driver conformance gaps confirmed
by reviewing the psqlodbc source code at tag `REL-16_00_0000`.

Additionally, 2 passed tests returned non-standard SQLSTATEs (documented below as
low-severity recommendations).

---

## Recommendation 1: Support `SQL_ATTR_CURSOR_SCROLLABLE` as a setter

**Severity**: Low (INFO)  
**Test affected**: `test_cursor_scrollable_attr` (SKIP)  
**ODBC Function**: `SQLSetStmtAttr(SQL_ATTR_CURSOR_SCROLLABLE)`  
**ODBC Spec**: ODBC 3.x §SQLSetStmtAttr — SQL_ATTR_CURSOR_SCROLLABLE  
**Conformance**: Level 2  

### Current behavior

The driver supports `SQL_ATTR_CURSOR_SCROLLABLE` as a **read-only derived attribute**.
In `pgapi30.c`, `PGAPI_GetStmtAttr` correctly returns `SQL_SCROLLABLE` or
`SQL_NONSCROLLABLE` based on the current `stmt->options.cursor_type`. However,
`PGAPI_SetStmtAttr` **unconditionally rejects** it:

```c
// pgapi30.c — PGAPI_SetStmtAttr
case SQL_ATTR_CURSOR_SCROLLABLE:        /* -1 */
case SQL_ATTR_CURSOR_SENSITIVITY:       /* -2 */
case SQL_ATTR_AUTO_IPD:                 /* 10001 */
    SC_set_error(stmt, DESC_OPTION_NOT_FOR_THE_DRIVER, "Unsupported statement option (Set)", func);
    return SQL_ERROR;
```

The driver **does** support scrollable cursors — tests `test_fetch_scroll_first_last`
and `test_fetch_scroll_absolute` both pass using `SQL_ATTR_CURSOR_TYPE = SQL_CURSOR_STATIC`.

### Expected behavior

Per ODBC 3.x, `SQL_ATTR_CURSOR_SCROLLABLE` and `SQL_ATTR_CURSOR_TYPE` are related
attributes. Setting `SQL_ATTR_CURSOR_SCROLLABLE = SQL_SCROLLABLE` should internally
set the cursor type to `SQL_CURSOR_STATIC` (or the driver's scrollable default).
Setting `SQL_NONSCROLLABLE` should set it to `SQL_CURSOR_FORWARD_ONLY`. This is the
ODBC 3.x preferred interface for requesting scrollability.

### Suggested fix

In `pgapi30.c`, in the `PGAPI_SetStmtAttr` switch, replace the rejection with:

```c
case SQL_ATTR_CURSOR_SCROLLABLE:
    if ((SQLUINTEGER)(SQLULEN)Value == SQL_SCROLLABLE)
        SC_set_cursor_type(stmt, SQL_CURSOR_STATIC);
    else
        SC_set_cursor_type(stmt, SQL_CURSOR_FORWARD_ONLY);
    break;
```

### Impact

Low. All applications can already use `SQL_ATTR_CURSOR_TYPE` directly. However, some
ODBC frameworks and ORMs (e.g., .NET `System.Data.Odbc`, certain Java JDBC-ODBC bridges)
use `SQL_ATTR_CURSOR_SCROLLABLE` as the primary interface for requesting scrollability.
Supporting it improves compatibility with those frameworks.

---

## Recommendation 2: Properly handle `SQL_ATTR_ASYNC_ENABLE` in `SQLSetStmtAttr`

**Severity**: Low (INFO)  
**Test affected**: `test_async_capability` (SKIP)  
**ODBC Function**: `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE)`  
**ODBC Spec**: ODBC 3.x §SQLSetStmtAttr — SQL_ATTR_ASYNC_ENABLE  
**Conformance**: Level 2 (optional)  

### Current behavior

The driver silently ignores `SQL_ATTR_ASYNC_ENABLE` on set but returns
`SQL_ASYNC_ENABLE_OFF` on get, creating an inconsistency:

```c
// options.c — set_statement_option
case SQL_ASYNC_ENABLE:  /* ignored */
    break;

// options.c — PGAPI_GetStmtOption
case SQL_ASYNC_ENABLE:  /* NOT SUPPORTED */
    *((SQLINTEGER *) pvParam) = SQL_ASYNC_ENABLE_OFF;
    break;
```

The driver also reports `SQL_AM_NONE` via `SQLGetInfo(SQL_ASYNC_MODE)` and
`0` for `SQL_MAX_ASYNC_CONCURRENT_STATEMENTS`, confirming async is not implemented.

The problem is that `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE, SQL_ASYNC_ENABLE_ON)`
returns `SQL_SUCCESS`, which makes the application believe async is enabled, while
`SQLGetStmtAttr` immediately returns `SQL_ASYNC_ENABLE_OFF`. Applications that
check the return code of `SQLSetStmtAttr` without verifying via `SQLGetStmtAttr`
will incorrectly believe async is active.

### Expected behavior

Per the ODBC specification, when a driver does not support an optional feature, it
should return `SQL_ERROR` with SQLSTATE `HYC00` ("Optional feature not implemented").
Silently returning `SQL_SUCCESS` for an unsupported setting violates the principle of
least surprise.

### Suggested fix

In `options.c`, change the `SQL_ASYNC_ENABLE` case in `set_statement_option()`:

```c
case SQL_ASYNC_ENABLE:
    if ((SQLULEN)Value == SQL_ASYNC_ENABLE_ON) {
        SC_set_error(stmt, STMT_OPTION_NOT_FOR_THE_DRIVER,
                     "Asynchronous execution is not supported", func);
        return SQL_ERROR;
    }
    break;  /* SQL_ASYNC_ENABLE_OFF is a no-op, which is correct */
```

### Impact

Minimal. The driver does not support async execution and no applications should
depend on it. This is a spec-correctness improvement that helps application code
detect the lack of async support through the standard return code mechanism.

---

## Recommendation 3: Return SQLSTATE `HY096` for invalid `SQLGetInfo` info types

**Severity**: Low (INFO)  
**Test affected**: `test_getinfo_invalid_type` (PASS, but with non-standard SQLSTATE)  
**ODBC Function**: `SQLGetInfo`  
**ODBC Spec**: ODBC 3.x §SQLGetInfo — HY096 "Invalid information type"  

### Current behavior

When `SQLGetInfo` is called with an unrecognized info type, the driver returns
`SQL_ERROR` with SQLSTATE `HYC00` ("Optional feature not implemented"). This happens
because `PGAPI_GetInfo` in `info.c` uses `CONN_NOT_IMPLEMENTED_ERROR` as the error
code, which maps to `HYC00` via the connection error table in `connection.c`.

### Expected behavior

The ODBC specification requires SQLSTATE `HY096` for unrecognized or invalid info type
values. `HYC00` is reserved for recognized-but-unimplemented features (e.g., a known
info type that the driver chose not to implement).

### Suggested fix

In `connection.c`, add a dedicated error code and mapping for invalid info types, or
update the `CONN_NOT_IMPLEMENTED_ERROR` mapping to `HY096` when raised from
`PGAPI_GetInfo`. Alternatively, add a new entry to the connection error table:

```c
// In the connection error code enum:
CONN_INVALID_INFO_TYPE,

// In the SQLSTATE mapping table:
case CONN_INVALID_INFO_TYPE:
    pg_sqlstate_set(env, szSqlState, "HY096", "S1096");
    break;
```

### Impact

Minimal. The error behavior (returning `SQL_ERROR`) is correct. Only the SQLSTATE
code differs from the spec requirement.

---

## Recommendation 4: Return SQLSTATE `HY092` for invalid connection attributes

**Severity**: Low (INFO)  
**Test affected**: `test_setconnattr_invalid_attr` (PASS, but with non-standard SQLSTATE)  
**ODBC Function**: `SQLSetConnectAttr`  
**ODBC Spec**: ODBC 3.x §SQLSetConnectAttr — HY092 "Invalid attribute/option identifier"  

### Current behavior

When `SQLSetConnectAttr` is called with an unrecognized attribute (e.g., 99999),
the driver returns `SQL_ERROR` with SQLSTATE `HY000` ("General error"). This happens
because the `CONN_OPTION_NOT_FOR_THE_DRIVER` error code has no explicit SQLSTATE
mapping in the connection-level error table, so it falls through to the generic `HY000`.

### Expected behavior

Invalid/unrecognized attributes should return SQLSTATE `HY092` ("Invalid
attribute/option identifier"). The driver already has proper `HY092` mapping at the
statement level (`STMT_INVALID_OPTION_IDENTIFIER`) but lacks the equivalent mapping
at the connection level.

### Suggested fix

In `connection.c`, add a SQLSTATE mapping for `CONN_OPTION_NOT_FOR_THE_DRIVER`:

```c
case CONN_OPTION_NOT_FOR_THE_DRIVER:
    pg_sqlstate_set(env, szSqlState, "HY092", "S1092");
    break;
```

### Impact

Minimal. Same situation as Recommendation 3 — correct error behavior with a
non-standard SQLSTATE code.

---

## Overall Assessment

The psqlodbc driver (v16.00.0000) is **highly ODBC-conformant** with a 98.5% pass rate.
All Core conformance features are fully implemented. The driver excels at:

- ✅ Complete descriptor API (`SQLCopyDesc`, `SQLGetDescField`, `SQLSetDescField`)
- ✅ Full array parameter support (column-wise, row-wise, status arrays, operation arrays, processed counts)
- ✅ Proper buffer validation (null termination, sentinel preservation, truncation reporting with `01004`)
- ✅ Correct state machine transitions (`HY010` for function sequence errors)
- ✅ Complete diagnostic depth (`SQLGetDiagRec`, `SQLGetDiagField`, multiple records)
- ✅ Scrollable cursor support via `SQL_ATTR_CURSOR_TYPE` (static, forward-only)
- ✅ Transaction isolation level support (read committed default)
- ✅ Full Unicode support (`SQL_C_WCHAR` throughout, `SQLColumnsW`, `SQLTablesW`)
- ✅ All 9 data type categories including GUID/UUID
- ✅ Cancellation support (`SQLCancel` on idle and active statements)
- ✅ Complete SQLSTATE validation (10/10 tests passed)
- ✅ Binary data (`bytea`) retrieval via `SQL_C_BINARY`

The 4 recommendations are all low-severity improvements:
- 2 involve unsupported optional features (Level 2 conformance)
- 2 involve non-standard SQLSTATE codes for error conditions that are otherwise correct

---
Generated by ODBC Crusher -- https://github.com/fdcastel/odbc-crusher/
