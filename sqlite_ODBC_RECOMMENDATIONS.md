# SQLite ODBC Driver — Recommendations from ODBC Crusher Analysis

**Driver**: sqlite3odbc.so v0.99991 (ODBC 3.00)  
**Database**: SQLite 3.45.1  
**Test Environment**: Linux (unixODBC 3.52 DM)  
**Test Tool**: ODBC Crusher v0.4.1  
**Analysis Date**: February 8, 2026  

## Summary

ODBC Crusher ran 131 tests against the SQLite ODBC driver.  
**116 passed (88.5%), 7 failed, 8 skipped.**

Of the 15 non-passing results:
- **9 are genuine driver deficiencies** (documented below)
- **2 are correctly-reported optional features** (async execution, connection timeout)
- **3 are legitimate skips** (empty test database, driver truncation edge case)
- **1 is an odbc-crusher bug** (tracked in PROJECT_PLAN.md)

---

## Recommendation 1: Implement SQL_ATTR_PARAM_STATUS_PTR population

**Severity**: WARNING  
**Tests affected**: `test_param_status_array`, `test_paramset_size_one`, `test_param_operation_array`, `test_array_partial_error`  
**ODBC Spec**: ODBC 3.x §Arrays of Parameter Values — Parameter Status Array

### Current behavior

`SQLSetStmtAttr(SQL_ATTR_PARAM_STATUS_PTR, ...)` correctly stores the pointer in `s->parm_status`, but `drvexecute()` **never writes to this array**. After array execution, all entries remain at their initial value (0xFFFF).

### Expected behavior

Per the ODBC 3.x specification, after executing a statement with `SQL_ATTR_PARAMSET_SIZE > 1`, the driver must fill each entry of the status array with one of:
- `SQL_PARAM_SUCCESS` (0) — row executed successfully
- `SQL_PARAM_SUCCESS_WITH_INFO` (1) — row executed with diagnostic info
- `SQL_PARAM_ERROR` (5) — row caused an error
- `SQL_PARAM_UNUSED` (7) — row was skipped (due to `SQL_PARAM_IGNORE` in the operation array, or because a prior error aborted remaining rows)

### Fix

In the `drvexecute()` function, after each parameter set is processed in the array execution loop, write the appropriate status:

```c
/* Inside the parameter set loop in drvexecute() */
if (s->parm_status) {
    if (rc == SQL_SUCCESS) {
        s->parm_status[i] = SQL_PARAM_SUCCESS;
    } else if (rc == SQL_SUCCESS_WITH_INFO) {
        s->parm_status[i] = SQL_PARAM_SUCCESS_WITH_INFO;
    } else {
        s->parm_status[i] = SQL_PARAM_ERROR;
    }
}
```

Also, for `SQL_ATTR_PARAMSET_SIZE = 1`, the single status entry should be set to `SQL_PARAM_SUCCESS` after successful execution.

---

## Recommendation 2: Implement SQL_ATTR_PARAM_OPERATION_PTR checking

**Severity**: INFO  
**Tests affected**: `test_param_operation_array`, `test_array_partial_error`  
**ODBC Spec**: ODBC 3.x §Using Arrays of Parameters — SQL_ATTR_PARAM_OPERATION_PTR

### Current behavior

`SQLSetStmtAttr(SQL_ATTR_PARAM_OPERATION_PTR, ...)` correctly stores the pointer in `s->parm_oper`, but `drvexecute()` **never checks this array**. All parameter sets are always executed, even if marked `SQL_PARAM_IGNORE`.

### Expected behavior

Before executing each parameter set, the driver should check the operation array:
- `SQL_PARAM_PROCEED` (0) — execute this parameter set
- `SQL_PARAM_IGNORE` (1) — skip this parameter set and set its status to `SQL_PARAM_UNUSED`

### Fix

In the `drvexecute()` parameter set loop:

```c
/* Inside the parameter set loop */
if (s->parm_oper && s->parm_oper[i] == SQL_PARAM_IGNORE) {
    if (s->parm_status) {
        s->parm_status[i] = SQL_PARAM_UNUSED;
    }
    continue;  /* Skip to next parameter set */
}
```

---

## Recommendation 3: Implement SQLCopyDesc

**Severity**: WARNING  
**Tests affected**: `test_copy_desc`  
**ODBC Spec**: ODBC 3.x §SQLCopyDesc — Core Conformance

### Current behavior

`SQLCopyDesc` returns `SQL_ERROR` unconditionally with no diagnostic message.

### Expected behavior

`SQLCopyDesc` is a Core conformance function. It should copy the fields from one descriptor handle to another. At minimum, the driver should:
1. Copy all relevant header and record fields from the source descriptor to the target descriptor
2. Handle the distinction between explicit and implicit descriptors
3. Return `SQL_SUCCESS` on success

### Note

Since `SQLGetDescField` and `SQLSetDescField` are also unimplemented, a full descriptor API implementation would be needed. If full descriptor support is too large a scope, at minimum consider implementing `SQLCopyDesc` for the common use case of copying an ARD/APD between statements.

---

## Recommendation 4: Implement SQLGetDescField / SQLSetDescField

**Severity**: INFO  
**Tests affected**: `test_ird_after_prepare`, `test_apd_fields`  
**ODBC Spec**: ODBC 3.x §SQLGetDescField, §SQLSetDescField — Core Conformance

### Current behavior

Both `SQLGetDescField` and `SQLSetDescField` return `SQL_ERROR` unconditionally ("Function not implemented").

### Expected behavior

These are Core conformance functions. After `SQLPrepare`, the IRD (Implementation Row Descriptor) should be auto-populated with column metadata that is accessible via `SQLGetDescField`. Similarly, `SQLSetDescField` should allow applications to configure APD/ARD fields for parameter and column binding.

### Fix

The driver already has internal column metadata (populated by `setupdyncols()` after prepare/execute). Expose this metadata through the descriptor API:

For `SQLGetDescField` on the IRD:
- `SQL_DESC_TYPE` → column SQL type (already in `dyncols[i].type`)
- `SQL_DESC_NAME` → column name (already in `dyncols[i].column`)
- `SQL_DESC_COUNT` → number of columns
- `SQL_DESC_NULLABLE` → nullability
- `SQL_DESC_PRECISION` / `SQL_DESC_SCALE` → numeric precision/scale

---

## Recommendation 5: Return SQLSTATE 42000 for SQL syntax errors

**Severity**: WARNING  
**Tests affected**: `test_execdirect_syntax_error`  
**ODBC Spec**: ODBC 3.x Appendix A: SQLSTATE codes — 42000 "Syntax error or access violation"

### Current behavior

All SQLite errors (including syntax errors like `SQLITE_ERROR` from an invalid SQL statement) are mapped to the generic `HY000` ("General error"). The driver uses a single pattern throughout:

```c
setstat(s, rc, "%s (%d)", (*s->ov3) ? "HY000" : "S1000", errp, rc);
```

### Expected behavior

The ODBC specification defines specific SQLSTATEs for different error categories:
- `42000` — Syntax error or access violation
- `42S02` — Base table or view not found
- `42S01` — Base table or view already exists
- `42S22` — Column not found
- `HY000` — General error (for truly unknown errors)

### Fix

Map SQLite error codes to appropriate ODBC SQLSTATEs. SQLite provides the error code from `sqlite3_errcode()`:

```c
const char *map_sqlite_to_sqlstate(int sqlite_errcode) {
    switch (sqlite_errcode) {
        case SQLITE_ERROR:      return "42000";  /* SQL error (includes syntax) */
        case SQLITE_PERM:       return "42000";  /* Access denied */
        case SQLITE_CONSTRAINT: return "23000";  /* Integrity constraint violation */
        case SQLITE_MISMATCH:   return "07006";  /* Type mismatch */
        case SQLITE_RANGE:      return "07009";  /* Column out of range */
        case SQLITE_NOTADB:     return "08001";  /* Cannot open database */
        case SQLITE_BUSY:       return "HYT00";  /* Timeout */
        case SQLITE_LOCKED:     return "HYT00";  /* Lock conflict */
        default:                return "HY000";  /* General error */
    }
}
```

**Note**: `SQLITE_ERROR` covers multiple cases (syntax errors, missing tables, etc.). More granular mapping would require parsing the error message text, which is fragile. Mapping `SQLITE_ERROR` → `42000` would be a good first step.

---

## Recommendation 6: Return HY092 for invalid connection attributes

**Severity**: WARNING  
**Tests affected**: `test_setconnattr_invalid_attr`  
**ODBC Spec**: ODBC 3.x §SQLSetConnectAttr — HY092 "Invalid attribute/option identifier"

### Current behavior

The `default` case in `drvsetconnectattr()` for unrecognized attributes returns `SQL_SUCCESS_WITH_INFO` with SQLSTATE `01S02` ("Option value changed"):

```c
default:
    setstatd(d, -1, "option value changed", "01S02");
    return SQL_SUCCESS_WITH_INFO;
```

This silently "accepts" any attribute value, including completely invalid attribute identifiers like 99999.

### Expected behavior

Unknown/invalid attribute identifiers should return `SQL_ERROR` with `HY092`:

```c
default:
    setstatd(d, -1, "Invalid attribute/option identifier", "HY092");
    return SQL_ERROR;
```

### Fix

The `default` case should distinguish between:
- **Valid but unsupported attributes** (e.g., `SQL_ATTR_CONNECTION_TIMEOUT`) → `SQL_ERROR` with `HYC00` ("Optional feature not implemented")
- **Invalid/unknown attributes** (e.g., attribute 99999) → `SQL_ERROR` with `HY092` ("Invalid attribute/option identifier")

At minimum, the driver should enumerate the attributes it recognizes (even if not all are supported) and return `HY092` for anything outside that set.

---

## Recommendation 7: Fix strmak truncation length reporting

**Severity**: INFO  
**Tests affected**: `test_string_truncation_wchar` (indirectly)  
**ODBC Spec**: ODBC 3.x §SQLGetInfo — StringLengthPtr on truncation

### Current behavior

The `strmak` macro reports the **truncated** length when the string doesn't fit in the buffer:

```c
#define strmak(dst, src, max, lenp) { \
    int len = strlen(src); \
    int cnt = min(len + 1, max); \
    strncpy(dst, src, cnt); \
    *lenp = (cnt > len) ? len : cnt; \  /* ← reports truncated length, not full */
}
```

### Expected behavior

Per the ODBC specification, when string truncation occurs, `*StringLengthPtr` (or `*pcbInfoValue` for `SQLGetInfo`) must report the **total available length** of the string (excluding the null terminator), not the truncated length. This allows the application to know how large a buffer is needed.

### Fix

```c
#define strmak(dst, src, max, lenp) { \
    int len = strlen(src); \
    int cnt = min(len + 1, max); \
    strncpy(dst, src, cnt); \
    *lenp = len;  /* Always report full length */ \
}
```

---

## Tests with Correct Behavior (No Action Needed)

| Test | Status | Why |
|------|--------|-----|
| `test_async_capability` | SKIP | Level 2 optional; SQLite is single-threaded embedded. No action needed. |
| `test_connection_timeout` | SKIP | Optional attribute; embedded DB has no network timeout concept. No action needed. |
| `test_columns_catalog` | SKIP | Empty test database — no tables to discover. Expected for `/tmp/test.db`. |
| `test_columns_unicode_patterns` | SKIP | Same — empty test database. |
| `test_string_truncation_wchar` | SKIP | All probed info types are too short OR the `strmak` bug prevents proper 01004 (see Rec. 7). |

---

*Generated by ODBC Crusher v0.4.1 analysis — February 8, 2026*
