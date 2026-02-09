# PostgreSQL ODBC Driver (psqlodbc) — Recommendations from ODBC Crusher Analysis

**Driver**: psqlodbcw.so v16.00.0000 (ODBC 3.51)  
**Database**: PostgreSQL 16.0.11  
**Test Environment**: Linux (unixODBC 3.52 DM)  
**Test Tool**: ODBC Crusher v0.4.1  
**Analysis Date**: February 8, 2026  

## Summary

ODBC Crusher ran 131 tests against the PostgreSQL ODBC driver (psqlodbc).  
**125 passed (95.4%), 0 failed, 6 skipped.**

This is an excellent result. The psqlodbc driver is one of the most ODBC-conformant
drivers tested. Of the 6 non-passing results:
- **3 are minor driver conformance issues** (documented below)
- **1 is a correctly-reported optional feature** (async execution)
- **4 are odbc-crusher bugs** (tracked in PROJECT_PLAN.md)

Additionally, 2 passed tests had minor SQLSTATE mapping notes (also documented below).

---

## Recommendation 1: Support SQL_ATTR_CURSOR_SCROLLABLE as a setter

**Severity**: INFO (Low)  
**Test affected**: `test_cursor_scrollable_attr`  
**ODBC Spec**: ODBC 3.x §SQLSetStmtAttr — SQL_ATTR_CURSOR_SCROLLABLE

### Current behavior

The driver supports `SQL_ATTR_CURSOR_SCROLLABLE` as a **getter** (returning
`SQL_SCROLLABLE` or `SQL_NONSCROLLABLE` based on the current cursor type), but
**rejects it as a setter** with `HYC00` ("Optional feature not implemented").

Scrollable cursors work correctly via the traditional `SQL_ATTR_CURSOR_TYPE`
mechanism (tests `test_fetch_scroll_first_last` and `test_fetch_scroll_absolute`
both pass). This means the underlying capability exists.

### Expected behavior

Per ODBC 3.x, `SQLSetStmtAttr(SQL_ATTR_CURSOR_SCROLLABLE, SQL_SCROLLABLE)` should
internally set the cursor type to `SQL_CURSOR_STATIC` (or the driver's scrollable
equivalent). This is the ODBC 3.x preferred interface for requesting scrollability.

### Suggested fix

In `pgapi30.c`, in the `PGAPI_SetStmtAttr` switch, add handling for `SQL_ATTR_CURSOR_SCROLLABLE`:

```c
case SQL_ATTR_CURSOR_SCROLLABLE:
    if ((SQLUINTEGER)(SQLULEN)Value == SQL_SCROLLABLE)
        SC_set_cursor_type(stmt, SQL_CURSOR_STATIC);
    else
        SC_set_cursor_type(stmt, SQL_CURSOR_FORWARD_ONLY);
    break;
```

### Impact

Low. Applications can use `SQL_ATTR_CURSOR_TYPE` directly. However, some ODBC
frameworks and ORMs use `SQL_ATTR_CURSOR_SCROLLABLE` as the primary interface.

---

## Recommendation 2: Return SQLSTATE HY096 for invalid SQLGetInfo info types

**Severity**: INFO (Low)  
**Test affected**: `test_getinfo_invalid_type` (passed, but with wrong SQLSTATE)  
**ODBC Spec**: ODBC 3.x §SQLGetInfo — HY096 "Invalid information type"

### Current behavior

When `SQLGetInfo` is called with an unrecognized info type, the driver sets error code
`CONN_NOT_IMPLEMENTED_ERROR` (209) which maps to SQLSTATE `HYC00` ("Optional feature
not implemented") via the connection error table in `connection.c`.

### Expected behavior

The ODBC specification requires SQLSTATE `HY096` for unrecognized or invalid info type
values. `HYC00` is for recognized-but-unimplemented features.

### Suggested fix

In `connection.c`, add a mapping for `HY096` and use a new error code for invalid
info types, or map `CONN_NOT_IMPLEMENTED_ERROR` to `HY096` when raised from
`PGAPI_GetInfo`.

### Impact

Minimal. The error behavior (returning `SQL_ERROR`) is correct. Only the SQLSTATE
code is wrong.

---

## Recommendation 3: Return SQLSTATE HY092 for invalid connection attributes

**Severity**: INFO (Low)  
**Test affected**: `test_setconnattr_invalid_attr` (passed, but with wrong SQLSTATE)  
**ODBC Spec**: ODBC 3.x §SQLSetConnectAttr — HY092 "Invalid attribute/option identifier"

### Current behavior

When `SQLSetConnectAttr` is called with an unrecognized attribute (e.g., 99999),
the driver sets `CONN_OPTION_NOT_FOR_THE_DRIVER` which falls through to the
default SQLSTATE `HY000` ("General error") because there is no explicit mapping
for this error code in the connection-level SQLSTATE table.

### Expected behavior

Invalid/unrecognized attributes should return SQLSTATE `HY092`. The driver already
has proper `HY092` mapping at the statement level (`STMT_INVALID_OPTION_IDENTIFIER`)
but lacks the equivalent mapping at the connection level.

### Suggested fix

In `connection.c`, add a SQLSTATE mapping for `CONN_OPTION_NOT_FOR_THE_DRIVER` → `HY092`:

```c
case CONN_OPTION_NOT_FOR_THE_DRIVER:
    pg_sqlstate_set(env, szSqlState, "HY092", "S1092");
    break;
```

### Impact

Minimal. Same situation as Recommendation 2 — correct error behavior with wrong SQLSTATE.

---

## Tests with Correct Behavior (No Action Needed)

| Test | Status | Why |
|------|--------|-----|
| `test_async_capability` | SKIP | Level 2 optional; psqlodbc silently accepts the set but never enables async. Acceptable for a synchronous driver. |
| `test_special_columns` | SKIP | odbc-crusher bug — test uses hardcoded Firebird/MySQL table names, not PostgreSQL tables |
| `test_binary_types` | SKIP | odbc-crusher bug — test lacks PostgreSQL `bytea`/`decode()` binary syntax |
| `test_columns_unicode_patterns` | SKIP | odbc-crusher discovers `information_schema` view which returns 0 columns via SQLColumns (expected for dynamically-defined views) |
| `test_tables_search_patterns` | SKIP | odbc-crusher bug — passes `nullptr` instead of `""` for SQL_ALL_TABLE_TYPES enumeration |

---

## Overall Assessment

The psqlodbc driver is **highly ODBC-conformant** with only 3 minor SQLSTATE/attribute
issues. All Core conformance features are fully implemented. The driver excels at:

- ✅ Complete descriptor API (SQLCopyDesc, SQLGetDescField, SQLSetDescField)
- ✅ Full array parameter support (status, operation, processed count)
- ✅ Proper buffer validation (sentinel preservation, truncation reporting)
- ✅ Correct state machine transitions
- ✅ Complete diagnostic depth (SQLSTATE 42601 for syntax errors)
- ✅ Scrollable cursor support via SQL_ATTR_CURSOR_TYPE
- ✅ Transaction isolation level support
- ✅ Unicode (SQL_C_WCHAR) throughout

The 3 recommendations are all low-severity SQLSTATE mapping improvements.

---
*Generated by ODBC Crusher -- https://github.com/fdcastel/odbc-crusher/*
