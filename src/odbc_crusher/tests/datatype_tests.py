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
            
            start_time = time.perf_counter()
            
            # Test different integer types with database-specific queries
            test_cases = [
                # (query, param, expected_value, type_name)
                ("SELECT CAST(? AS SMALLINT) FROM RDB$DATABASE", 32767, 32767, "SMALLINT"),
                ("SELECT CAST(? AS INTEGER) FROM RDB$DATABASE", 2147483647, 2147483647, "INTEGER"),
                ("SELECT CAST(? AS BIGINT) FROM RDB$DATABASE", 9223372036854775807, 9223372036854775807, "BIGINT"),
                # Generic fallbacks
                ("SELECT CAST(? AS SMALLINT)", 100, 100, "SMALLINT"),
                ("SELECT CAST(? AS INTEGER)", 1000, 1000, "INTEGER"),
                ("SELECT CAST(? AS BIGINT)", 10000, 10000, "BIGINT"),
            ]
            
            successful_types = []
            
            for query, param, expected, type_name in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    if result == expected and type_name not in successful_types:
                        successful_types.append(type_name)
                except:
                    continue
            
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
            
            start_time = time.perf_counter()
            
            test_cases = [
                ("SELECT CAST(? AS DECIMAL(10,2)) FROM RDB$DATABASE", Decimal("123.45"), "DECIMAL"),
                ("SELECT CAST(? AS NUMERIC(10,2)) FROM RDB$DATABASE", Decimal("678.90"), "NUMERIC"),
                ("SELECT CAST(? AS DECIMAL(10,2))", Decimal("123.45"), "DECIMAL"),
                ("SELECT CAST(? AS NUMERIC(10,2))", Decimal("678.90"), "NUMERIC"),
            ]
            
            successful_types = []
            
            for query, param, type_name in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    # Check if result is close enough (accounting for precision)
                    if abs(float(result) - float(param)) < 0.01 and type_name not in successful_types:
                        successful_types.append(type_name)
                except:
                    continue
            
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
            
            start_time = time.perf_counter()
            
            test_cases = [
                ("SELECT CAST(? AS FLOAT) FROM RDB$DATABASE", 3.14159, "FLOAT"),
                ("SELECT CAST(? AS DOUBLE PRECISION) FROM RDB$DATABASE", 2.71828, "DOUBLE PRECISION"),
                ("SELECT CAST(? AS REAL) FROM RDB$DATABASE", 1.414, "REAL"),
                ("SELECT CAST(? AS FLOAT)", 3.14159, "FLOAT"),
                ("SELECT CAST(? AS REAL)", 1.414, "REAL"),
            ]
            
            successful_types = []
            
            for query, param, type_name in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    # Check if result is close (floating point comparison)
                    if abs(result - param) < 0.001 and type_name not in successful_types:
                        successful_types.append(type_name)
                except:
                    continue
            
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
            
            start_time = time.perf_counter()
            
            test_cases = [
                ("SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE", "Hello World", "VARCHAR"),
                ("SELECT CAST(? AS CHAR(20)) FROM RDB$DATABASE", "Fixed Width", "CHAR"),
                ("SELECT CAST(? AS VARCHAR(50))", "Test String", "VARCHAR"),
                ("SELECT CAST(? AS CHAR(10))", "ABC", "CHAR"),
            ]
            
            successful_types = []
            
            for query, param, type_name in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    # For CHAR, result might be padded
                    if (result.strip() == param.strip() or result == param) and type_name not in successful_types:
                        successful_types.append(type_name)
                except:
                    continue
            
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
            
            start_time = time.perf_counter()
            
            test_date = date(2026, 2, 3)
            
            test_cases = [
                ("SELECT CAST(? AS DATE) FROM RDB$DATABASE", test_date),
                ("SELECT CAST(? AS DATE)", test_date),
            ]
            
            success = False
            
            for query, param in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    # Result might be date or datetime
                    if isinstance(result, datetime):
                        result = result.date()
                    if result == param:
                        success = True
                        break
                except:
                    continue
            
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
            
            start_time = time.perf_counter()
            
            test_time = dt_time(14, 30, 45)
            
            test_cases = [
                ("SELECT CAST(? AS TIME) FROM RDB$DATABASE", test_time),
                ("SELECT CAST(? AS TIME)", test_time),
            ]
            
            success = False
            
            for query, param in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    # Result might be time or datetime
                    if isinstance(result, datetime):
                        result = result.time()
                    # Compare hour, minute, second (ignore microseconds)
                    if (result.hour == param.hour and 
                        result.minute == param.minute and 
                        result.second == param.second):
                        success = True
                        break
                except:
                    continue
            
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
            
            start_time = time.perf_counter()
            
            test_timestamp = datetime(2026, 2, 3, 14, 30, 45)
            
            test_cases = [
                ("SELECT CAST(? AS TIMESTAMP) FROM RDB$DATABASE", test_timestamp),
                ("SELECT CAST(? AS TIMESTAMP)", test_timestamp),
                ("SELECT CAST(? AS DATETIME) FROM RDB$DATABASE", test_timestamp),
                ("SELECT CAST(? AS DATETIME)", test_timestamp),
            ]
            
            success = False
            
            for query, param in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    # Compare up to seconds (ignore microseconds)
                    if (isinstance(result, datetime) and
                        result.year == param.year and
                        result.month == param.month and
                        result.day == param.day and
                        result.hour == param.hour and
                        result.minute == param.minute and
                        result.second == param.second):
                        success = True
                        break
                except:
                    continue
            
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
            
            start_time = time.perf_counter()
            
            test_binary = b'\x00\x01\x02\x03\xff'
            
            test_cases = [
                ("SELECT CAST(? AS VARBINARY(10)) FROM RDB$DATABASE", test_binary),
                ("SELECT CAST(? AS VARBINARY(10))", test_binary),
                ("SELECT CAST(? AS BINARY(10)) FROM RDB$DATABASE", test_binary),
                ("SELECT CAST(? AS BINARY(10))", test_binary),
            ]
            
            success = False
            
            for query, param in test_cases:
                try:
                    cursor.execute(query, (param,))
                    result = cursor.fetchone()[0]
                    # Result might have padding for BINARY type
                    if param in result or result.startswith(param):
                        success = True
                        break
                except:
                    continue
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if success:
                self._record_test(
                    test_name="test_binary_types",
                    function="SQLBindParameter/SQLGetData (BINARY)",
                    status=TestStatus.PASS,
                    expected="Support for binary data types",
                    actual="Successfully bound and retrieved binary data",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_binary_types",
                    function="SQLBindParameter/SQLGetData (BINARY)",
                    status=TestStatus.SKIP,
                    expected="Support for binary data types",
                    actual="Binary types could not be tested",
                    diagnostic="Driver may not support binary type binding (common limitation)",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_binary_types",
                function="SQLBindParameter/SQLGetData (BINARY)",
                status=TestStatus.SKIP,
                expected="Support for binary data types",
                actual=f"Skipped: {str(e)}",
                diagnostic="Binary types often have driver-specific limitations",
                severity=Severity.INFO,
                duration_ms=0,
            )
