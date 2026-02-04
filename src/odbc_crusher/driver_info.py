"""Driver information collection using ODBC functions."""

import pyodbc
from typing import Dict, List, Any, Optional
from enum import Enum


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
        
        # Define all SQL_xxx info types we want to query
        # Organized by category for clarity
        info_types = {
            # Driver Information
            'DRIVER_NAME': pyodbc.SQL_DRIVER_NAME,
            'DRIVER_VER': pyodbc.SQL_DRIVER_VER,
            'DRIVER_ODBC_VER': pyodbc.SQL_DRIVER_ODBC_VER,
            
            # DBMS Information
            'DBMS_NAME': pyodbc.SQL_DBMS_NAME,
            'DBMS_VER': pyodbc.SQL_DBMS_VER,
            'DATABASE_NAME': pyodbc.SQL_DATABASE_NAME,
            'SERVER_NAME': pyodbc.SQL_SERVER_NAME,
            'DATA_SOURCE_NAME': pyodbc.SQL_DATA_SOURCE_NAME,
            'USER_NAME': pyodbc.SQL_USER_NAME,
            
            # SQL Support
            'SQL_CONFORMANCE': pyodbc.SQL_SQL_CONFORMANCE,
            'ODBC_VER': pyodbc.SQL_ODBC_VER,
            
            # Catalog/Schema Support
            'CATALOG_NAME': pyodbc.SQL_CATALOG_NAME,
            'CATALOG_TERM': pyodbc.SQL_CATALOG_TERM,
            'SCHEMA_TERM': pyodbc.SQL_SCHEMA_TERM,
            'TABLE_TERM': pyodbc.SQL_TABLE_TERM,
            'PROCEDURE_TERM': pyodbc.SQL_PROCEDURE_TERM,
            
            # Identifiers
            'IDENTIFIER_QUOTE_CHAR': pyodbc.SQL_IDENTIFIER_QUOTE_CHAR,
            'IDENTIFIER_CASE': pyodbc.SQL_IDENTIFIER_CASE,
            'QUOTED_IDENTIFIER_CASE': pyodbc.SQL_QUOTED_IDENTIFIER_CASE,
            
            # Limits
            'MAX_COLUMN_NAME_LEN': pyodbc.SQL_MAX_COLUMN_NAME_LEN,
            'MAX_TABLE_NAME_LEN': pyodbc.SQL_MAX_TABLE_NAME_LEN,
            'MAX_COLUMNS_IN_SELECT': pyodbc.SQL_MAX_COLUMNS_IN_SELECT,
            'MAX_COLUMNS_IN_TABLE': pyodbc.SQL_MAX_COLUMNS_IN_TABLE,
            'MAX_STATEMENT_LEN': pyodbc.SQL_MAX_STATEMENT_LEN,
            'MAX_TABLES_IN_SELECT': pyodbc.SQL_MAX_TABLES_IN_SELECT,
            
            # Transaction Support
            'TXN_CAPABLE': pyodbc.SQL_TXN_CAPABLE,
            'DEFAULT_TXN_ISOLATION': pyodbc.SQL_DEFAULT_TXN_ISOLATION,
            'MULTIPLE_ACTIVE_TXN': pyodbc.SQL_MULTIPLE_ACTIVE_TXN,
            
            # Features
            'PROCEDURES': pyodbc.SQL_PROCEDURES,
            'MULT_RESULT_SETS': pyodbc.SQL_MULT_RESULT_SETS,
            'ACCESSIBLE_TABLES': pyodbc.SQL_ACCESSIBLE_TABLES,
            'ACCESSIBLE_PROCEDURES': pyodbc.SQL_ACCESSIBLE_PROCEDURES,
            'OUTER_JOINS': pyodbc.SQL_OUTER_JOINS,
            'NULL_COLLATION': pyodbc.SQL_NULL_COLLATION,
            
            # Cursor Support
            'CURSOR_COMMIT_BEHAVIOR': pyodbc.SQL_CURSOR_COMMIT_BEHAVIOR,
            'CURSOR_ROLLBACK_BEHAVIOR': pyodbc.SQL_CURSOR_ROLLBACK_BEHAVIOR,
            
            # Data Source Characteristics
            'DATA_SOURCE_READ_ONLY': pyodbc.SQL_DATA_SOURCE_READ_ONLY,
            'FILE_USAGE': pyodbc.SQL_FILE_USAGE,
            
            # Character Set
            'COLLATION_SEQ': pyodbc.SQL_COLLATION_SEQ,
        }
        
        for name, info_type in info_types.items():
            try:
                value = conn.getinfo(info_type)
                self.info[name] = value
            except:
                # Some info types may not be supported by all drivers
                self.info[name] = None
    
    def _collect_sqlgetfunctions(self, conn: pyodbc.Connection):
        """Collect supported functions using SQLGetFunctions (via pyodbc introspection)."""
        
        # PyODBC doesn't expose SQLGetFunctions directly, but we can test
        # if methods exist and work. We'll test key ODBC functions.
        
        # Core functions - these should always exist if driver is ODBC compliant
        core_functions = [
            'tables', 'columns', 'statistics', 'primaryKeys', 'foreignKeys',
            'procedures', 'procedureColumns', 'getTypeInfo',
        ]
        
        cursor = conn.cursor()
        
        for func_name in core_functions:
            try:
                if hasattr(cursor, func_name):
                    # Try to call it with minimal parameters to see if it's implemented
                    func = getattr(cursor, func_name)
                    # Don't actually execute, just check if callable
                    self.functions[func_name] = callable(func)
                else:
                    self.functions[func_name] = False
            except:
                self.functions[func_name] = False
        
        cursor.close()
        
    def _collect_sqlgettypeinfo(self, conn: pyodbc.Connection):
        """Collect supported data types using SQLGetTypeInfo."""
        
        try:
            cursor = conn.cursor()
            
            # Call SQLGetTypeInfo with SQL_ALL_TYPES to get all supported types
            # In pyodbc, this is cursor.getTypeInfo()
            try:
                rows = cursor.getTypeInfo(pyodbc.SQL_ALL_TYPES)
                
                for row in rows:
                    datatype = {
                        'TYPE_NAME': row.type_name,
                        'DATA_TYPE': row.data_type,
                        'COLUMN_SIZE': row.column_size if hasattr(row, 'column_size') else None,
                        'LITERAL_PREFIX': row.literal_prefix if hasattr(row, 'literal_prefix') else None,
                        'LITERAL_SUFFIX': row.literal_suffix if hasattr(row, 'literal_suffix') else None,
                        'CREATE_PARAMS': row.create_params if hasattr(row, 'create_params') else None,
                        'NULLABLE': row.nullable if hasattr(row, 'nullable') else None,
                        'CASE_SENSITIVE': row.case_sensitive if hasattr(row, 'case_sensitive') else None,
                        'SEARCHABLE': row.searchable if hasattr(row, 'searchable') else None,
                        'UNSIGNED_ATTRIBUTE': row.unsigned_attribute if hasattr(row, 'unsigned_attribute') else None,
                        'FIXED_PREC_SCALE': row.fixed_prec_scale if hasattr(row, 'fixed_prec_scale') else None,
                        'AUTO_UNIQUE_VALUE': row.auto_unique_value if hasattr(row, 'auto_unique_value') else None,
                        'MINIMUM_SCALE': row.minimum_scale if hasattr(row, 'minimum_scale') else None,
                        'MAXIMUM_SCALE': row.maximum_scale if hasattr(row, 'maximum_scale') else None,
                    }
                    self.datatypes.append(datatype)
            except AttributeError:
                # pyodbc might not have getTypeInfo method
                # Fallback: execute raw SQL
                pass
            
            cursor.close()
        except Exception as e:
            # SQLGetTypeInfo might not be supported
            self.datatypes.append({'error': str(e)})


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
    lines.append(f"  Multiple Result Sets: {info.get('MULT_RESULT_SETS', 'N/A')}")
    lines.append(f"  Outer Joins:          {info.get('OUTER_JOINS', 'N/A')}")
    lines.append(f"  Read Only:            {info.get('DATA_SOURCE_READ_ONLY', 'N/A')}")
    lines.append(f"  Multiple Active TXN:  {info.get('MULTIPLE_ACTIVE_TXN', 'N/A')}")
    
    # Transaction Support
    txn_capable = info.get('TXN_CAPABLE')
    txn_map = {0: 'Not Supported', 1: 'DML Only', 2: 'DDL causes commit', 3: 'DDL ignored', 4: 'DML+DDL'}
    lines.append(f"  Transaction Support:  {txn_map.get(txn_capable, f'Unknown ({txn_capable})')}")
    
    # Character Set
    lines.append("\n=== CHARACTER SET ===")
    lines.append(f"  Collation Sequence:   {info.get('COLLATION_SEQ', 'N/A')}")
    
    # Supported Data Types
    datatypes = driver_data.get('datatypes', [])
    if datatypes and 'error' not in datatypes[0]:
        lines.append(f"\n=== SUPPORTED DATA TYPES ({len(datatypes)} types) ===")
        
        for dt in datatypes[:20]:  # Show first 20 to avoid overwhelming output
            type_name = dt.get('TYPE_NAME', 'UNKNOWN')
            data_type = dt.get('DATA_TYPE', '?')
            size = dt.get('COLUMN_SIZE', '?')
            nullable = 'NULL' if dt.get('NULLABLE') == 1 else 'NOT NULL'
            auto = 'AUTO' if dt.get('AUTO_UNIQUE_VALUE') else ''
            
            lines.append(f"  {type_name:20s} (SQL type {data_type:3}) - Size: {str(size):8s} {nullable:8s} {auto}")
        
        if len(datatypes) > 20:
            lines.append(f"  ... and {len(datatypes) - 20} more types")
    
    lines.append("\n" + "=" * 80)
    
    return "\n".join(lines)
