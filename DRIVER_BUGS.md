# ODBC Driver Bugs & Findings

**Project**: ODBC Crusher  
**Purpose**: Document bugs, inconsistencies, and issues found in ODBC drivers during testing

---

## Critical Finding #1: Firebird ODBC Driver - False "File Not Found" Error

**Date**: February 3, 2026  
**Driver**: Firebird ODBC Driver  
**Severity**: CRITICAL ⚠️  
**Status**: CONFIRMED

### Summary

The Firebird ODBC driver reports "[08004] File Database is not found (-902)" even when the database file exists and is accessible.

### Details

**Connection String**:
```
Driver={Firebird ODBC Driver};Database=/fbodbc-tests/TEST.FB50.FDB;UID=SYSDBA;PWD=masterkey;CHARSET=UTF8;CLIENT=/fbodbc-tests/fb502/fbclient.dll
```

**Evidence**:

1. **File Exists**: 
   ```
   C:\fbodbc-tests\TEST.FB50.FDB (1,835,008 bytes)
   ```

2. **pyodbc Error**:
   ```
   [08004] [08004] [ODBC Firebird Driver]File Database is not found (-902)
   ```

3. **DuckDB Connection SUCCESS**: 
   DuckDB successfully connects using the SAME connection string and queries system tables:
   ```sql
   SELECT * FROM mon$database
   -- Returns 1 row with 28 columns
   ```

### Root Cause Analysis

**Possible causes**:
1. File might be locked/in-use, but driver reports wrong error
2. Driver path handling issue (forward slashes vs backslashes)
3. Driver initialization timing issue
4. Error code mapping bug in driver

**Most Likely**: File-in-use condition misreported as "file not found"

### Impact

- **For Developers**: Cannot trust error messages from this driver
- **For Applications**: May implement wrong error handling
- **For Users**: Confusing error messages lead to wrong troubleshooting

### Reproduction

```python
import pyodbc
conn_str = "Driver={Firebird ODBC Driver};Database=/fbodbc-tests/TEST.FB50.FDB;UID=SYSDBA;PWD=masterkey;CHARSET=UTF8;CLIENT=/fbodbc-tests/fb502/fbclient.dll"

try:
    conn = pyodbc.connect(conn_str)
except pyodbc.Error as e:
    print(e)  # Shows "File Database is not found"
```

Meanwhile, the file exists and other ODBC clients can connect.

### Recommended Fixes

**For Driver Developers**:
1. Fix error code mapping in SQLConnect/SQLDriverConnect
2. Return correct SQLSTATE for file-in-use (HY008 or HY013)
3. Include actual error details in diagnostic message

**For Application Developers**:
1. Don't trust error messages at face value
2. Verify file existence independently before assuming driver is correct
3. Implement retry logic for file-in-use scenarios

### Lessons Learned

**Rule #1 of ODBC Driver Testing**: 
> **DO NOT TRUST ANYTHING THE ODBC DRIVER TELLS YOU. VERIFY INDEPENDENTLY.**

This finding validates the entire purpose of the ODBC Crusher project.

---

## Testing Methodology Notes

### Using DuckDB for Verification

DuckDB can be used as an independent verifier for ODBC connections:

```powershell
@'
INSTALL 'http://nightly-extensions.duckdb.org/v1.2.0/windows_amd64/odbc_scanner.duckdb_extension.gz';
LOAD odbc_scanner;
SET VARIABLE conn = odbc_connect('<connection-string>');
FROM odbc_query(getvariable('conn'), '<query>');
'@ | duckdb
```

**Note**: DuckDB uses the same ODBC driver being tested, so it can't catch driver bugs, but it can verify:
- Connection string syntax is correct
- File/server is accessible
- Credentials are valid
- Basic queries work

---

## Critical Finding #2: Firebird ODBC Driver - Parameter Binding Failures

**Date**: February 3, 2026  
**Driver**: Firebird ODBC Driver  
**Severity**: CRITICAL ⚠️  
**Status**: CONFIRMED

### Summary

The Firebird ODBC driver fails to execute prepared statements with parameter markers (`?`). Both `SQLPrepare/SQLExecute` and `SQLBindParameter` functionality appears to be broken or not properly implemented.

### Details

**Connection String**:
```
Driver={Firebird ODBC Driver};Database=/fbodbc-tests/TEST.FB50.FDB;UID=SYSDBA;PWD=masterkey;CHARSET=UTF8;CLIENT=/fbodbc-tests/fb502/fbclient.dll
```

**Test Results**:
- ✗ `test_prepared_statement` - FAILED
- ✗ `test_parameter_binding` - FAILED

**Queries Attempted**:
```sql
SELECT ? FROM RDB$DATABASE  -- Failed
SELECT ? FROM DUAL          -- Failed  
SELECT ?                    -- Failed
```

**All variants failed** when attempting to execute with a parameter value.

### Impact

- **For Developers**: Cannot use prepared statements or parameterized queries
- **For Applications**: Vulnerable to SQL injection (must use string concatenation)
- **For Performance**: No query plan reuse (each query is re-parsed)
- **For Security**: **CRITICAL SECURITY RISK** - forces unsafe query building

### Comparison

Other ODBC drivers (SQL Server, PostgreSQL, MySQL) properly support:
- Parameter markers (`?`)
- Prepared statement execution
- Parameter binding for all basic data types

Firebird ODBC driver does not.

### Recommended Fixes

**For Driver Developers**:
1. Implement proper `SQLPrepare` support
2. Support parameter markers (`?`) in SQL statements
3. Implement `SQLBindParameter` for all SQL data types
4. Test with standard ODBC test suites

**For Application Developers** (Workarounds):
1. ⚠️ **Use with extreme caution** - SQL injection risk!
2. Properly escape/sanitize all user input
3. Consider using stored procedures instead
4. Switch to a different ODBC driver if possible

### Security Note

**This is a CRITICAL security issue.** Applications using this driver cannot use parameterized queries, the standard defense against SQL injection attacks.

---

## Future Findings

Document additional bugs and findings below...
