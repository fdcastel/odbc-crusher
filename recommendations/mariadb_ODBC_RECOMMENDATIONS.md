# MariaDB Connector/ODBC — Recommendations from ODBC Crusher Analysis

**Driver Analyzed**: MariaDB Connector/ODBC (libmaodbc.so)  
**Driver Version**: 03.01.0015  
**ODBC Version**: 03.51  
**DBMS Version**: MariaDB 10.11.000014  
**Analysis Tool**: ODBC Crusher v0.4.1  
**Analysis Date**: February 8, 2026

---

## Summary

ODBC Crusher ran **131 tests** against the MariaDB Connector/ODBC driver.

| Result | Count | Percentage |
|--------|------:|------------|
| PASS   |   115 |     87.8%  |
| FAIL   |     1 |      0.8%  |
| SKIP   |     1 |      0.8%  |
| *Inconclusive (test env)* | *14* | *10.7%* |

Of the 16 non-passing results:
- **1 is a confirmed driver deficiency** (actionable fix recommended below)
- **1 is a correctly-reported optional feature** (async not supported — Level 2 optional, no action required)
- **14 are odbc-crusher test environment issues** (table creation failures, catalog assumptions — not driver bugs)

The MariaDB Connector/ODBC driver is a **high-quality, spec-conformant driver**. It passed all Core and most Level 1/Level 2 tests. Only one genuine spec violation was found.

---

## Recommendation 1: `SQLSetConnectAttr` Should Reject Unknown Attributes with HY092

**Severity**: Low (ODBC spec conformance)  
**ODBC Function**: `SQLSetConnectAttr`  
**Test**: `test_setconnattr_invalid_attr`  
**ODBC Spec Reference**: [SQLSetConnectAttr Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlsetconnectattr-function) — SQLSTATE HY092

### What ODBC Crusher Found

When `SQLSetConnectAttr` is called with an invalid attribute value (e.g., `99999`), the driver returns `SQL_SUCCESS` instead of `SQL_ERROR` with SQLSTATE `HY092` ("Invalid attribute/option identifier").

### What the Driver Source Code Shows

In `ma_connection.cpp`, the `MADB_DbcSetAttr` method's `switch` statement has a `default:` case that behaves differently depending on build configuration:

```cpp
default:
#ifdef SQL_DRIVER_CONN_ATTR_BASE
    if (Attribute >= SQL_DRIVER_CONN_ATTR_BASE)
    {
        // Returns HYC00 ("Optional feature not implemented") — wrong SQLSTATE
        return MADB_SetError(&Error, MADB_ERR_HYC00, nullptr, 0);
    }
#endif
    break;  // Falls through — silently accepts the attribute!
```

There are **two issues**:

1. **When `SQL_DRIVER_CONN_ATTR_BASE` is defined** (ODBC 3.8+): Attributes ≥ 16384 (driver-specific range) return `HYC00` instead of the spec-mandated `HY092`. `HYC00` means "optional feature not implemented" — this is semantically wrong because `99999` is not an optional feature, it's an invalid attribute identifier.

2. **For attributes < 16384** (standard ODBC range) or when `SQL_DRIVER_CONN_ATTR_BASE` is not defined: The `default:` case falls through to `break`, and the function returns `SQL_SUCCESS` with no error — silently accepting a completely invalid attribute.

### Recommended Fix

```cpp
default:
#ifdef SQL_DRIVER_CONN_ATTR_BASE
    if (Attribute >= SQL_DRIVER_CONN_ATTR_BASE)
    {
        return MADB_SetError(&Error, MADB_ERR_HY092, nullptr, 0);
    }
#endif
    return MADB_SetError(&Error, MADB_ERR_HY092, nullptr, 0);
```

Both paths should return `HY092` ("Invalid attribute/option identifier") as required by the ODBC specification. The distinction between "unknown standard attribute" and "unknown driver-specific attribute" does not matter — both are invalid identifiers that the driver does not recognize.

### Impact

Low. Most applications never pass invalid attributes, so this is unlikely to cause real-world issues. However, spec conformance testing tools (like ODBC Crusher) will flag this, and applications that rely on error codes for feature detection may behave incorrectly.

---

## Correctly-Reported Optional Feature: Asynchronous Execution Not Supported

**Severity**: Informational (Level 2 optional feature)  
**ODBC Function**: `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE)`  
**Test**: `test_async_capability`  

### What ODBC Crusher Found

The driver accepts `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE, SQL_ASYNC_ENABLE_ON)` with `SQL_SUCCESS_WITH_INFO` (SQLSTATE `01S02` — "Option value changed") but does not persist the setting. `SQLGetStmtAttr(SQL_ATTR_ASYNC_ENABLE)` always returns `SQL_ASYNC_ENABLE_OFF`.

### What the Driver Source Code Shows

In `ma_statement.cpp`:

```cpp
// SQLSetStmtAttr
case SQL_ATTR_ASYNC_ENABLE:
    if ((SQLULEN)ValuePtr != SQL_ASYNC_ENABLE_OFF)
    {
        MADB_SetError(&Stmt->Error, MADB_ERR_01S02,
            "Option value changed to default (SQL_ATTR_ASYNC_ENABLE)", 0);
        ret = SQL_SUCCESS_WITH_INFO;
    }
    break;

// SQLGetStmtAttr
case SQL_ATTR_ASYNC_ENABLE:
    *(SQLPOINTER *)ValuePtr = SQL_ASYNC_ENABLE_OFF;
    break;
```

The driver correctly signals via `01S02` that it substituted a different value (OFF instead of ON). This is valid ODBC behavior for an optional Level 2 feature.

### Recommendation

**No action required.** Asynchronous execution is a Level 2 optional feature. The driver's behavior of returning `01S02` is the correct way to indicate that the feature is not supported. If async support is desired in the future, the driver would need to implement an asynchronous execution model internally.

---

## Tests That Passed — Notable Strengths

The MariaDB driver demonstrated excellent conformance in:

- **All 6 Connection Tests** — RAII handles, connection pooling, timeouts
- **All 15 Statement Tests** — prepared statements, parameter binding, result fetching, `SQLBindCol`, `SQLFreeStmt(SQL_UNBIND)`, `SQLRowCount`, `SQLNumParams`, `SQLDescribeParam`, `SQLNativeSql`
- **All 9 Data Type Tests** — integers, decimals, floats, strings, dates, NULLs, Unicode, binary, GUIDs
- **All 5 Descriptor Tests** — implicit descriptors, IRD auto-population, APD fields, `SQLCopyDesc`
- **All 2 Cancellation Tests** — `SQLCancel` on idle and after execution
- **All 5 Boundary Value Tests** — zero buffers, NULL indicators, empty SQL, column 0
- **All 10 Data Type Edge Cases** — INT_MIN/INT_MAX, empty strings, special chars, type conversions
- **All 6 Error Queue Tests** — diagnostic records, field extraction, error clearing
- **All 6 State Machine Tests** — valid/invalid transitions, prepare/execute cycles
- **All 6 Catalog Depth Tests** — result set shapes, search patterns, NULL parameters
- **All 4 Diagnostic Depth Tests** — SQLSTATE format, record count, row count
- **All 4 Cursor Behavior Tests** — forward-only, scrollable, re-reading columns
- **All 3 Parameter Binding Tests** — WCHAR input, NULL indicator, rebind+execute
- **9/10 SQLSTATE Tests** — correct error codes for most invalid operations

This is an impressively broad conformance profile.

---
*Generated by ODBC Crusher -- https://github.com/fdcastel/odbc-crusher/*

