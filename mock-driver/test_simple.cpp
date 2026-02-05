#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <iostream>
#include <iomanip>

int main() {
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    SQLRETURN ret;
    
    // Allocate environment
    ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "Failed to allocate environment\n";
        return 1;
    }
    
    // Set ODBC version
    SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    
    // Allocate connection
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "Failed to allocate connection\n";
        return 1;
    }
    
    // Connect
    const char* conn_str = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=100;";
    ret = SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn_str, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "Failed to connect. Return code: " << ret << "\n";
        
        SQLCHAR state[6], msg[256];
        SQLINTEGER native;
        SQLSMALLINT msgLen;
        SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1, state, &native, msg, sizeof(msg), &msgLen);
        std::cerr << "SQLSTATE: " << state << ", Message: " << msg << "\n";
        return 1;
    }
    
    std::cout << "Connected successfully!\n";
    
    // Allocate statement
    ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "Failed to allocate statement\n";
        return 1;
    }
    
    std::cout << "Statement allocated successfully! hstmt = " << hstmt << "\n";
    
    // Verify the handle is not null
    if (hstmt == SQL_NULL_HSTMT) {
        std::cerr << "Statement handle is NULL!\n";
        return 1;
    }
    
    std::cout << "Statement handle is valid (non-null)\n";
    
    // Try getting statement attributes first
    std::cout << "Trying SQLGetStmtAttr...\n";
    SQLULEN cursorType = 0;
    ret = SQLGetStmtAttr(hstmt, SQL_ATTR_CURSOR_TYPE, &cursorType, 0, NULL);
    if (SQL_SUCCEEDED(ret)) {
        std::cout << "SQLGetStmtAttr succeeded! Cursor type: " << cursorType << "\n";
    } else {
        std::cout << "SQLGetStmtAttr failed: " << ret << "\n";
    }
    
    // Try a simple query first to see if basic functions work
    std::cout << "Trying SQLExecDirect with a SELECT...\n";
    const char* query = "SELECT * FROM CUSTOMERS";
    ret = SQLExecDirect(hstmt, (SQLCHAR*)query, SQL_NTS);
    if (SQL_SUCCEEDED(ret)) {
        std::cout << "SQLExecDirect succeeded!\n";
    } else {
        std::cout << "SQLExecDirect failed (expected for now): " << ret << "\n";
    }
    
    // Now try SQLGetTypeInfo - but let's call it via the ODBC  manager
    std::cout << "\nNow testing SQLGetTypeInfo...\n";
    
    // Close previous statement
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    
    // Allocate new statement for type info
    ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "Failed to allocate statement for type info\n";
        return 1;
    }
    
    // Call SQLGetTypeInfo
    std::cout << "Calling SQLGetTypeInfo...\n";
    try {
        ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
        std::cout << "SQLGetTypeInfo returned: " << ret << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception in SQLGetTypeInfo: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "After SQLGetTypeInfo call\n";
    
    if (!SQL_SUCCEEDED(ret)) {
        std::cerr << "SQLGetTypeInfo failed. Return code: " << ret << "\n";
        
        SQLCHAR state[6], msg[256];
        SQLINTEGER native;
        SQLSMALLINT msgLen;
        ret = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1, state, &native, msg, sizeof(msg), &msgLen);
        if (SQL_SUCCEEDED(ret)) {
            std::cerr << "SQLSTATE: " << state << ", Message: " << msg << "\n";
        }
        return 1;
    }
    
    std::cout << "SQLGetTypeInfo succeeded!\n";
    
    // Get number of columns
    SQLSMALLINT numCols = 0;
    std::cout << "Calling SQLNumResultCols...\n";
    ret = SQLNumResultCols(hstmt, &numCols);
    std::cout << "SQLNumResultCols returned: " << ret << ", numCols=" << numCols << "\n";
    
    try {
        // Fetch rows
        int rowCount = 0;
        std::cout << "Starting to fetch rows...\n";
        while (SQL_SUCCEEDED(ret = SQLFetch(hstmt))) {
            rowCount++;
            std::cout << "Fetched row " << rowCount << "\n";
            
            SQLCHAR typeName[128] = {0};
            SQLSMALLINT dataType = 0;
            SQLINTEGER columnSize = 0;
            SQLLEN indicator = 0;
            
            std::cout << "  Getting column 1 (TYPE_NAME)...\n";
            ret = SQLGetData(hstmt, 1, SQL_C_CHAR, typeName, sizeof(typeName), &indicator);
            if (!SQL_SUCCEEDED(ret)) {
                std::cerr << "  SQLGetData column 1 failed: " << ret << "\n";
                break;
            }
            
            std::cout << "  Getting column 2 (DATA_TYPE)...\n";
            ret = SQLGetData(hstmt, 2, SQL_C_SSHORT, &dataType, 0, NULL);
            if (!SQL_SUCCEEDED(ret)) {
                std::cerr << "  SQLGetData column 2 failed: " << ret << "\n";
                break;
            }
            
            std::cout << "  Getting column 3 (COLUMN_SIZE)...\n";
            ret = SQLGetData(hstmt, 3, SQL_C_SLONG, &columnSize, 0, NULL);
            if (!SQL_SUCCEEDED(ret)) {
                std::cerr << "  SQLGetData column 3 failed: " << ret << "\n";
                break;
            }
            
            std::cout << "Row " << rowCount << ": " 
                      << typeName << " (type=" << dataType << ", size=" << columnSize << ")\n";
        }
        
        std::cout << "Total rows: " << rowCount << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception during fetch: " << e.what() << "\n";
    }
    
    // Cleanup
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    SQLDisconnect(hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
    
    return 0;
}
