"""Tests for ODBC data type handling and conversion."""

import pyodbc
from typing import List
import time
from decimal import Decimal
from datetime import date, time as dt_time, datetime

from .base import ODBCTest, TestResult, TestStatus, Severity
from ..sql_dialect import SQLDialect


class DataTypeTests(ODBCTest):
    """Tests for SQL data type support."""
    
    def run(self) -> List[TestResult]:
        """Run all data type tests."""
        self.results = []
        
        self.test_integer_types()
        self.test_decimal_types()
        self.test_float_types()
        self.test_character_types()
        self.test_date_type()
        self.test_time_type()
        self.test_timestamp_type()
        self.test_binary_types()
        
        return self.results
    
    def test_integer_types(self):
        """Test integer data types (SMALLINT, INTEGER, BIGINT)."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            # Test different integer types using dialect-aware queries
            # Note: MySQL uses SIGNED/UNSIGNED instead of INTEGER/BIGINT with CAST
            if dialect.db_type.value == "mysql":
                test_cases = [
                    ("CAST(? AS SIGNED)", 32767, 32767, "SIGNED"),
                    ("CAST(? AS SIGNED)", 2147483647, 2147483647, "INTEGER"),
                    ("CAST(? AS SIGNED)", 9223372036854775807, 9223372036854775807, "BIGINT"),
                ]
            else:
                test_cases = [
                    ("CAST(? AS SMALLINT)", 32767, 32767, "SMALLINT"),
                    ("CAST(? AS INTEGER)", 2147483647, 2147483647, "INTEGER"),
                    ("CAST(? AS BIGINT)", 9223372036854775807, 9223372036854775807, "BIGINT"),
                ]
            
            successful_types = []
            
            for select_expr, param, expected, type_name in test_cases:
                result = dialect.execute_with_fallback(cursor, select_expr, (param,))
                if result == expected and type_name not in successful_types:
                    successful_types.append(type_name)
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if len(successful_types) >= 2:
                types_str = ", ".join(successful_types)
                self._record_test(
                    test_name="test_integer_types",
                    function="SQLBindParameter/SQLGetData (integers)",
                    status=TestStatus.PASS,
                    expected="Support for integer data types",
                    actual=f"Successfully tested {len(successful_types)} integer types: {types_str}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            elif len(successful_types) > 0:
                self._record_test(
                    test_name="test_integer_types",
                    function="SQLBindParameter/SQLGetData (integers)",
                    status=TestStatus.PASS,
                    expected="Support for integer data types",
                    actual=f"Limited support: {successful_types[0]}",
                    diagnostic="Driver may not support all integer types",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_integer_types",
                    function="SQLBindParameter/SQLGetData (integers)",
                    status=TestStatus.FAIL,
                    expected="Support for integer data types",
                    actual="No integer types could be tested",
                    diagnostic="Driver may not support integer type binding",
                    severity=Severity.ERROR,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_integer_types",
                function="SQLBindParameter/SQLGetData (integers)",
                status=TestStatus.ERROR,
                expected="Support for integer data types",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_decimal_types(self):
        """Test decimal/numeric data types."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            test_cases = [
                ("CAST(? AS DECIMAL(10,2))", Decimal("123.45"), "DECIMAL"),
                ("CAST(? AS NUMERIC(10,2))", Decimal("678.90"), "NUMERIC"),
            ]
            
            successful_types = []
            
            for select_expr, param, type_name in test_cases:
                result = dialect.execute_with_fallback(cursor, select_expr, (param,))
                if result and abs(float(result) - float(param)) < 0.01 and type_name not in successful_types:
                    successful_types.append(type_name)
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if len(successful_types) > 0:
                types_str = ", ".join(successful_types)
                self._record_test(
                    test_name="test_decimal_types",
                    function="SQLBindParameter/SQLGetData (decimal)",
                    status=TestStatus.PASS,
                    expected="Support for decimal/numeric data types",
                    actual=f"Successfully tested {len(successful_types)} decimal types: {types_str}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_decimal_types",
                    function="SQLBindParameter/SQLGetData (decimal)",
                    status=TestStatus.FAIL,
                    expected="Support for decimal/numeric data types",
                    actual="No decimal types could be tested",
                    diagnostic="Driver may not support decimal type binding",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_decimal_types",
                function="SQLBindParameter/SQLGetData (decimal)",
                status=TestStatus.ERROR,
                expected="Support for decimal/numeric data types",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_float_types(self):
        """Test floating-point data types."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            # MySQL uses DECIMAL for float casting with parameters
            if dialect.db_type.value == "mysql":
                test_cases = [
                    ("CAST(? AS DECIMAL(10,5))", 3.14159, "FLOAT"),
                    ("CAST(? AS DECIMAL(10,5))", 2.71828, "DOUBLE"),
                    ("CAST(? AS DECIMAL(10,3))", 1.414, "REAL"),
                ]
            else:
                test_cases = [
                    ("CAST(? AS FLOAT)", 3.14159, "FLOAT"),
                    ("CAST(? AS DOUBLE PRECISION)", 2.71828, "DOUBLE PRECISION"),
                    ("CAST(? AS REAL)", 1.414, "REAL"),
                ]
            
            successful_types = []
            
            for select_expr, param, type_name in test_cases:
                result = dialect.execute_with_fallback(cursor, select_expr, (param,))
                if result and abs(float(result) - param) < 0.001 and type_name not in successful_types:
                    successful_types.append(type_name)
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if len(successful_types) > 0:
                types_str = ", ".join(successful_types)
                self._record_test(
                    test_name="test_float_types",
                    function="SQLBindParameter/SQLGetData (float)",
                    status=TestStatus.PASS,
                    expected="Support for floating-point data types",
                    actual=f"Successfully tested {len(successful_types)} float types: {types_str}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_float_types",
                    function="SQLBindParameter/SQLGetData (float)",
                    status=TestStatus.FAIL,
                    expected="Support for floating-point data types",
                    actual="No float types could be tested",
                    diagnostic="Driver may not support float type binding",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_float_types",
                function="SQLBindParameter/SQLGetData (float)",
                status=TestStatus.ERROR,
                expected="Support for floating-point data types",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_character_types(self):
        """Test character/string data types."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            test_cases = [
                ("CAST(? AS VARCHAR(50))", "Hello World", "VARCHAR"),
                ("CAST(? AS CHAR(20))", "Fixed Width", "CHAR"),
            ]
            
            successful_types = []
            
            for select_expr, param, type_name in test_cases:
                result = dialect.execute_with_fallback(cursor, select_expr, (param,))
                # For CHAR, result might be padded
                if result and (result.strip() == param.strip() or result == param) and type_name not in successful_types:
                    successful_types.append(type_name)
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if len(successful_types) > 0:
                types_str = ", ".join(successful_types)
                self._record_test(
                    test_name="test_character_types",
                    function="SQLBindParameter/SQLGetData (char)",
                    status=TestStatus.PASS,
                    expected="Support for character data types",
                    actual=f"Successfully tested {len(successful_types)} char types: {types_str}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_character_types",
                    function="SQLBindParameter/SQLGetData (char)",
                    status=TestStatus.FAIL,
                    expected="Support for character data types",
                    actual="No character types could be tested",
                    diagnostic="Driver may not support character type binding",
                    severity=Severity.ERROR,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_character_types",
                function="SQLBindParameter/SQLGetData (char)",
                status=TestStatus.ERROR,
                expected="Support for character data types",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_date_type(self):
        """Test DATE data type."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            test_date = date(2026, 2, 3)
            
            result = dialect.execute_with_fallback(cursor, "CAST(? AS DATE)", (test_date,))
            
            # Result might be date or datetime
            if result:
                if isinstance(result, datetime):
                    result = result.date()
                success = (result == test_date)
            else:
                success = False
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if success:
                self._record_test(
                    test_name="test_date_type",
                    function="SQLBindParameter/SQLGetData (DATE)",
                    status=TestStatus.PASS,
                    expected="Support for DATE data type",
                    actual=f"Successfully bound and retrieved DATE: {test_date}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_date_type",
                    function="SQLBindParameter/SQLGetData (DATE)",
                    status=TestStatus.FAIL,
                    expected="Support for DATE data type",
                    actual="DATE type could not be tested",
                    diagnostic="Driver may not support DATE type binding",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_date_type",
                function="SQLBindParameter/SQLGetData (DATE)",
                status=TestStatus.ERROR,
                expected="Support for DATE data type",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_time_type(self):
        """Test TIME data type."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            test_time = dt_time(14, 30, 45)
            
            result = dialect.execute_with_fallback(cursor, "CAST(? AS TIME)", (test_time,))
            
            # Result might be time or datetime
            if result:
                if isinstance(result, datetime):
                    result = result.time()
                # Compare hour, minute, second (ignore microseconds)
                success = (result.hour == test_time.hour and 
                          result.minute == test_time.minute and 
                          result.second == test_time.second)
            else:
                success = False
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if success:
                self._record_test(
                    test_name="test_time_type",
                    function="SQLBindParameter/SQLGetData (TIME)",
                    status=TestStatus.PASS,
                    expected="Support for TIME data type",
                    actual=f"Successfully bound and retrieved TIME: {test_time}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_time_type",
                    function="SQLBindParameter/SQLGetData (TIME)",
                    status=TestStatus.FAIL,
                    expected="Support for TIME data type",
                    actual="TIME type could not be tested",
                    diagnostic="Driver may not support TIME type binding",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_time_type",
                function="SQLBindParameter/SQLGetData (TIME)",
                status=TestStatus.ERROR,
                expected="Support for TIME data type",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_timestamp_type(self):
        """Test TIMESTAMP data type."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            test_timestamp = datetime(2026, 2, 3, 14, 30, 45)
            
            # Try TIMESTAMP first, then DATETIME as fallback
            result = dialect.execute_with_fallback(cursor, "CAST(? AS TIMESTAMP)", (test_timestamp,))
            if not result:
                result = dialect.execute_with_fallback(cursor, "CAST(? AS DATETIME)", (test_timestamp,))
            
            # Compare up to seconds (ignore microseconds)
            if result and isinstance(result, datetime):
                success = (result.year == test_timestamp.year and
                          result.month == test_timestamp.month and
                          result.day == test_timestamp.day and
                          result.hour == test_timestamp.hour and
                          result.minute == test_timestamp.minute and
                          result.second == test_timestamp.second)
            else:
                success = False
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if success:
                self._record_test(
                    test_name="test_timestamp_type",
                    function="SQLBindParameter/SQLGetData (TIMESTAMP)",
                    status=TestStatus.PASS,
                    expected="Support for TIMESTAMP data type",
                    actual=f"Successfully bound and retrieved TIMESTAMP: {test_timestamp}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_timestamp_type",
                    function="SQLBindParameter/SQLGetData (TIMESTAMP)",
                    status=TestStatus.FAIL,
                    expected="Support for TIMESTAMP data type",
                    actual="TIMESTAMP type could not be tested",
                    diagnostic="Driver may not support TIMESTAMP type binding",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_timestamp_type",
                function="SQLBindParameter/SQLGetData (TIMESTAMP)",
                status=TestStatus.ERROR,
                expected="Support for TIMESTAMP data type",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_binary_types(self):
        """Test binary data types."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            dialect = SQLDialect(conn)
            
            start_time = time.perf_counter()
            
            test_binary = b'\x00\x01\x02\x03\xff'
            
            # Try VARBINARY first, then BINARY
            test_cases = [
                ("CAST(? AS VARBINARY(10))", test_binary, "VARBINARY"),
                ("CAST(? AS BINARY(10))", test_binary, "BINARY"),
            ]
            
            success = False
            success_type = None
            
            for select_expr, param, type_name in test_cases:
                result = dialect.execute_with_fallback(cursor, select_expr, (param,))
                if result:
                    # Result might be bytes or str depending on driver
                    # Convert to bytes if needed for comparison
                    if isinstance(result, str):
                        # Firebird returns string, encode to bytes
                        try:
                            result = result.encode('latin1')
                        except:
                            result = result.encode('utf-8', errors='ignore')
                    
                    # Check if our test data is in the result (might have padding)
                    if isinstance(result, bytes) and (param in result or result.startswith(param)):
                        success = True
                        success_type = type_name
                        break
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if success:
                self._record_test(
                    test_name="test_binary_types",
                    function="SQLBindParameter/SQLGetData (BINARY)",
                    status=TestStatus.PASS,
                    expected="Support for binary data types",
                    actual=f"Successfully bound and retrieved {success_type} data",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_binary_types",
                    function="SQLBindParameter/SQLGetData (BINARY)",
                    status=TestStatus.FAIL,
                    expected="Support for binary data types",
                    actual="Binary types could not be tested",
                    diagnostic="Driver may not support binary type binding or returns incompatible format",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_binary_types",
                function="SQLBindParameter/SQLGetData (BINARY)",
                status=TestStatus.ERROR,
                expected="Support for binary data types",
                actual=f"Error: {str(e)}",
                diagnostic="Binary types often have driver-specific limitations",
                severity=Severity.WARNING,
                duration_ms=0,
            )
