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
    
    // Call SQLGetTypeInfo
    std::cout << "Calling SQLGetTypeInfo...\n";
    ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
    std::cout << "SQLGetTypeInfo returned: " << ret << "\n";
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
    SQLNumResultCols(hstmt, &numCols);
    std::cout << "Number of columns: " << numCols << "\n";
    
    // Fetch rows
    int rowCount = 0;
    while (SQL_SUCCEEDED(SQLFetch(hstmt))) {
        rowCount++;
        
        SQLCHAR typeName[128] = {0};
        SQLSMALLINT dataType = 0;
        SQLINTEGER columnSize = 0;
        SQLLEN indicator = 0;
        
        SQLGetData(hstmt, 1, SQL_C_CHAR, typeName, sizeof(typeName), &indicator);
        SQLGetData(hstmt, 2, SQL_C_SSHORT, &dataType, 0, NULL);
        SQLGetData(hstmt, 3, SQL_C_SLONG, &columnSize, 0, NULL);
        
        std::cout << "Row " << rowCount << ": " 
                  << typeName << " (type=" << dataType << ", size=" << columnSize << ")\n";
    }
    
    std::cout << "Total rows: " << rowCount << "\n";
    
    // Cleanup
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    SQLDisconnect(hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
    
    return 0;
}
