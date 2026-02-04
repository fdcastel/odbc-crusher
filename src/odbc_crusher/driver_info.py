"""Driver information collection using ODBC functions."""

import pyodbc
from typing import Dict, List, Any, Optional
from enum import Enum
import ctypes
import sys
import struct


class DriverInfo:
    """Collects comprehensive driver information using SQLGetInfo, SQLGetFunctions, and SQLGetTypeInfo."""
    
    def __init__(self, connection_string: str):
        self.connection_string = connection_string
        self.info: Dict[str, Any] = {}
        self.functions: Dict[str, bool] = {}
        self.datatypes: List[Dict[str, Any]] = []
        
    def collect_all(self) -> Dict[str, Any]:
        """Collect all driver information."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            self._collect_sqlgetinfo(conn)
            self._collect_sqlgetfunctions(conn)
            self._collect_sqlgettypeinfo(conn)
            
            conn.close()
            
            return {
                'info': self.info,
                'functions': self.functions,
                'datatypes': self.datatypes,
            }
        except Exception as e:
            return {
                'error': str(e),
                'info': self.info,
                'functions': self.functions,
                'datatypes': self.datatypes,
            }
    
    def _collect_sqlgetinfo(self, conn: pyodbc.Connection):
        """Collect information using SQLGetInfo."""
        
        # Only use info types that actually exist in pyodbc
        # We check hasattr to avoid AttributeError
        info_types = {}
        
        # Try to add each info type, but only if it exists in pyodbc
        possible_info_types = {
            # Driver Information
            'DRIVER_NAME': 'SQL_DRIVER_NAME',
            'DRIVER_VER': 'SQL_DRIVER_VER',
            'DRIVER_ODBC_VER': 'SQL_DRIVER_ODBC_VER',
            
            # DBMS Information
            'DBMS_NAME': 'SQL_DBMS_NAME',
            'DBMS_VER': 'SQL_DBMS_VER',
            'DATABASE_NAME': 'SQL_DATABASE_NAME',
            'SERVER_NAME': 'SQL_SERVER_NAME',
            'DATA_SOURCE_NAME': 'SQL_DATA_SOURCE_NAME',
            'USER_NAME': 'SQL_USER_NAME',
            
            # SQL Support
            'SQL_CONFORMANCE': 'SQL_SQL_CONFORMANCE',
            'ODBC_VER': 'SQL_ODBC_VER',
            'ODBC_INTERFACE_CONFORMANCE': 'SQL_ODBC_INTERFACE_CONFORMANCE',
            
            # Catalog/Schema Support
            'CATALOG_NAME': 'SQL_CATALOG_NAME',
            'CATALOG_TERM': 'SQL_CATALOG_TERM',
            'SCHEMA_TERM': 'SQL_SCHEMA_TERM',
            'TABLE_TERM': 'SQL_TABLE_TERM',
            'PROCEDURE_TERM': 'SQL_PROCEDURE_TERM',
            'CATALOG_NAME_SEPARATOR': 'SQL_CATALOG_NAME_SEPARATOR',
            'CATALOG_LOCATION': 'SQL_CATALOG_LOCATION',
            'CATALOG_USAGE': 'SQL_CATALOG_USAGE',
            'SCHEMA_USAGE': 'SQL_SCHEMA_USAGE',
            
            # Identifiers
            'IDENTIFIER_QUOTE_CHAR': 'SQL_IDENTIFIER_QUOTE_CHAR',
            'IDENTIFIER_CASE': 'SQL_IDENTIFIER_CASE',
            'QUOTED_IDENTIFIER_CASE': 'SQL_QUOTED_IDENTIFIER_CASE',
            
            # Limits
            'MAX_COLUMN_NAME_LEN': 'SQL_MAX_COLUMN_NAME_LEN',
            'MAX_TABLE_NAME_LEN': 'SQL_MAX_TABLE_NAME_LEN',
            'MAX_COLUMNS_IN_SELECT': 'SQL_MAX_COLUMNS_IN_SELECT',
            'MAX_COLUMNS_IN_TABLE': 'SQL_MAX_COLUMNS_IN_TABLE',
            'MAX_STATEMENT_LEN': 'SQL_MAX_STATEMENT_LEN',
            'MAX_TABLES_IN_SELECT': 'SQL_MAX_TABLES_IN_SELECT',
            'MAX_IDENTIFIER_LEN': 'SQL_MAX_IDENTIFIER_LEN',
            
            # Transaction Support
            'TXN_CAPABLE': 'SQL_TXN_CAPABLE',
            'DEFAULT_TXN_ISOLATION': 'SQL_DEFAULT_TXN_ISOLATION',
            'MULTIPLE_ACTIVE_TXN': 'SQL_MULTIPLE_ACTIVE_TXN',
            'TXN_ISOLATION_OPTION': 'SQL_TXN_ISOLATION_OPTION',
            
            # Features
            'PROCEDURES': 'SQL_PROCEDURES',
            'MULT_RESULT_SETS': 'SQL_MULT_RESULT_SETS',
            'ACCESSIBLE_TABLES': 'SQL_ACCESSIBLE_TABLES',
            'ACCESSIBLE_PROCEDURES': 'SQL_ACCESSIBLE_PROCEDURES',
            'NULL_COLLATION': 'SQL_NULL_COLLATION',
            'OJ_CAPABILITIES': 'SQL_OJ_CAPABILITIES',
            'FILE_USAGE': 'SQL_FILE_USAGE',
            'GROUP_BY': 'SQL_GROUP_BY',
            
            # Cursor Support
            'CURSOR_COMMIT_BEHAVIOR': 'SQL_CURSOR_COMMIT_BEHAVIOR',
            'CURSOR_ROLLBACK_BEHAVIOR': 'SQL_CURSOR_ROLLBACK_BEHAVIOR',
            
            # Data Source Characteristics
            'DATA_SOURCE_READ_ONLY': 'SQL_DATA_SOURCE_READ_ONLY',
            
            # Character Set
            'COLLATION_SEQ': 'SQL_COLLATION_SEQ',
            
            # Additional useful info
            'KEYWORDS': 'SQL_KEYWORDS',
            'SPECIAL_CHARACTERS': 'SQL_SPECIAL_CHARACTERS',
        }
        
        # Build dict of only available constants
        for name, const_name in possible_info_types.items():
            if hasattr(pyodbc, const_name):
                info_types[name] = getattr(pyodbc, const_name)
        
        # Collect info for each available type
        for name, info_type in info_types.items():
            try:
                value = conn.getinfo(info_type)
                self.info[name] = value
            except Exception as e:
                # Some info types may not be supported by the driver
                self.info[name] = None
    
    def _collect_sqlgetfunctions(self, conn: pyodbc.Connection):
        """Collect supported functions using the ACTUAL SQLGetFunctions ODBC API."""
        
        # Call the real SQLGetFunctions from ODBC Driver Manager
        if sys.platform == 'win32':
            try:
                # Load ODBC32.DLL (the ODBC Driver Manager on Windows)
                odbc32 = ctypes.windll.odbc32
                
                # PyODBC doesn't expose the connection handle directly
                # We need to use SQLAllocHandle to get our own connection
                # OR use the ctypes.pythonapi to extract the handle from the pyodbc object
                
                # Alternative: Get handle via internal structure
                # This is hacky but works: pyodbc Connection objects have a hdbc member
                # We can access it via ctypes
                
                # Get the id/address of the connection object
                conn_id = id(conn)
                
                # Try to get handle from connection string reconstruction
                # Actually, let's use SQLDriverConnect to get a new handle for querying
                
                SQL_HANDLE_ENV = 1
                SQL_HANDLE_DBC = 2
                SQL_SUCCESS = 0
                SQL_NULL_HANDLE = 0
                SQL_OV_ODBC3 = 3
                SQL_ATTR_ODBC_VERSION = 200
                
                # Allocate environment handle
                henv = ctypes.c_void_p()
                ret = odbc32.SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, ctypes.byref(henv))
                if ret != SQL_SUCCESS:
                    self.functions['_error'] = f'Failed to allocate environment handle: {ret}'
                    return
                
                # Set ODBC version
                ret = odbc32.SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, SQL_OV_ODBC3, 0)
                if ret != SQL_SUCCESS:
                    odbc32.SQLFreeHandle(SQL_HANDLE_ENV, henv)
                    self.functions['_error'] = f'Failed to set ODBC version: {ret}'
                    return
                
                # Allocate connection handle
                hdbc = ctypes.c_void_p()
                ret = odbc32.SQLAllocHandle(SQL_HANDLE_DBC, henv, ctypes.byref(hdbc))
                if ret != SQL_SUCCESS:
                    odbc32.SQLFreeHandle(SQL_HANDLE_ENV, henv)
                    self.functions['_error'] = f'Failed to allocate connection handle: {ret}'
                    return
                
                # Connect using the connection string
                conn_str = self.connection_string
                out_conn_str = ctypes.create_string_buffer(1024)
                out_conn_str_len = ctypes.c_short()
                
                ret = odbc32.SQLDriverConnectA(
                    hdbc,
                    0,  # No window handle
                    conn_str.encode('utf-8'),
                    len(conn_str),
                    out_conn_str,
                    1024,
                    ctypes.byref(out_conn_str_len),
                    0  # SQL_DRIVER_NOPROMPT
                )
                
                if ret not in [SQL_SUCCESS, 1]:  # SQL_SUCCESS or SQL_SUCCESS_WITH_INFO
                    odbc32.SQLFreeHandle(SQL_HANDLE_DBC, hdbc)
                    odbc32.SQLFreeHandle(SQL_HANDLE_ENV, henv)
                    self.functions['_error'] = f'Failed to connect for SQLGetFunctions: {ret}'
                    return
                
                # Now we have a valid connection handle!
                # SQL_API_ODBC3_ALL_FUNCTIONS = 999
                # SQL_API_ODBC3_ALL_FUNCTIONS_SIZE = 250 (creates 4000-bit bitmap)
                SQL_API_ODBC3_ALL_FUNCTIONS = 999
                SQL_API_ODBC3_ALL_FUNCTIONS_SIZE = 250
                
                # Create a buffer for the function support bitmap
                # SQLUSMALLINT array[250]
                supported = (ctypes.c_ushort * SQL_API_ODBC3_ALL_FUNCTIONS_SIZE)()
                
                # Call SQLGetFunctions
                # SQLRETURN SQLGetFunctions(SQLHDBC hdbc, SQLUSMALLINT fFunction, SQLUSMALLINT *pfExists)
                result = odbc32.SQLGetFunctions(
                    hdbc,
                    SQL_API_ODBC3_ALL_FUNCTIONS,
                    ctypes.byref(supported)
                )
                
                if result == 0:  # SQL_SUCCESS
                    # Parse the bitmap to find supported functions
                    # Use SQL_FUNC_EXISTS macro: ((pfExists[(f) >> 4] & (1 << ((f) & 0x000F))) != 0)
                    
                    # Map of important ODBC function IDs to names
                    odbc_functions = {
                        # Core functions
                        1: 'SQLAllocHandle',
                        2: 'SQLBindCol',
                        3: 'SQLCancel',
                        4: 'SQLCloseCursor',
                        5: 'SQLColAttribute',
                        6: 'SQLConnect',
                        7: 'SQLCopyDesc',
                        8: 'SQLDataSources',
                        9: 'SQLDescribeCol',
                        10: 'SQLDisconnect',
                        11: 'SQLEndTran',
                        12: 'SQLExecDirect',
                        13: 'SQLExecute',
                        14: 'SQLFetch',
                        15: 'SQLFetchScroll',
                        16: 'SQLFreeHandle',
                        17: 'SQLFreeStmt',
                        18: 'SQLGetConnectAttr',
                        19: 'SQLGetCursorName',
                        20: 'SQLGetData',
                        21: 'SQLGetDescField',
                        22: 'SQLGetDescRec',
                        23: 'SQLGetDiagField',
                        24: 'SQLGetDiagRec',
                        25: 'SQLGetEnvAttr',
                        26: 'SQLGetFunctions',
                        27: 'SQLGetInfo',
                        28: 'SQLGetStmtAttr',
                        29: 'SQLGetTypeInfo',
                        30: 'SQLNumResultCols',
                        31: 'SQLParamData',
                        32: 'SQLPrepare',
                        33: 'SQLPutData',
                        34: 'SQLRowCount',
                        35: 'SQLSetConnectAttr',
                        36: 'SQLSetCursorName',
                        37: 'SQLSetDescField',
                        38: 'SQLSetDescRec',
                        39: 'SQLSetEnvAttr',
                        40: 'SQLSetStmtAttr',
                        
                        # Catalog functions
                        41: 'SQLColumns',
                        42: 'SQLSpecialColumns',
                        43: 'SQLStatistics',
                        44: 'SQLTables',
                        
                        # Extended functions
                        51: 'SQLBindParameter',
                        52: 'SQLBrowseConnect',
                        53: 'SQLBulkOperations',
                        54: 'SQLColAttributes',
                        55: 'SQLColumnPrivileges',
                        56: 'SQLDescribeParam',
                        57: 'SQLDriverConnect',
                        58: 'SQLDrivers',
                        59: 'SQLExtendedFetch',
                        60: 'SQLForeignKeys',
                        61: 'SQLMoreResults',
                        62: 'SQLNativeSql',
                        63: 'SQLNumParams',
                        64: 'SQLParamOptions',
                        65: 'SQLPrimaryKeys',
                        66: 'SQLProcedureColumns',
                        67: 'SQLProcedures',
                        68: 'SQLSetPos',
                        69: 'SQLSetScrollOptions',
                        70: 'SQLTablePrivileges',
                    }
                    
                    # Check each function using SQL_FUNC_EXISTS macro
                    for func_id, func_name in odbc_functions.items():
                        # SQL_FUNC_EXISTS: ((supported[func_id >> 4] & (1 << (func_id & 0x000F))) != 0)
                        word_index = func_id >> 4  # Divide by 16
                        bit_position = func_id & 0x000F  # Modulo 16
                        
                        if word_index < len(supported):
                            is_supported = (supported[word_index] & (1 << bit_position)) != 0
                            self.functions[func_name] = is_supported
                    
                    # Store summary
                    supported_count = sum(1 for v in self.functions.values() if v)
                    self.functions['_summary'] = f'{supported_count}/{len(odbc_functions)} ODBC functions supported'
                else:
                    self.functions['_error'] = f'SQLGetFunctions returned {result}'
                
                # Clean up
                odbc32.SQLDisconnect(hdbc)
                odbc32.SQLFreeHandle(SQL_HANDLE_DBC, hdbc)
                odbc32.SQLFreeHandle(SQL_HANDLE_ENV, henv)
                    
            except Exception as e:
                self.functions['_error'] = f'Failed to call SQLGetFunctions: {str(e)}'
        else:
            self.functions['_error'] = 'SQLGetFunctions only implemented for Windows'
        
    def _collect_sqlgettypeinfo(self, conn: pyodbc.Connection):
        """Collect supported data types using SQLGetTypeInfo."""
        
        try:
            cursor = conn.cursor()
            
            # Call SQLGetTypeInfo with SQL_ALL_TYPES to get all supported types
            # In pyodbc, this is cursor.getTypeInfo()
            if hasattr(cursor, 'getTypeInfo'):
                try:
                    # Get all data types
                    if hasattr(pyodbc, 'SQL_ALL_TYPES'):
                        rows = cursor.getTypeInfo(pyodbc.SQL_ALL_TYPES)
                    else:
                        # SQL_ALL_TYPES = 0
                        rows = cursor.getTypeInfo(0)
                    
                    for row in rows:
                        # Safely extract row data
                        datatype = {}
                        
                        # Column indices for SQLGetTypeInfo result set (as per ODBC spec)
                        try:
                            datatype['TYPE_NAME'] = row[0] if len(row) > 0 else None
                            datatype['DATA_TYPE'] = row[1] if len(row) > 1 else None
                            datatype['COLUMN_SIZE'] = row[2] if len(row) > 2 else None
                            datatype['LITERAL_PREFIX'] = row[3] if len(row) > 3 else None
                            datatype['LITERAL_SUFFIX'] = row[4] if len(row) > 4 else None
                            datatype['CREATE_PARAMS'] = row[5] if len(row) > 5 else None
                            datatype['NULLABLE'] = row[6] if len(row) > 6 else None
                            datatype['CASE_SENSITIVE'] = row[7] if len(row) > 7 else None
                            datatype['SEARCHABLE'] = row[8] if len(row) > 8 else None
                            datatype['UNSIGNED_ATTRIBUTE'] = row[9] if len(row) > 9 else None
                            datatype['FIXED_PREC_SCALE'] = row[10] if len(row) > 10 else None
                            datatype['AUTO_UNIQUE_VALUE'] = row[11] if len(row) > 11 else None
                            datatype['LOCAL_TYPE_NAME'] = row[12] if len(row) > 12 else None
                            datatype['MINIMUM_SCALE'] = row[13] if len(row) > 13 else None
                            datatype['MAXIMUM_SCALE'] = row[14] if len(row) > 14 else None
                        except (IndexError, TypeError):
                            # Some drivers may return fewer columns
                            pass
                        
                        self.datatypes.append(datatype)
                        
                except Exception as e:
                    self.datatypes.append({'error': f'getTypeInfo failed: {str(e)}'})
            else:
                self.datatypes.append({'error': 'cursor.getTypeInfo not available in pyodbc'})
            
            cursor.close()
        except Exception as e:
            # SQLGetTypeInfo might not be supported
            self.datatypes.append({'error': f'SQLGetTypeInfo error: {str(e)}'})


def format_driver_info_report(driver_data: Dict[str, Any]) -> str:
    """Format driver information into a readable report."""
    
    lines = []
    lines.append("=" * 80)
    lines.append("DRIVER INFORMATION REPORT")
    lines.append("=" * 80)
    
    if 'error' in driver_data:
        lines.append(f"\nERROR collecting driver info: {driver_data['error']}\n")
    
    info = driver_data.get('info', {})
    
    # Driver & DBMS Section
    lines.append("\n=== DRIVER & DATABASE MANAGEMENT SYSTEM ===")
    lines.append(f"  Driver Name:          {info.get('DRIVER_NAME', 'N/A')}")
    lines.append(f"  Driver Version:       {info.get('DRIVER_VER', 'N/A')}")
    lines.append(f"  Driver ODBC Version:  {info.get('DRIVER_ODBC_VER', 'N/A')}")
    lines.append(f"  ODBC Version (DM):    {info.get('ODBC_VER', 'N/A')}")
    lines.append(f"  DBMS Name:            {info.get('DBMS_NAME', 'N/A')}")
    lines.append(f"  DBMS Version:         {info.get('DBMS_VER', 'N/A')}")
    lines.append(f"  Database:             {info.get('DATABASE_NAME', 'N/A')}")
    lines.append(f"  Server:               {info.get('SERVER_NAME', 'N/A')}")
    lines.append(f"  Data Source:          {info.get('DATA_SOURCE_NAME', 'N/A')}")
    lines.append(f"  User:                 {info.get('USER_NAME', 'N/A')}")
    
    # SQL Conformance
    lines.append("\n=== SQL CONFORMANCE ===")
    sql_conf = info.get('SQL_CONFORMANCE')
    conf_map = {0: 'SQL-92 Entry', 1: 'FIPS127-2 Transitional', 2: 'SQL-92 Intermediate', 3: 'SQL-92 Full'}
    lines.append(f"  SQL Conformance:      {conf_map.get(sql_conf, f'Unknown ({sql_conf})')}")
    
    # Terminology
    lines.append("\n=== TERMINOLOGY ===")
    lines.append(f"  Catalog Support:      {'YES' if info.get('CATALOG_NAME') == 'Y' else 'NO'}")
    lines.append(f"  Catalog Term:         {info.get('CATALOG_TERM', 'N/A')}")
    lines.append(f"  Catalog Separator:    {info.get('CATALOG_NAME_SEPARATOR', 'N/A')}")
    lines.append(f"  Schema Term:          {info.get('SCHEMA_TERM', 'N/A')}")
    lines.append(f"  Table Term:           {info.get('TABLE_TERM', 'N/A')}")
    lines.append(f"  Procedure Term:       {info.get('PROCEDURE_TERM', 'N/A')}")
    lines.append(f"  Quote Character:      {info.get('IDENTIFIER_QUOTE_CHAR', 'N/A')}")
    
    # Limits
    lines.append("\n=== LIMITS ===")
    lines.append(f"  Max Column Name:      {info.get('MAX_COLUMN_NAME_LEN', 'N/A')}")
    lines.append(f"  Max Table Name:       {info.get('MAX_TABLE_NAME_LEN', 'N/A')}")
    lines.append(f"  Max Columns/SELECT:   {info.get('MAX_COLUMNS_IN_SELECT', 'N/A')}")
    lines.append(f"  Max Columns/TABLE:    {info.get('MAX_COLUMNS_IN_TABLE', 'N/A')}")
    lines.append(f"  Max Statement Length: {info.get('MAX_STATEMENT_LEN', 'N/A')}")
    lines.append(f"  Max Tables/SELECT:    {info.get('MAX_TABLES_IN_SELECT', 'N/A')}")
    
    # Features
    lines.append("\n=== FEATURES ===")
    lines.append(f"  Procedures:           {info.get('PROCEDURES', 'N/A')}")
    
    # Outer join capabilities (bitmask)
    oj_caps = info.get('OJ_CAPABILITIES')
    if oj_caps is not None and oj_caps > 0:
        oj_features = []
        if hasattr(pyodbc, 'SQL_OJ_LEFT') and oj_caps & pyodbc.SQL_OJ_LEFT:
            oj_features.append('LEFT')
        if hasattr(pyodbc, 'SQL_OJ_RIGHT') and oj_caps & pyodbc.SQL_OJ_RIGHT:
            oj_features.append('RIGHT')
        if hasattr(pyodbc, 'SQL_OJ_FULL') and oj_caps & pyodbc.SQL_OJ_FULL:
            oj_features.append('FULL')
        lines.append(f"  Outer Joins:          {', '.join(oj_features) if oj_features else 'YES'}")
    else:
        lines.append(f"  Outer Joins:          NO")
    
    lines.append(f"  Read Only:            {info.get('DATA_SOURCE_READ_ONLY', 'N/A')}")
    lines.append(f"  Multiple Active TXN:  {info.get('MULTIPLE_ACTIVE_TXN', 'N/A')}")
    
    # File usage
    file_usage = info.get('FILE_USAGE')
    file_map = {0: 'Not a file-based driver', 1: 'File = Table', 2: 'File = Catalog'}
    if file_usage is not None:
        lines.append(f"  File Usage:           {file_map.get(file_usage, f'Unknown ({file_usage})/A')}")
    lines.append(f"  Multiple Active TXN:  {info.get('MULTIPLE_ACTIVE_TXN', 'N/A')}")
    
    # Transaction Support
    txn_capable = info.get('TXN_CAPABLE')
    txn_map = {0: 'Not Supported', 1: 'DML Only', 2: 'DDL causes commit', 3: 'DDL ignored', 4: 'DML+DDL'}
    lines.append(f"  Transaction Support:  {txn_map.get(txn_capable, f'Unknown ({txn_capable})')}")
    
    # Character Set
    lines.append("\n=== CHARACTER SET ===")
    lines.append(f"  Collation Sequence:   {info.get('COLLATION_SEQ', 'N/A')}")
    
    # Supported ODBC Functions (via SQLGetFunctions)
    functions = driver_data.get('functions', {})
    if functions and '_error' not in functions:
        lines.append(f"\n=== ODBC FUNCTIONS (via SQLGetFunctions) ===")
        
        summary = functions.get('_summary', 'Unknown')
        lines.append(f"  {summary}")
        
        # Group by category
        catalog_funcs = [k for k in functions.keys() if k.startswith('SQL') and ('Columns' in k or 'Tables' in k or 'Procedures' in k or 'Primary' in k or 'Foreign' in k or 'Special' in k or 'Statistics' in k or 'Privileges' in k)]
        core_funcs = [k for k in functions.keys() if k.startswith('SQL') and k not in catalog_funcs and k not in ['_summary', '_error']]
        
        if catalog_funcs:
            supported_catalog = [f for f in catalog_funcs if functions.get(f)]
            missing_catalog = [f for f in catalog_funcs if not functions.get(f)]
            lines.append(f"  Catalog functions:    {len(supported_catalog)}/{len(catalog_funcs)}")
            if missing_catalog:
                lines.append(f"    MISSING: {', '.join([f.replace('SQL', '') for f in missing_catalog])}")
        
        if core_funcs:
            supported_core = [f for f in core_funcs if functions.get(f)]
            missing_core = [f for f in core_funcs if not functions.get(f)]
            lines.append(f"  Core/Extended funcs:  {len(supported_core)}/{len(core_funcs)}")
            if missing_core:
                lines.append(f"    MISSING: {', '.join([f.replace('SQL', '') for f in missing_core])}")
    elif '_error' in functions:
        lines.append(f"\n=== ODBC FUNCTIONS ===")
        lines.append(f"  Error: {functions['_error']}")
    
    # Supported Data Types
    datatypes = driver_data.get('datatypes', [])
    if datatypes and 'error' not in datatypes[0]:
        lines.append(f"\n=== SUPPORTED DATA TYPES ({len(datatypes)} types) ===")
        lines.append("")
        lines.append("  Type Name                    SQL Type   Max Size      Nullable  Auto-Inc")
        lines.append("  " + "-" * 76)
        
        # Show ALL data types
        for dt in datatypes:
            type_name = dt.get('TYPE_NAME', 'UNKNOWN')
            data_type = dt.get('DATA_TYPE', '?')
            size = dt.get('COLUMN_SIZE', '?')
            nullable = dt.get('NULLABLE')
            auto_inc = dt.get('AUTO_UNIQUE_VALUE')
            
            # Format values for display
            nullable_str = 'Yes' if nullable == 1 else 'No' if nullable == 0 else '?'
            auto_inc_str = 'Yes' if auto_inc else ''
            size_str = str(size) if size != '?' else 'N/A'
            
            # Format the line with proper spacing
            lines.append(f"  {type_name:28s} {str(data_type):9s}  {size_str:12s}  {nullable_str:8s}  {auto_inc_str}")
    
    lines.append("\n" + "=" * 80)
    
    return "\n".join(lines)
