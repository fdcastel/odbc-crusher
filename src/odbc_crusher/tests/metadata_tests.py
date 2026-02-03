"""Tests for ODBC metadata catalog functions."""

import pyodbc
from typing import List
import time

from .base import ODBCTest, TestResult, TestStatus, Severity


class MetadataTests(ODBCTest):
    """Tests for ODBC catalog/metadata functions."""
    
    def run(self) -> List[TestResult]:
        """Run all metadata tests."""
        self.results = []
        
        self.test_tables()
        self.test_columns()
        self.test_primary_keys()
        self.test_foreign_keys()
        self.test_statistics()
        self.test_type_info()
        
        return self.results
    
    def test_tables(self):
        """Test SQLTables - retrieve table list."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Get list of tables
            tables = cursor.tables()
            table_count = 0
            table_names = []
            
            for row in tables:
                table_count += 1
                if table_count <= 5:  # Collect first 5 table names
                    table_names.append(row.table_name)
            
            duration = (time.perf_counter() - start_time) * 1000
            
            cursor.close()
            conn.close()
            
            if table_count > 0:
                sample = ", ".join(table_names[:3])
                self._record_test(
                    test_name="test_tables",
                    function="SQLTables",
                    status=TestStatus.PASS,
                    expected="Retrieve table catalog information",
                    actual=f"Found {table_count} tables (sample: {sample}...)",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_tables",
                    function="SQLTables",
                    status=TestStatus.PASS,
                    expected="Retrieve table catalog information",
                    actual="No tables found (empty database or system tables only)",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_tables",
                function="SQLTables",
                status=TestStatus.ERROR,
                expected="Retrieve table catalog information",
                actual=f"Error: {str(e)}",
                diagnostic="Driver may not support SQLTables or catalog functions",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_columns(self):
        """Test SQLColumns - retrieve column information."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # First, try to find a table to get columns from
            tables = cursor.tables()
            first_table = None
            
            for row in tables:
                # Skip system tables if possible
                if not row.table_name.startswith(('SYS', 'sys', 'RDB$', 'MON$')):
                    first_table = row.table_name
                    break
            
            # If no user tables, use any table
            if not first_table:
                tables = cursor.tables()
                for row in tables:
                    first_table = row.table_name
                    break
            
            if first_table:
                # Get columns for the table
                columns = cursor.columns(table=first_table)
                column_count = 0
                column_names = []
                
                for row in columns:
                    column_count += 1
                    if column_count <= 5:
                        column_names.append(f"{row.column_name}:{row.type_name}")
                
                duration = (time.perf_counter() - start_time) * 1000
                
                if column_count > 0:
                    sample = ", ".join(column_names[:3])
                    self._record_test(
                        test_name="test_columns",
                        function="SQLColumns",
                        status=TestStatus.PASS,
                        expected="Retrieve column information for a table",
                        actual=f"Found {column_count} columns in '{first_table}' (sample: {sample}...)",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                else:
                    self._record_test(
                        test_name="test_columns",
                        function="SQLColumns",
                        status=TestStatus.FAIL,
                        expected="Retrieve column information for a table",
                        actual=f"Table '{first_table}' has no columns",
                        diagnostic="Driver may not properly report column metadata",
                        severity=Severity.WARNING,
                        duration_ms=duration,
                    )
            else:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_columns",
                    function="SQLColumns",
                    status=TestStatus.SKIP,
                    expected="Retrieve column information for a table",
                    actual="No tables available to test columns",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_columns",
                function="SQLColumns",
                status=TestStatus.ERROR,
                expected="Retrieve column information for a table",
                actual=f"Error: {str(e)}",
                diagnostic="Driver may not support SQLColumns",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_primary_keys(self):
        """Test SQLPrimaryKeys - retrieve primary key information."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Find a table to test
            tables = cursor.tables()
            test_table = None
            
            for row in tables:
                if not row.table_name.startswith(('SYS', 'sys', 'RDB$', 'MON$')):
                    test_table = row.table_name
                    break
            
            if not test_table:
                tables = cursor.tables()
                for row in tables:
                    test_table = row.table_name
                    break
            
            if test_table:
                try:
                    # Get primary keys
                    pk_info = cursor.primaryKeys(table=test_table)
                    pk_count = 0
                    pk_columns = []
                    
                    for row in pk_info:
                        pk_count += 1
                        pk_columns.append(row.column_name)
                    
                    duration = (time.perf_counter() - start_time) * 1000
                    
                    if pk_count > 0:
                        cols = ", ".join(pk_columns)
                        self._record_test(
                            test_name="test_primary_keys",
                            function="SQLPrimaryKeys",
                            status=TestStatus.PASS,
                            expected="Retrieve primary key information",
                            actual=f"Found {pk_count} PK column(s) in '{test_table}': {cols}",
                            severity=Severity.INFO,
                            duration_ms=duration,
                        )
                    else:
                        self._record_test(
                            test_name="test_primary_keys",
                            function="SQLPrimaryKeys",
                            status=TestStatus.PASS,
                            expected="Retrieve primary key information",
                            actual=f"Table '{test_table}' has no primary key",
                            severity=Severity.INFO,
                            duration_ms=duration,
                        )
                except Exception as e:
                    duration = (time.perf_counter() - start_time) * 1000
                    # Some drivers don't support primaryKeys
                    self._record_test(
                        test_name="test_primary_keys",
                        function="SQLPrimaryKeys",
                        status=TestStatus.SKIP,
                        expected="Retrieve primary key information",
                        actual=f"Function not supported: {str(e)}",
                        diagnostic="Driver may not implement SQLPrimaryKeys",
                        severity=Severity.WARNING,
                        duration_ms=duration,
                    )
            else:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_primary_keys",
                    function="SQLPrimaryKeys",
                    status=TestStatus.SKIP,
                    expected="Retrieve primary key information",
                    actual="No tables available to test",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_primary_keys",
                function="SQLPrimaryKeys",
                status=TestStatus.ERROR,
                expected="Retrieve primary key information",
                actual=f"Error: {str(e)}",
                diagnostic="Driver may not support SQLPrimaryKeys",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_foreign_keys(self):
        """Test SQLForeignKeys - retrieve foreign key information."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Find a table
            tables = cursor.tables()
            test_table = None
            
            for row in tables:
                if not row.table_name.startswith(('SYS', 'sys', 'RDB$', 'MON$')):
                    test_table = row.table_name
                    break
            
            if not test_table:
                tables = cursor.tables()
                for row in tables:
                    test_table = row.table_name
                    break
            
            if test_table:
                try:
                    # Get foreign keys
                    fk_info = cursor.foreignKeys(table=test_table)
                    fk_count = 0
                    
                    for row in fk_info:
                        fk_count += 1
                    
                    duration = (time.perf_counter() - start_time) * 1000
                    
                    self._record_test(
                        test_name="test_foreign_keys",
                        function="SQLForeignKeys",
                        status=TestStatus.PASS,
                        expected="Retrieve foreign key information",
                        actual=f"Found {fk_count} foreign key(s) in '{test_table}'",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                except AttributeError:
                    # pyodbc might not have foreignKeys method
                    duration = (time.perf_counter() - start_time) * 1000
                    self._record_test(
                        test_name="test_foreign_keys",
                        function="SQLForeignKeys",
                        status=TestStatus.SKIP,
                        expected="Retrieve foreign key information",
                        actual="pyodbc.cursor.foreignKeys() not available",
                        diagnostic="pyodbc may not expose SQLForeignKeys (version-dependent)",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                except Exception as e:
                    duration = (time.perf_counter() - start_time) * 1000
                    self._record_test(
                        test_name="test_foreign_keys",
                        function="SQLForeignKeys",
                        status=TestStatus.SKIP,
                        expected="Retrieve foreign key information",
                        actual=f"Function not supported: {str(e)}",
                        diagnostic="Driver may not implement SQLForeignKeys",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
            else:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_foreign_keys",
                    function="SQLForeignKeys",
                    status=TestStatus.SKIP,
                    expected="Retrieve foreign key information",
                    actual="No tables available to test",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_foreign_keys",
                function="SQLForeignKeys",
                status=TestStatus.ERROR,
                expected="Retrieve foreign key information",
                actual=f"Error: {str(e)}",
                diagnostic="Driver may not support SQLForeignKeys",
                severity=Severity.INFO,
                duration_ms=0,
            )
    
    def test_statistics(self):
        """Test SQLStatistics - retrieve index/statistics information."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Find a table
            tables = cursor.tables()
            test_table = None
            
            for row in tables:
                if not row.table_name.startswith(('SYS', 'sys', 'RDB$', 'MON$')):
                    test_table = row.table_name
                    break
            
            if not test_table:
                tables = cursor.tables()
                for row in tables:
                    test_table = row.table_name
                    break
            
            if test_table:
                try:
                    # Get statistics/indexes
                    stats = cursor.statistics(table=test_table)
                    stat_count = 0
                    index_names = set()
                    
                    for row in stats:
                        stat_count += 1
                        if hasattr(row, 'index_name') and row.index_name:
                            index_names.add(row.index_name)
                    
                    duration = (time.perf_counter() - start_time) * 1000
                    
                    self._record_test(
                        test_name="test_statistics",
                        function="SQLStatistics",
                        status=TestStatus.PASS,
                        expected="Retrieve statistics and index information",
                        actual=f"Found {len(index_names)} index(es) in '{test_table}' ({stat_count} stat rows)",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                except Exception as e:
                    duration = (time.perf_counter() - start_time) * 1000
                    self._record_test(
                        test_name="test_statistics",
                        function="SQLStatistics",
                        status=TestStatus.SKIP,
                        expected="Retrieve statistics and index information",
                        actual=f"Function not supported: {str(e)}",
                        diagnostic="Driver may not implement SQLStatistics",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
            else:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_statistics",
                    function="SQLStatistics",
                    status=TestStatus.SKIP,
                    expected="Retrieve statistics and index information",
                    actual="No tables available to test",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_statistics",
                function="SQLStatistics",
                status=TestStatus.ERROR,
                expected="Retrieve statistics and index information",
                actual=f"Error: {str(e)}",
                diagnostic="Driver may not support SQLStatistics",
                severity=Severity.INFO,
                duration_ms=0,
            )
    
    def test_type_info(self):
        """Test SQLGetTypeInfo - retrieve data type information."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            try:
                # Get type info
                type_info = cursor.getTypeInfo()
                type_count = 0
                type_names = []
                
                for row in type_info:
                    type_count += 1
                    if type_count <= 10:
                        type_names.append(row.type_name)
                
                duration = (time.perf_counter() - start_time) * 1000
                
                if type_count > 0:
                    sample = ", ".join(type_names[:5])
                    self._record_test(
                        test_name="test_type_info",
                        function="SQLGetTypeInfo",
                        status=TestStatus.PASS,
                        expected="Retrieve data type information",
                        actual=f"Found {type_count} data types (sample: {sample}...)",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                else:
                    self._record_test(
                        test_name="test_type_info",
                        function="SQLGetTypeInfo",
                        status=TestStatus.FAIL,
                        expected="Retrieve data type information",
                        actual="No data types reported",
                        diagnostic="Driver should report at least basic SQL data types",
                        severity=Severity.WARNING,
                        duration_ms=duration,
                    )
            except AttributeError:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_type_info",
                    function="SQLGetTypeInfo",
                    status=TestStatus.SKIP,
                    expected="Retrieve data type information",
                    actual="pyodbc.cursor.getTypeInfo() not available",
                    diagnostic="pyodbc may not expose SQLGetTypeInfo (version-dependent)",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            except Exception as e:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_type_info",
                    function="SQLGetTypeInfo",
                    status=TestStatus.SKIP,
                    expected="Retrieve data type information",
                    actual=f"Function not supported: {str(e)}",
                    diagnostic="Driver may not implement SQLGetTypeInfo",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_type_info",
                function="SQLGetTypeInfo",
                status=TestStatus.ERROR,
                expected="Retrieve data type information",
                actual=f"Error: {str(e)}",
                diagnostic="Driver may not support SQLGetTypeInfo",
                severity=Severity.INFO,
                duration_ms=0,
            )
