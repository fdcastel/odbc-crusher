# Firebird ODBC Driver — Recommendations from ODBC Crusher Analysis

**Driver Tested**: Firebird ODBC Driver (Debug) v03.00.0021 (ODBC 3.51)  
**DBMS**: Firebird 5.0 (WI-V)  
**Analysis Date**: February 8, 2026  
**Test Tool**: ODBC Crusher v0.3.1

---

## Summary

ODBC Crusher ran 127 tests against the Firebird ODBC driver. Of the results:
- **100 passed** (78.7%)
- **1 failed** (Buffer truncation indicator)
- **1 error** (Descriptor crash — access violation)
- **25 skipped** — of which:
  - **14 are odbc-crusher bugs** (hardcoded table names not found in database)
  - **8 are odbc-crusher bugs** (test setup failures, not driver issues)
  - **2 are genuine driver limitations** (optional features: connection timeout, async)
  - **1 is a genuine driver issue** (SQLForeignKeys returning 0 rows for non-existent tables — debatable)

After investigation against the actual Firebird ODBC source code, only **3 genuine driver issues** were identified.

---

## 🔴 CRITICAL: Descriptor Operations Crash (Access Violation 0xC0000005)

### What Happened
The Descriptor Tests category caused an access violation, crashing the driver. The crash occurs during `SQLSetDescField(SQL_DESC_COUNT)` and/or `SQLCopyDesc` operations.

### Root Cause 1: `SQLSetDescField(SQL_DESC_COUNT)` doesn't allocate records
In `OdbcDesc.cpp`, the `SQL_DESC_COUNT` case in `sqlSetDescField()` only sets `headCount` without calling `allocRecords()` to allocate the backing `records` array. There is even a comment acknowledging this: `"If modify value realized ReAlloc FIXME!"`.

After calling `SQLSetDescField(SQL_DESC_COUNT, N)`, `headCount = N` but `records = NULL` and `recordsCount = 0`.

### Root Cause 2: `SQLCopyDesc` (operator=) doesn't null-check source records
In `OdbcDesc.cpp`, the `operator=` method iterates `sour.records[n]` without checking if the source's `records` array is NULL or if `recordsCount` matches `headCount`. When the source descriptor was created by just setting `SQL_DESC_COUNT` (without binding any columns), the `records` pointer is NULL, leading to a null pointer dereference.

### Recommendation

**Fix 1** — In `OdbcDesc::sqlSetDescField()`, for the `SQL_DESC_COUNT` case:
```cpp
case SQL_DESC_COUNT:
    headCount = (SQLSMALLINT)(intptr_t)value;
    if (headCount > 0)
        getDescRecord(headCount);  // Allocate records array
    break;
```

**Fix 2** — In `OdbcDesc::operator=()`, add null safety:
```cpp
for (int n = 0; n <= headCount; n++) {
    if (n >= sour.recordsCount || !sour.records)
        break;  // Source has no records allocated for this index
    DescRecord *srcrec = sour.records[n];
    if (!srcrec)
        continue;
    // ... existing copy logic
}
```

### Impact
- Any ODBC application that uses explicit descriptor manipulation (e.g., bulk copy tools, ORMs) may crash the driver.
- This violates the ODBC spec requirement that drivers should never crash — they should return SQL_ERROR with an appropriate SQLSTATE.

---

## 🟡 WARNING: SQLGetInfo String Truncation Length Indicator

### What Happened
When `SQLGetInfo` is called with a buffer too small to hold the result string, the driver returns `SQL_SUCCESS_WITH_INFO` with SQLSTATE `01004` (correct), but the `StringLengthPtr` output contains the **truncated length** instead of the **full required length**.

### Root Cause
In `OdbcObject.cpp`, the `returnStringInfo()` function has this code:

```cpp
// Initially correct:
*returnLength = count;           // Full string length

// But then overwritten on truncation:
if (count > maxLength) {
    memcpy(ptr, value, maxLength);
    ((char*)ptr)[maxLength] = 0;
    *returnLength = maxLength;   // BUG: overwrites with truncated length
}
```

### ODBC Spec Requirement
From the ODBC 3.8 specification for `SQLGetInfo`:
> *"If the character data is equal to or longer than BufferLength, the info in InfoValuePtr is truncated to BufferLength minus 1 bytes (for a null-termination character) and is null-terminated by the driver. StringLengthPtr contains the **total length in bytes** of the data available to return — not including the null-termination character."*

The length should indicate how large the buffer needs to be so the application can retry with a larger buffer.

### Recommendation
In `OdbcObject::returnStringInfo()`, remove the line that overwrites the length:
```cpp
if (count > maxLength) {
    memcpy(ptr, value, maxLength);
    ((char*)ptr)[maxLength] = 0;
    // Do NOT overwrite *returnLength — keep the full length
    // *returnLength = maxLength;  // REMOVE THIS LINE
}
```

**Note**: The `setString()` path (used by some info types) correctly preserves the full length. Only `returnStringInfo()` has this issue.

### Impact
- Applications that query info types in two passes (first with small buffer to get length, then with full buffer) will allocate buffers that are too small.
- This affects tools like database browsers and ORMs that enumerate driver capabilities.

---

## 🟢 OPTIONAL: Connection Timeout Attribute (SQL_ATTR_CONNECTION_TIMEOUT)

### What Happened
The driver does not support `SQL_ATTR_CONNECTION_TIMEOUT`. This attribute is **optional** per the ODBC spec.

### Recommendation
This is a low-priority enhancement. If desired, Firebird ODBC could implement `SQL_ATTR_CONNECTION_TIMEOUT` by setting the `isc_dpb_connect_timeout` parameter on the Firebird DPB (Database Parameter Block) during connection. This would allow applications to set connection timeouts without using driver-specific connection string parameters.

### Impact
- Minor — most applications use connection string parameters for timeouts.

---

## 🟢 OPTIONAL: Asynchronous Execution (SQL_ATTR_ASYNC_ENABLE)

### What Happened
The driver accepts `SQL_ATTR_ASYNC_ENABLE` without error but does not persist the setting. Asynchronous execution is **Level 2** (optional) in the ODBC conformance levels.

### Recommendation
No action needed. Asynchronous execution is complex to implement and rarely required by ODBC applications. The current behavior (accept but don't persist) is acceptable, though returning `SQL_ERROR` with `HYC00` (Optional feature not implemented) would be more correct.

### Impact
- None for typical applications.

---

## ℹ️ Notes on Other Skipped Tests

The remaining 22 skipped tests are caused by **bugs in ODBC Crusher** (the testing tool), not by Firebird ODBC driver issues. These are documented in ODBC Crusher's `PROJECT_PLAN.md` under Phase 18 for future fixes. The key issues are:

1. **14 tests** use hardcoded table names (`CUSTOMERS`, `USERS`) that don't exist in the target database
2. **3 tests** use non-Firebird SQL syntax (`N'...'` prefix, `NVARCHAR` type, `DUAL` table)
3. **5 tests** have test setup failures (DDL transaction handling, query fallback issues)

The Firebird ODBC driver **does support** all the features these tests attempt to verify (Unicode/SQL_C_WCHAR, array parameters, parameter binding, cursors, etc.), as confirmed by source code analysis.

---

*Generated by ODBC Crusher v0.3.1 with manual source code verification*
