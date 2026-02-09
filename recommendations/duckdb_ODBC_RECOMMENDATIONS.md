# DuckDB ODBC Driver — Recommendations from ODBC Crusher Analysis

**Driver**: DuckDB v03.51.0000 (ODBC 3.51)  
**Database**: DuckDB (in-memory)  
**Test Environment**: Linux (unixODBC 3.52 DM)  
**Test Tool**: ODBC Crusher v0.4.5  
**Analysis Date**: February 8, 2026  
**Source Tag**: `v1.4.4.0`

## Summary

ODBC Crusher ran **131 tests** against the DuckDB ODBC driver.  
Of the 123 tests that completed (8 were lost to driver crashes):

| Result | Count | Percentage |
|--------|------:|------------|
| PASS   |   108 |     87.8%  |
| FAIL   |     7 |      5.7%  |
| SKIP   |     5 |      4.1%  |
| ERROR  |     3 |      2.4%  |

The DuckDB ODBC driver has **2 crash-severity bugs** (SIGSEGV in Descriptor and
Unicode test categories), **7 spec violations** of varying severity, and **5 unsupported
features**. All findings were verified against the driver source code at tag `v1.4.4.0`.

---

## Recommendation 1: Fix Descriptor API (SIGSEGV crash)

**Severity**: CRITICAL  
**Test affected**: Descriptor Tests (ERROR — SIGSEGV, all 5 tests lost)  
**ODBC Functions**: `SQLGetStmtAttr`, `SQLGetDescField`, `SQLSetDescField`, `SQLCopyDesc`  
**ODBC Spec**: ODBC 3.x §Descriptors  
**Conformance**: Core  

### Current behavior

The entire Descriptor test category crashes with a segmentation fault. The driver has
multiple bugs in its descriptor implementation:

1. **`stmt_ptr` is always null for implicit descriptors.** In the `OdbcHandleStmt`
   constructor, descriptors are created as `make_uniq<OdbcHandleDesc>(stmt->dbc)` —
   the `stmt_ptr` parameter defaults to `nullptr`. This causes `IsID()`, `IsIRD()`,
   and `IsIPD()` to always return `false`, so the driver cannot distinguish implicit
   from explicit descriptors or enforce read-only rules on the IRD.

2. **`SQLCopyDesc` does pointer assignment instead of object copy.** In `descriptor.cpp`,
   the copy operation does `target_desc = source_desc` which reassigns a local pointer
   variable rather than performing a deep copy of descriptor records.

3. **Record management appends instead of replacing.** The `SetDescField` function
   calls `Reset()` on individual records but then appends new records without clearing
   existing ones, leading to data corruption.

### Suggested fix

```cpp
// 1. Pass stmt_ptr when creating implicit descriptors in OdbcHandleStmt constructor:
ard = make_uniq<OdbcHandleDesc>(this->dbc, this);
ird = make_uniq<OdbcHandleDesc>(this->dbc, this);

// 2. Implement proper deep copy in SQLCopyDesc:
SQLRETURN SQL_API SQLCopyDesc(SQLHDESC source, SQLHDESC target) {
    auto *src = (OdbcHandleDesc *)source;
    auto *tgt = (OdbcHandleDesc *)target;
    tgt->header = src->header;
    tgt->records = src->records;  // deep copy records vector
    return SQL_SUCCESS;
}
```

### Impact

Critical. Applications that use descriptors (e.g., for efficient bulk operations,
cursor libraries, or ORM frameworks) will crash. This blocks all descriptor-based
functionality.

---

## Recommendation 2: Fix Unicode/Wide-character handling (SIGSEGV crash)

**Severity**: CRITICAL  
**Test affected**: Unicode Tests (ERROR — SIGSEGV, all 5 tests lost)  
**ODBC Functions**: `SQLGetInfoW`, `SQLDescribeColW`, `SQLGetData(SQL_C_WCHAR)`, `SQLColumnsW`  
**ODBC Spec**: ODBC 3.x §Unicode  
**Conformance**: Core  

### Current behavior

The entire Unicode test category crashes with a segmentation fault. The driver's
widechar layer (`src/widechar/`) hardcodes UTF-16 (2-byte) assumptions for `SQLWCHAR`,
but on Linux with unixODBC, `SQLWCHAR` may be 4 bytes (`wchar_t`) unless
`-DSQL_WCHART_CONVERT` is defined.

Key issues in the conversion functions:
- `utf16_conv()` uses `char16_t` iterators on data that may be `wchar_t` (4 bytes),
  causing misaligned reads and null-terminator detection failures.
- Buffer size calculations mix byte counts and character counts.
- `SqlWString::set_from_sqlwchar()` uses `char16_t` casting that is only correct
  when `sizeof(SQLWCHAR) == 2`.

### Suggested fix

The widechar layer must detect `sizeof(SQLWCHAR)` at compile time and use the
appropriate conversion:

```cpp
#if SIZEOF_SQLWCHAR == 4
    // UCS-4/UTF-32: wchar_t on Linux without SQL_WCHART_CONVERT
    // Convert from UTF-32 to UTF-8
#else
    // UTF-16: Windows, or Linux with -DSQL_WCHART_CONVERT
    // Current code path
#endif
```

Alternatively, define `-DSQL_WCHART_CONVERT` in the CMakeLists.txt to ensure
`SQLWCHAR` is always `char16_t` (2 bytes), matching the driver's expectations.

### Impact

Critical. Any application using W-functions (the default for Unicode-aware ODBC
applications on all platforms) will crash. This blocks Unicode support entirely
on Linux.

---

## Recommendation 3: Validate column index in `SQLGetData`

**Severity**: HIGH  
**Test affected**: `test_getdata_col_out_of_range` (ERROR — internal exception)  
**ODBC Function**: `SQLGetData`  
**ODBC Spec**: ODBC 3.x §SQLGetData — SQLSTATE 07009  
**Conformance**: Core  

### Current behavior

When `SQLGetData` is called with a column number greater than the number of columns
in the result set (e.g., column 999 on a 1-column result), the driver throws a
DuckDB internal exception:

```
Attempted to access index 998 within vector of size 1
```

This exception propagates as an unhandled error with a JSON stack trace instead of
the ODBC-standard `SQL_ERROR` with SQLSTATE `07009`.

### Root cause

In `odbc_fetch.cpp`, the `GetData` function decrements the column number
(`col_or_param_num--`) and passes it directly to the result fetcher without validating
that it falls within the result set's column count.

### Suggested fix

Add a bounds check before accessing the column data:

```cpp
SQLRETURN OdbcFetch::GetData(SQLUSMALLINT col_or_param_num, ...) {
    if (col_or_param_num == 0) {
        // Already handled — bookmark column
        return SQL_ERROR;  // SQLSTATE 07009
    }
    col_or_param_num--;
    if (col_or_param_num >= hstmt->stmt->ColumnCount()) {
        // Set SQLSTATE 07009 "Invalid descriptor index"
        return SetDiagnosticRecord(hstmt, SQL_ERROR, "SQLGetData",
            "Invalid column index", SQLStateType::ST_07009, "");
    }
    // ... proceed with data retrieval
}
```

### Impact

High. Any application that passes an incorrect column index will get an unstructured
exception message instead of a standard ODBC error. This breaks error-handling code
that expects `SQL_ERROR` and a valid SQLSTATE.

---

## Recommendation 4: Fix `SQLEndTran(SQL_ROLLBACK)` in manual-commit mode

**Severity**: HIGH  
**Test affected**: `test_manual_rollback` (FAIL)  
**ODBC Function**: `SQLEndTran(SQL_ROLLBACK)`  
**ODBC Spec**: ODBC 3.x §SQLEndTran  
**Conformance**: Core  

### Current behavior

After disabling autocommit, executing DML statements, and committing, a subsequent
`SQLEndTran(SQL_ROLLBACK)` fails with SQLSTATE `HY115`. The DuckDB transaction
context resets its `auto_commit` flag to `true` after `Commit()`, causing the next
statement to auto-commit. The subsequent `ROLLBACK` then finds no active transaction
and throws an error.

### Root cause

In DuckDB's transaction lifecycle (`transaction_context.cpp`), `Commit()` resets the
context such that subsequent statements run in auto-commit mode regardless of the
ODBC connection's `SQL_ATTR_AUTOCOMMIT` setting. The ODBC layer in `connection.cpp`
does not re-synchronize the transaction context after each commit.

### Suggested fix

After calling `connection->Commit()`, the ODBC layer should ensure the transaction
context respects the current `SQL_ATTR_AUTOCOMMIT` setting:

```cpp
case SQL_ROLLBACK: {
    if (dbc->conn->HasActiveTransaction()) {
        dbc->conn->Rollback();
    }
    // If autocommit is OFF, start a new transaction for the next statement
    if (!dbc->autocommit) {
        dbc->conn->BeginTransaction();
    }
    return SQL_SUCCESS;
}
```

### Impact

High. Applications using manual transaction control (autocommit OFF) cannot perform
rollbacks, which is a fundamental ODBC operation. This breaks any transactional
workflow that needs rollback capability.

---

## Recommendation 5: Fix `SQLBindParameter` with `nullptr` `StrLen_or_IndPtr`

**Severity**: HIGH  
**Test affected**: `test_parameter_binding` (SKIP — inconclusive)  
**ODBC Function**: `SQLBindParameter`  
**ODBC Spec**: ODBC 3.x §SQLBindParameter — `StrLen_or_IndPtr`  
**Conformance**: Core  

### Current behavior

When `SQLBindParameter` is called with `StrLen_or_IndPtr = nullptr` and a valid data
pointer (e.g., for a fixed-length `SQL_C_SLONG` parameter), the driver treats the
parameter as `NULL`. In `parameter_descriptor.cpp`, the `SetValue` function checks:

```cpp
if (sql_data_ptr == nullptr || sql_ind_ptr == nullptr || *sql_ind_ptr_val_set == SQL_NULL_DATA) {
    Value val_null(nullptr);
    SetValue(val_null, rec_idx);
    return SQL_SUCCESS;
}
```

The `sql_ind_ptr == nullptr` condition incorrectly treats a missing indicator as
"data is NULL".

### Expected behavior

Per the ODBC specification, when `StrLen_or_IndPtr` is a null pointer:
- For **fixed-length types** (SQL_C_SLONG, SQL_C_DOUBLE, etc.): the driver should
  use the data pointer directly — the data is always non-null.
- For **variable-length types** (SQL_C_CHAR, SQL_C_BINARY): the driver assumes the
  data is null-terminated or uses the buffer length.

A null `StrLen_or_IndPtr` only means "no indicator is provided" — it does NOT mean
the data is null.

### Suggested fix

```cpp
if (sql_data_ptr == nullptr && sql_ind_ptr == nullptr) {
    return SQL_ERROR;  // Both null — no data at all
}

// Check for explicit NULL via indicator
if (sql_ind_ptr != nullptr && *sql_ind_ptr_val_set == SQL_NULL_DATA) {
    Value val_null(nullptr);
    SetValue(val_null, rec_idx);
    return SQL_SUCCESS;
}

// If sql_data_ptr is null but indicator is not SQL_NULL_DATA, error
if (sql_data_ptr == nullptr) {
    return SQL_ERROR;
}

// Otherwise, proceed with the data — it's valid
```

### Impact

High. Many ODBC applications pass `nullptr` for `StrLen_or_IndPtr` when binding
fixed-length parameters, which is the most common pattern for integer/float bindings.
This causes all such parameters to be treated as NULL, producing incorrect query
results.

---

## Recommendation 6: Implement row-wise array parameter binding

**Severity**: MEDIUM  
**Test affected**: `test_row_wise_array_binding` (FAIL)  
**ODBC Function**: `SQLSetStmtAttr(SQL_ATTR_PARAM_BIND_TYPE)` / `SQLBindParameter`  
**ODBC Spec**: ODBC 3.x §Arrays of Parameter Values — Row-wise binding  
**Conformance**: Level 1  

### Current behavior

The driver stores `SQL_ATTR_PARAM_BIND_TYPE` when set via `SQLSetStmtAttr` but never
uses it during parameter value extraction. In `parameter_descriptor.cpp`, the
`SetValue` function:

- For **string types**: uses `val_idx * buff_size` (column-wise offset)
- For **numeric types**: uses no offset at all — always reads from the base pointer

A TODO comment in the code confirms this is known:
```cpp
// TODO need to check it param_value_ptr is an array of parameters
// and get the right parameter using the index
```

This causes row-wise array binding to insert duplicate values from row 0,
and executing with string parameters in a struct layout would cause memory
corruption and a process crash (as the driver reads from incorrect addresses).

### Suggested fix

When `SQL_ATTR_PARAM_BIND_TYPE != SQL_PARAM_BIND_BY_COLUMN`, use the bind type as
stride:

```cpp
duckdb::const_data_ptr_t dataptr;
if (apd->header.sql_desc_bind_type == SQL_PARAM_BIND_BY_COLUMN) {
    // Column-wise: offset by element size
    dataptr = (duckdb::const_data_ptr_t)sql_data_ptr + (val_idx * element_size);
} else {
    // Row-wise: offset by struct stride
    dataptr = (duckdb::const_data_ptr_t)sql_data_ptr + (val_idx * apd->header.sql_desc_bind_type);
}
```

### Impact

Medium. Applications using row-wise array binding (common in batch insert scenarios
with struct layouts) will get incorrect data. This is a Level 1 conformance feature.

---

## Recommendation 7: Implement `SQL_ATTR_PARAM_OPERATION_PTR`

**Severity**: MEDIUM  
**Test affected**: `test_param_operation_array` (FAIL), `test_array_partial_error` (FAIL)  
**ODBC Function**: `SQLSetStmtAttr(SQL_ATTR_PARAM_OPERATION_PTR)`  
**ODBC Spec**: ODBC 3.x §Using Arrays of Parameters — Parameter Operation Array  
**Conformance**: Level 1  

### Current behavior

The driver silently accepts `SQL_ATTR_PARAM_OPERATION_PTR` via the default handler
in `SQLSetStmtAttr` (which returns `SQL_SUCCESS_WITH_INFO` / `01S02` for any
unrecognized attribute) but never reads or honors the operation array during
execution. In `parameter_descriptor.cpp`, the execution loop iterates all parameter
sets unconditionally, without checking if any rows are marked `SQL_PARAM_IGNORE`.

### Expected behavior

Per the ODBC spec, during array parameter execution:
1. Check the operation array for each row index
2. Skip rows marked `SQL_PARAM_IGNORE`
3. Set the status of skipped rows to `SQL_PARAM_UNUSED`
4. Only execute rows marked `SQL_PARAM_PROCEED`

### Suggested fix

In the parameter execution loop in `parameter_descriptor.cpp`:

```cpp
for (idx_t i = 0; i < paramset_size; i++) {
    // Check operation array
    if (param_operation_ptr && param_operation_ptr[i] == SQL_PARAM_IGNORE) {
        if (param_status_ptr) {
            param_status_ptr[i] = SQL_PARAM_UNUSED;
        }
        continue;  // Skip this row
    }
    // Execute this row...
    auto ret = SetValue(rec_idx);
    if (param_status_ptr) {
        param_status_ptr[i] = SQL_SUCCEEDED(ret) ? SQL_PARAM_SUCCESS : SQL_PARAM_ERROR;
    }
}
```

### Impact

Medium. Applications that use operation arrays for selective batch execution (e.g.,
to skip rows that failed validation) will execute all rows regardless, potentially
causing data integrity issues.

---

## Recommendation 8: Return `SQL_ERROR` for invalid `SQLGetInfo` info types

**Severity**: LOW  
**Test affected**: `test_getinfo_invalid_type` (FAIL)  
**ODBC Function**: `SQLGetInfo`  
**ODBC Spec**: ODBC 3.x §SQLGetInfo — SQLSTATE HY096  
**Conformance**: Core  

### Current behavior

When `SQLGetInfo` is called with an unrecognized info type (e.g., 65535), the driver's
`default:` case in `api_info.cpp` returns `SQL_SUCCESS` with a diagnostic record
using SQLSTATE `HY092`:

```cpp
default:
    std::string msg = "Unrecognized attribute: " + std::to_string(info_type);
    return SetDiagnosticRecord(dbc, SQL_SUCCESS, "SQLGetInfo", msg,
                               SQLStateType::ST_HY092, dbc->GetDataSourceName());
```

### Expected behavior

Per the ODBC specification:
- Return code should be `SQL_ERROR` (not `SQL_SUCCESS`)
- SQLSTATE should be `HY096` ("Information type out of range"), not `HY092`

### Suggested fix

```cpp
default:
    std::string msg = "Information type out of range: " + std::to_string(info_type);
    return SetDiagnosticRecord(dbc, SQL_ERROR, "SQLGetInfo", msg,
                               SQLStateType::ST_HY096, dbc->GetDataSourceName());
```

### Impact

Low. Applications that query unsupported info types receive misleading success instead
of an error. Error-handling code that checks for `SQL_ERROR` will miss this condition.

---

## Recommendation 9: Return `SQL_ERROR` for invalid `SQLSetConnectAttr` attributes

**Severity**: LOW  
**Test affected**: `test_setconnattr_invalid_attr` (FAIL)  
**ODBC Function**: `SQLSetConnectAttr`  
**ODBC Spec**: ODBC 3.x §SQLSetConnectAttr — SQLSTATE HY092  
**Conformance**: Core  

### Current behavior

When `SQLSetConnectAttr` is called with an unrecognized attribute (e.g., 99999), the
`default:` case returns `SQL_SUCCESS_WITH_INFO` with SQLSTATE `01S02`:

```cpp
default:
    return SetDiagnosticRecord(dbc, SQL_SUCCESS_WITH_INFO, "SQLSetConnectAttr",
        "Option value changed:" + std::to_string(attribute),
        SQLStateType::ST_01S02, dbc->GetDataSourceName());
```

### Expected behavior

Per the ODBC specification, unrecognized attributes must return `SQL_ERROR` with
SQLSTATE `HY092` ("Invalid attribute/option identifier").

### Suggested fix

```cpp
default:
    return SetDiagnosticRecord(dbc, SQL_ERROR, "SQLSetConnectAttr",
        "Invalid attribute/option identifier: " + std::to_string(attribute),
        SQLStateType::ST_HY092, dbc->GetDataSourceName());
```

### Impact

Low. Applications cannot distinguish between valid and invalid attribute settings.
The `SQL_SUCCESS_WITH_INFO` return code is misleading — it implies the driver accepted
a substituted value, when in reality it ignored the call entirely.

---

## Recommendation 10: Report required buffer length in `SQLGetInfo` with zero-length buffer

**Severity**: LOW  
**Test affected**: `test_getinfo_zero_buffer` (FAIL)  
**ODBC Function**: `SQLGetInfo`  
**ODBC Spec**: ODBC 3.x §SQLGetInfo — Buffer Length  
**Conformance**: Core  

### Current behavior

When `SQLGetInfo` is called with `BufferLength = 0`, the driver's `WriteString`
template function in `odbc_utils.hpp` returns `written_chars = 0` in the output
length pointer:

```cpp
if (out_buf != nullptr) {
    written_chars = (INT_TYPE)snprintf((char *)out_buf, buf_len, "%s", s.c_str());
}
if (out_len != nullptr) {
    *out_len = written_chars;
}
```

When `out_buf` is non-null but `buf_len` is 0, `snprintf` returns the number of
characters that *would have been written*, which is correct. However, when `out_buf`
is null (the standard length-probing pattern), `written_chars` stays at 0 and the
caller never learns the required buffer size. Additionally, the calling code in
`api_info.cpp` never returns `SQL_SUCCESS_WITH_INFO` for truncation — it always
returns `SQL_SUCCESS`.

### Expected behavior

Per the ODBC specification:
1. When `BufferLength = 0` and `InfoValuePtr` is non-null: return
   `SQL_SUCCESS_WITH_INFO` with SQLSTATE `01004`, and set `*StringLengthPtr` to the
   total length of the data (excluding null terminator).
2. When `InfoValuePtr` is null: return `SQL_SUCCESS` and set `*StringLengthPtr` to
   the total length.

### Suggested fix

```cpp
template <typename INT_TYPE, typename CHAR_TYPE=SQLCHAR>
static SQLRETURN WriteString(const std::string &s, CHAR_TYPE *out_buf,
                             SQLLEN buf_len, INT_TYPE *out_len) {
    INT_TYPE total_len = (INT_TYPE)s.length();
    if (out_len) {
        *out_len = total_len;  // Always report full length
    }
    if (out_buf != nullptr && buf_len > 0) {
        snprintf((char *)out_buf, buf_len, "%s", s.c_str());
    }
    if (out_buf != nullptr && total_len >= buf_len) {
        return SQL_SUCCESS_WITH_INFO;  // Truncation occurred
    }
    return SQL_SUCCESS;
}
```

### Impact

Low. Applications that probe for required buffer sizes (the standard two-pass pattern:
first call with null buffer to get length, then allocate and call again) will get
incorrect lengths and may allocate zero-byte buffers.

---

## Recommendation 11: Implement `SQLNativeSql`

**Severity**: LOW (INFO)  
**Test affected**: `test_native_sql` (SKIP)  
**ODBC Function**: `SQLNativeSql`  
**ODBC Spec**: ODBC 3.x §SQLNativeSql  
**Conformance**: Core  

### Current behavior

`SQLNativeSql` is stubbed in `empty_stubs.cpp` and returns `SQL_ERROR` with SQLSTATE
`HYC00` ("Optional feature not implemented"). However, the driver reports it as
supported via `SQLGetFunctions`.

### Expected behavior

Per the ODBC specification, `SQLNativeSql` is a Core conformance function. A minimal
implementation for DuckDB (which uses standard SQL) would simply copy the input SQL
to the output buffer unchanged:

```cpp
SQLRETURN SQL_API SQLNativeSql(SQLHDBC hdbc, SQLCHAR *in_sql, SQLINTEGER in_len,
                               SQLCHAR *out_sql, SQLINTEGER buf_len, SQLINTEGER *out_len) {
    std::string sql(in_sql, in_len == SQL_NTS ? strlen((char*)in_sql) : in_len);
    if (out_len) *out_len = (SQLINTEGER)sql.length();
    if (out_sql && buf_len > 0) {
        snprintf((char*)out_sql, buf_len, "%s", sql.c_str());
    }
    return SQL_SUCCESS;
}
```

### Impact

Low. Most applications don't call `SQLNativeSql`, but reporting it as supported via
`SQLGetFunctions` while returning `HYC00` is misleading.

---

## Recommendation 12: Handle `SQL_ATTR_ASYNC_ENABLE` consistently

**Severity**: LOW (INFO)  
**Test affected**: `test_async_capability` (SKIP — inconclusive)  
**ODBC Function**: `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE)` / `SQLGetStmtAttr`  
**ODBC Spec**: ODBC 3.x §SQL_ATTR_ASYNC_ENABLE  
**Conformance**: Level 2 (optional)  

### Current behavior

The driver is inconsistent between set and get:
- **Set**: `SQL_ATTR_ASYNC_ENABLE` falls through to the `default:` case in
  `SQLSetStmtAttr`, which returns `SQL_SUCCESS_WITH_INFO` with `01S02`.
- **Get**: `SQL_ATTR_ASYNC_ENABLE` falls through to the error handler in
  `SQLGetStmtAttr`, which returns `SQL_ERROR` with "Unsupported attribute type".

DuckDB does not support async execution (confirmed by `SQLGetInfo(SQL_ASYNC_MODE)`
returning `SQL_AM_NONE` at the connection level).

### Suggested fix

Add an explicit case for `SQL_ATTR_ASYNC_ENABLE` in `SQLSetStmtAttr` that returns
`SQL_ERROR` with `HYC00`:

```cpp
case SQL_ATTR_ASYNC_ENABLE:
    return SetDiagnosticRecord(hstmt, SQL_ERROR, "SQLSetStmtAttr",
        "Asynchronous execution is not supported",
        SQLStateType::ST_HYC00, "");
```

### Impact

Minimal. Async execution is a Level 2 optional feature. The fix is for consistency —
both set and get should agree that the feature is unsupported.

---

## Recommendation 13: Support `SQL_ATTR_CONNECTION_TIMEOUT`

**Severity**: LOW (INFO)  
**Test affected**: `test_connection_timeout` (SKIP)  
**ODBC Function**: `SQLGetConnectAttr(SQL_ATTR_CONNECTION_TIMEOUT)`  
**ODBC Spec**: ODBC 3.x §SQLGetConnectAttr  
**Conformance**: Core (optional attribute)  

### Current behavior

`SQL_ATTR_CONNECTION_TIMEOUT` falls through to a block of unsupported attributes that
returns `SQL_NO_DATA` in `connection.cpp`. This means the attribute is not recognized.

### Expected behavior

Even if DuckDB doesn't enforce a connection timeout (as an embedded database, it has
no network timeouts), it should return a default value of 0 (meaning "no timeout")
rather than indicating the attribute is unrecognized:

```cpp
case SQL_ATTR_CONNECTION_TIMEOUT:
    *(SQLUINTEGER *)value_ptr = 0;  // No timeout
    return SQL_SUCCESS;
```

### Impact

Minimal. Applications that query the connection timeout get an unexpected error
instead of the standard default value of 0.

---

## Overall Assessment

The DuckDB ODBC driver (v1.4.4.0) has significant issues that need attention:

**Critical (crash-severity):**
- ❌ Descriptor API crashes with SIGSEGV (Recommendation 1)
- ❌ Unicode/W-functions crash with SIGSEGV on Linux (Recommendation 2)

**High (incorrect behavior):**
- ❌ `SQLGetData` with out-of-range column throws exception (Recommendation 3)
- ❌ `SQLEndTran(ROLLBACK)` fails in manual-commit mode (Recommendation 4)
- ❌ `SQLBindParameter` with nullptr indicator treats data as NULL (Recommendation 5)
- ❌ Row-wise array binding ignores struct stride (Recommendation 6)
- ❌ `SQL_ATTR_PARAM_OPERATION_PTR` silently ignored (Recommendation 7)

**Low (spec conformance):**
- ⚠️ Invalid `SQLGetInfo` types return `SQL_SUCCESS` (Recommendation 8)
- ⚠️ Invalid `SQLSetConnectAttr` attributes return `SQL_SUCCESS_WITH_INFO` (Recommendation 9)
- ⚠️ Zero-buffer `SQLGetInfo` reports length=0 (Recommendation 10)
- ℹ️ `SQLNativeSql` stubbed but reported as supported (Recommendation 11)
- ℹ️ Async enable set/get inconsistency (Recommendation 12)
- ℹ️ Connection timeout not recognized (Recommendation 13)

**What works well:**
- ✅ Core statement lifecycle (prepare, execute, fetch, close)
- ✅ Column-wise array parameter binding with status arrays
- ✅ Data type handling (all 9 type categories pass, including GUID)
- ✅ Scrollable cursors (static, forward-only, absolute positioning)
- ✅ Error queue management (6/6 tests pass)
- ✅ State machine validation (6/6 tests pass)
- ✅ Cancellation support
- ✅ Boundary value handling (except zero-buffer)

---
Generated by ODBC Crusher -- https://github.com/fdcastel/odbc-crusher/
