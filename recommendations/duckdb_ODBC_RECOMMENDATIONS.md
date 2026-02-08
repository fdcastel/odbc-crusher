# DuckDB ODBC Driver — ODBC Crusher Analysis & Recommendations

**Date:** February 8, 2026
**Driver:** DuckDB ODBC 03.51.0000 (ODBC 3.51)
**Tool:** ODBC Crusher v0.3.1

---

## Summary

| Metric | Count |
|--------|-------|
| Total tests | 131 |
| Passed | 113 (86.3%) |
| Failed | 9 |
| Skipped | 8 |
| Errors | 1 |
| Total time | 99.73 ms |

The DuckDB ODBC driver passes the majority of ODBC conformance tests.
However, several issues were identified — two of which are **crash-severity
bugs** that can terminate the host process without any possibility of
recovery via SEH or signal handlers.

---

## Crash-Severity Issues (Process Termination)

### 1. `SQLDescribeParam` crashes on unresolved parameters

| Field | Value |
|-------|-------|
| **Test** | `test_describe_param` |
| **Function** | `SQLDescribeParam` |
| **Severity** | 🔴 CRITICAL — uncatchable process crash |
| **ODBC Spec** | ODBC 3.x `SQLDescribeParam` |
| **Exit Code** | `0xC0000005` (ACCESS_VIOLATION) |

**Reproduction:**

```c
SQLPrepare(hstmt, "SELECT ?", SQL_NTS);   // returns SQL_SUCCESS_WITH_INFO
SQLDescribeParam(hstmt, 1, ...);           // ACCESS VIOLATION
```

**Root Cause:**

In `src/odbc_api/parameter_api.cpp` → `SQLDescribeParam`, line ~37:

```cpp
auto param_type_id = hstmt->stmt->data->GetType(identifier).id();
```

When `SQLPrepare` on `"SELECT ?"` returns `SQL_SUCCESS_WITH_INFO`, the
statement's `bound_all_parameters` flag is `false`.  The code in
`FinalizeStmt` clears `hstmt->stmt->data->types` and
`hstmt->stmt->data->names` in this case.  `GetType(identifier)` then
accesses an empty vector at an out-of-bounds index → access violation.

The bounds check at line ~32 (`parameter_number > named_param_map.size()`)
passes because the map IS populated — only the `data->types` vector is
empty.

**Fix:**

Add a guard in `SQLDescribeParam` before accessing `data->GetType()`:

```cpp
if (!hstmt->stmt->data || identifier >= hstmt->stmt->data->types.size()) {
    return duckdb::SetDiagnosticRecord(hstmt, SQL_ERROR, "SQLDescribeParam",
        "Parameter type information not available",
        SQLStateType::ST_HY000, ...);
}
```

Or better: if `bound_all_parameters` is `false`, return `SQL_ERROR` with
`HY091` ("Invalid descriptor field identifier") since the parameter
metadata is unavailable.

**odbc-crusher workaround:** The tool now tries
`"SELECT CAST(? AS INTEGER)"` before `"SELECT ?"` so that DuckDB can
resolve the parameter type from the explicit cast.

---

### 2. Row-wise array binding corrupts memory → `__fastfail`

| Field | Value |
|-------|-------|
| **Test** | `test_row_wise_array_binding` |
| **Function** | `SQLSetStmtAttr` / `SQLExecute` |
| **Severity** | 🔴 CRITICAL — uncatchable process crash |
| **ODBC Spec** | ODBC 3.x "Binding Arrays of Parameters" |
| **Exit Code** | `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN) on MSVC |

**Reproduction:**

```c
struct ParamRow { SQLINTEGER id; SQLLEN ind; char name[51]; SQLLEN name_ind; };
ParamRow rows[3] = {{1,0,"A",SQL_NTS}, {2,0,"B",SQL_NTS}, {3,0,"C",SQL_NTS}};

SQLPrepare(hstmt, "INSERT INTO t (id, name) VALUES (?, ?)", SQL_NTS);
SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_BIND_TYPE, sizeof(ParamRow), 0);  // SQL_SUCCESS
SQLSetStmtAttr(hstmt, SQL_ATTR_PARAMSET_SIZE, 3, 0);
SQLBindParameter(hstmt, 1, ..., &rows[0].id, 0, &rows[0].ind);
SQLBindParameter(hstmt, 2, ..., rows[0].name, 51, &rows[0].name_ind);
SQLExecute(hstmt);  // __fastfail — UNCATCHABLE
```

**Root Cause:**

In `src/odbc_driver/parameter_descriptor.cpp` → `SetValue()`:

- The `SQL_ATTR_PARAM_BIND_TYPE` is stored in
  `apd->header.sql_desc_bind_type` (line 604 of `attribute_api.cpp`).

- But `SetValue()` **never uses** `sql_desc_bind_type` for pointer
  arithmetic.  For integer types it uses `Load<int32_t>(dataptr)` with
  the **same base pointer** for every row (no offset at all).  For string
  types it uses `(char*)sql_data_ptr + (val_idx * buff_size)` where
  `buff_size` is the **column size**, not the struct size.

- With row-wise binding, the correct offset for row `i` should be:
  `(char*)sql_data_ptr + (i * sql_desc_bind_type)`.

- Because the driver computes wrong offsets, it reads:
  - **Integers:** always from the same address (row 0's value repeated).
  - **Strings:** from `base + i * column_size` instead of
    `base + i * struct_size`, reading past the buffer into unrelated
    memory.  MSVC's `/GS` stack cookie detects this as a buffer overrun
    and calls `__fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE)`, which
    is an **uncatchable** exception on Windows 8+.

**Verified behavior:**

odbc-crusher's safety probe inserts with `PARAMSET_SIZE=2` and integer
parameters only (safe, since all reads come from offset 0 with no
arithmetic).  Result: `{9991, 9991}` instead of `{9991, 9992}` — the
second row gets row 0's value, confirming the offset bug.

**Fix:**

In `ParameterDescriptor::SetValue()`, use `sql_desc_bind_type` to
compute the correct data pointer for each row:

```cpp
auto bind_type = cur_apd->header.sql_desc_bind_type;
auto sql_data_ptr = GetSQLDescDataPtr(*apd_record);

if (bind_type != SQL_PARAM_BIND_BY_COLUMN && bind_type > 0) {
    // Row-wise: offset by row index * struct size
    sql_data_ptr = (SQLPOINTER)((char*)sql_data_ptr + val_idx * bind_type);
} else {
    // Column-wise: existing per-type offset logic
    ...
}
```

The same offset logic must be applied to the indicator pointer
(`sql_desc_indicator_ptr`).

**odbc-crusher workaround:** The tool probes with integer-only parameters
at `PARAMSET_SIZE=2` before attempting the full row-wise test.  If the
inserted values are wrong (duplicate row 0), it reports a `FAIL` with an
explanation and **skips the string-parameter test** to avoid crashing.

---

## Non-Crash Failures

### 3. `SQLEndTran(SQL_ROLLBACK)` fails in autocommit mode

| Field | Value |
|-------|-------|
| **Test** | `test_manual_rollback` |
| **Severity** | INFO |
| **SQLSTATE** | HY115 |

**Root Cause:**

`SQLEndTran` with `SQL_ROLLBACK` calls `dbc->conn->Rollback()` which
throws if no transaction is active.  The `SQL_COMMIT` branch already
checks `dbc->conn->IsAutoCommit()` and returns `SQL_SUCCESS` (no-op),
but the `SQL_ROLLBACK` branch does not.

**Fix:** Add the same autocommit guard:

```cpp
case SQL_ROLLBACK:
    if (dbc->conn->IsAutoCommit()) {
        return SQL_SUCCESS;  // no-op per ODBC spec
    }
    // ... existing rollback code
```

---

### 4. `SQLGetInfo` returns `SQL_SUCCESS` for invalid info types

| Field | Value |
|-------|-------|
| **Test** | `test_getinfo_invalid_type` |
| **Severity** | WARNING |
| **Expected SQLSTATE** | HY096 |

**Root Cause:**

The `default:` case in `SQLGetInfo` calls `SetDiagnosticRecord` with
`SQL_SUCCESS` instead of `SQL_ERROR`:

```cpp
default:
    return duckdb::SetDiagnosticRecord(dbc, SQL_SUCCESS, "SQLGetInfo", ...);
    //                                       ^^^^^^^^^^ should be SQL_ERROR
```

**Fix:** Change `SQL_SUCCESS` to `SQL_ERROR` and SQLSTATE to `HY096`.

---

### 5. `SQLSetConnectAttr` accepts invalid attributes silently

| Field | Value |
|-------|-------|
| **Test** | `test_setconnattr_invalid_attr` |
| **Severity** | WARNING |
| **Expected SQLSTATE** | HY092 |

**Root Cause:**

The `default:` case in `SQLSetConnectAttr` returns
`SQL_SUCCESS_WITH_INFO` with `01S02` for ALL unrecognized attributes.

**Fix:** Return `SQL_ERROR` with `HY092` ("Invalid attribute/option
identifier").

**Note:** The same pattern exists in `SQLSetStmtAttr`, where the
`default:` case also returns `SQL_SUCCESS_WITH_INFO` for unknown
attributes.  This caused test confusion with `SQL_ATTR_PARAM_BIND_TYPE`
(row-wise binding appeared accepted but was not implemented).

---

### 6. `SQLCloseCursor` succeeds when no cursor is open

| Field | Value |
|-------|-------|
| **Test** | `test_closecursor_no_cursor` |
| **Severity** | WARNING |
| **Expected SQLSTATE** | 24000 |

**Root Cause:**

`CloseStmt()` unconditionally calls `hstmt->Close()` and returns
`SQL_SUCCESS`, without checking whether a cursor is actually open.

**Fix:** Check the statement's `open` flag:

```cpp
SQLRETURN duckdb::CloseStmt(OdbcHandleStmt *hstmt) {
    if (!hstmt->open) {
        return SetDiagnosticRecord(hstmt, SQL_ERROR, "SQLCloseCursor",
            "Invalid cursor state", SQLStateType::ST_24000, ...);
    }
    hstmt->Close();
    return SQL_SUCCESS;
}
```

---

### 7. `SQLGetInfo` with zero-length buffer returns required length 0

| Field | Value |
|-------|-------|
| **Test** | `test_getinfo_zero_buffer` |
| **Severity** | WARNING |

**Root Cause:**

When `buffer_length=0` and a non-null `InfoValuePtr` is passed,
`OdbcUtils::WriteStringUtf8` computes `buf_len - 1` which underflows
to `SIZE_MAX`.  It then copies the full string without truncation and
returns `SQL_SUCCESS`, but the `string_length_ptr` output may not be
set correctly depending on the code path.

When `buffer_length=0` and `InfoValuePtr=NULL`, the function returns 0
written bytes and the string length should be set — but the edge case
causes the required length to appear as 0.

**Fix:** Handle `buffer_length == 0` as a special case in
`WriteStringUtf8`:

```cpp
if (buf_len == 0) {
    if (string_length) *string_length = str.length();
    return SQL_SUCCESS_WITH_INFO;  // indicates truncation
}
```

---

### 8. `SQLGetDiagField(SQL_DIAG_NUMBER)` returns 0 after errors

| Field | Value |
|-------|-------|
| **Test** | `test_diagfield_record_count` |
| **Severity** | INFO |

**Root Cause:**

`OdbcDiagnostic::AddDiagRecord()` adds records to `diag_records` but
**never increments** `header.sql_diag_number`.  The header field stays at
its initial value of `0`.  When `SQLGetDiagField(SQL_DIAG_NUMBER)` reads
it, it returns `0`.

**Fix:**

```cpp
void OdbcDiagnostic::AddDiagRecord(DiagRecord &diag_record) {
    diag_records.emplace_back(diag_record);
    vec_record_idx.emplace_back(static_cast<SQLSMALLINT>(diag_records.size() - 1));
    header.sql_diag_number = static_cast<SQLINTEGER>(diag_records.size());
}
```

---

### 9. `SQLGetData` with out-of-range column throws internal exception

| Field | Value |
|-------|-------|
| **Test** | `test_getdata_col_out_of_range` |
| **Severity** | INFO (should be ERROR) |
| **Expected SQLSTATE** | 07009 |

**Root Cause:**

`OdbcFetch::GetValue()` does not validate that `col_idx` is within the
result set's column count.  It calls
`current_chunk->GetValue(col_idx, chunk_row)` directly, which throws a
DuckDB internal C++ exception when `col_idx >= ColumnCount()`.

This exception escapes the ODBC layer and shows up as:
```
{"exception_type":"INTERNAL","exception_message":"Attempted to access
index 998 within vector of size 1"}
```

**Fix:** Add a bounds check before accessing the chunk:

```cpp
SQLRETURN OdbcFetch::GetValue(SQLUSMALLINT col_idx, Value &value) {
    if (!current_chunk || col_idx >= current_chunk->ColumnCount()) {
        return SQL_ERROR;  // caller sets SQLSTATE 07009
    }
    value = current_chunk->GetValue(col_idx, chunk_row);
    return SQL_SUCCESS;
}
```

---

### 10. `SQL_ATTR_PARAM_OPERATION_PTR` not implemented

| Field | Value |
|-------|-------|
| **Test** | `test_param_operation_array` |
| **Severity** | INFO |

**Root Cause:**

`SQLSetStmtAttr` has **no case** for `SQL_ATTR_PARAM_OPERATION_PTR`.
It falls through to the `default:` handler which returns
`SQL_SUCCESS_WITH_INFO` (see issue #5) without storing the pointer.

Even if the pointer were stored, `ParameterDescriptor::GetParamValues()`
never checks the operation array to skip rows marked `SQL_PARAM_IGNORE`.

**Fix:**

1. Add a case in `SQLSetStmtAttr`:
   ```cpp
   case SQL_ATTR_PARAM_OPERATION_PTR:
       hstmt->param_desc->GetAPD()->header.sql_desc_array_status_ptr =
           (SQLUSMALLINT*)value_ptr;
       return SQL_SUCCESS;
   ```

2. In `HasParamSetToProcess()` / `GetParamValues()`, skip rows where
   `operation_ptr[paramset_idx] == SQL_PARAM_IGNORE`:
   ```cpp
   if (operation_ptr && operation_ptr[paramset_idx] == SQL_PARAM_IGNORE) {
       ipd->header.sql_desc_array_status_ptr[paramset_idx] = SQL_PARAM_UNUSED;
       continue;  // skip this row
   }
   ```

---

### 11. `test_array_partial_error` — no mixed status on partial failures

| Field | Value |
|-------|-------|
| **Test** | `test_array_partial_error` |
| **Severity** | WARNING |

This test attempts to insert rows that should partially fail (e.g.,
duplicate primary keys).  DuckDB executes all rows successfully because
the table has no unique constraint enforcement in the ODBC path, or
all rows happen to succeed.

This is a consequence of issue #10 — without `SQL_ATTR_PARAM_OPERATION_PTR`
support, the mixed-status scenario cannot be properly tested.

---

## Skipped Tests (Not Bugs)

| Test | Reason |
|------|--------|
| `test_connection_timeout` | `SQL_ATTR_CONNECTION_TIMEOUT` not supported |
| `test_parameter_binding` | First query variant failed, fallback succeeded |
| `test_columns_catalog` | Non-standard result columns |
| `test_async_capability` | Async not supported |
| `test_apd_fields` | `SQLSetDescField` not supported (1 of 52 functions missing) |
| `test_columns_unicode_patterns` | Pattern matching differences |
| `test_string_truncation_wchar` | WCHAR truncation edge case |
| `test_native_sql` | `SQLNativeSql` returns SQL_ERROR |

These are expected limitations, not bugs.

---

## Recommendations by Priority

### 🔴 Must Fix (Process Crash)

1. **Guard `SQLDescribeParam`** against empty parameter type vectors
2. **Implement row-wise parameter binding arithmetic** in
   `ParameterDescriptor::SetValue()` using `sql_desc_bind_type`

### 🟠 Should Fix (Spec Violations)

3. Return `SQL_ERROR` from `SQLGetInfo` default case (not `SQL_SUCCESS`)
4. Return `SQL_ERROR` from `SQLSetConnectAttr` default case (not
   `SQL_SUCCESS_WITH_INFO`)
5. Return `SQL_ERROR` from `SQLSetStmtAttr` default case (not
   `SQL_SUCCESS_WITH_INFO`)
6. Return `SQL_ERROR` with `24000` from `SQLCloseCursor` when no cursor open
7. Increment `sql_diag_number` in `AddDiagRecord()`
8. Add bounds check in `OdbcFetch::GetValue()`
9. Handle `buffer_length=0` in `WriteStringUtf8`

### 🟡 Nice to Have

10. Add autocommit guard to `SQLEndTran(SQL_ROLLBACK)`
11. Implement `SQL_ATTR_PARAM_OPERATION_PTR`

---

## Test Environment

- **OS:** Windows 10/11 x64
- **Compiler:** MSVC 2022 (v17.14)
- **ODBC DM:** Microsoft ODBC Driver Manager 03.80
- **DuckDB ODBC:** v03.51.0000
- **Test tool:** ODBC Crusher v0.3.1

---

*Generated by ODBC Crusher — https://github.com/example/odbc-crusher*
