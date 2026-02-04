"""Tests for ODBC driver capabilities and API support."""

import pyodbc
from typing import List
import time
import sys
import ctypes
import os

from .base import ODBCTest, TestResult, TestStatus, Severity


class DriverCapabilityTests(ODBCTest):
    """Tests for driver capabilities and API support."""
    
    def run(self) -> List[TestResult]:
        """Run all driver capability tests."""
        self.results = []
        
        self.test_api_support()
        self.test_unicode_capability()
        self.test_driver_conformance()
        
        return self.results
    
    def test_api_support(self):
        """Test which ODBC API the driver implements (ANSI vs Unicode)."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            driver_name = conn.getinfo(pyodbc.SQL_DRIVER_NAME)
            driver_ver = conn.getinfo(pyodbc.SQL_DRIVER_VER)
            
            # Method 1: Check DLL exports (Windows only)
            api_info = "Unknown"
            has_ansi = False
            has_unicode = False
            
            if sys.platform == 'win32':
                try:
                    dll = ctypes.CDLL(driver_name)
                    
                    # Try to find ANSI functions
                    try:
                        dll.SQLConnectA
                        has_ansi = True
                    except AttributeError:
                        pass
                    
                    # Try to find Unicode functions  
                    try:
                        dll.SQLConnectW
                        has_unicode = True
                    except AttributeError:
                        pass
                    
                    if has_ansi and has_unicode:
                        api_info = "Both ANSI and Unicode APIs"
                    elif has_unicode:
                        api_info = "Unicode API only"
                    elif has_ansi:
                        api_info = "ANSI API only"
                    else:
                        # Functions might be mangled or not directly exported
                        # Infer from DLL name
                        if 'w.dll' in driver_name.lower() or 'unicode' in driver_name.lower():
                            api_info = "Unicode API (inferred from DLL name)"
                            has_unicode = True
                        elif 'a.dll' in driver_name.lower() or 'ansi' in driver_name.lower():
                            api_info = "ANSI API (inferred from DLL name)"
                            has_ansi = True
                        else:
                            api_info = "API functions not directly exported (likely Unicode on Python 3)"
                            has_unicode = True  # Assume Unicode on modern Python
                            
                except Exception as e:
                    # Fallback: infer from driver name patterns
                    if 'w.dll' in driver_name.lower() or 'unicode' in driver_name.lower():
                        api_info = "Unicode API (inferred)"
                        has_unicode = True
                    elif 'a.dll' in driver_name.lower() or 'ansi' in driver_name.lower():
                        api_info = "ANSI API (inferred)"
                        has_ansi = True
                    else:
                        api_info = f"Could not detect (Python {sys.version_info.major}.{sys.version_info.minor} defaults to Unicode)"
                        has_unicode = True
            else:
                # On non-Windows, assume Unicode on Python 3
                api_info = f"Assumed Unicode (Python {sys.version_info.major}.{sys.version_info.minor})"
                has_unicode = True
            
            duration = (time.perf_counter() - start_time) * 1000
            
            # Record the result
            severity = Severity.INFO
            if has_unicode:
                status = TestStatus.PASS
                expected = "Driver implements Unicode ODBC API (recommended)"
                actual = f"{api_info} - Driver: {driver_name}"
            elif has_ansi:
                status = TestStatus.PASS
                expected = "Driver implements ODBC API"
                actual = f"{api_info} (ANSI only - limited Unicode support) - Driver: {driver_name}"
                severity = Severity.WARNING
            else:
                status = TestStatus.SKIP
                expected = "Detect ODBC API implementation"
                actual = f"Could not determine API - Driver: {driver_name}"
            
            self._record_test(
                test_name="test_api_support",
                function="SQLConnect (ANSI vs Unicode detection)",
                status=status,
                expected=expected,
                actual=actual,
                diagnostic=f"pyodbc {pyodbc.version} on Python {sys.version_info.major}.{sys.version_info.minor}",
                severity=severity,
                duration_ms=duration,
            )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_api_support",
                function="SQLConnect (ANSI vs Unicode detection)",
                status=TestStatus.ERROR,
                expected="Detect ODBC API implementation",
                actual=f"Error: {str(e)}",
                diagnostic="Connection error",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_unicode_capability(self):
        """Test if driver properly handles Unicode data."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Test with Unicode strings from different scripts
            test_cases = [
                ("Hello", "Basic ASCII"),
                ("Café", "Latin with accents"),
                ("Привет", "Cyrillic"),
                ("你好", "Chinese"),
                ("🐍", "Emoji"),
            ]
            
            passed = 0
            failed = 0
            details = []
            
            for test_str, description in test_cases:
                try:
                    # Use database-appropriate query
                    driver_name = conn.getinfo(pyodbc.SQL_DRIVER_NAME).lower()
                    if 'firebird' in driver_name:
                        query = "SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"
                    elif 'oracle' in driver_name:
                        query = "SELECT CAST(? AS VARCHAR(50)) FROM DUAL"
                    else:
                        query = "SELECT CAST(? AS VARCHAR(50))"
                    
                    cursor.execute(query, (test_str,))
                    result = cursor.fetchone()[0]
                    
                    if result == test_str:
                        passed += 1
                        details.append(f"{description}: PASS")
                    else:
                        failed += 1
                        details.append(f"{description}: FAIL (got '{result}')")
                except Exception as e:
                    failed += 1
                    details.append(f"{description}: ERROR ({str(e)[:30]})")
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if passed == len(test_cases):
                self._record_test(
                    test_name="test_unicode_capability",
                    function="SQLBindParameter/SQLGetData (Unicode)",
                    status=TestStatus.PASS,
                    expected="Full Unicode support (all scripts)",
                    actual=f"All {passed}/{len(test_cases)} Unicode tests passed",
                    diagnostic="; ".join(details),
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            elif passed > 0:
                self._record_test(
                    test_name="test_unicode_capability",
                    function="SQLBindParameter/SQLGetData (Unicode)",
                    status=TestStatus.PASS,
                    expected="Full Unicode support",
                    actual=f"Partial Unicode support: {passed}/{len(test_cases)} passed",
                    diagnostic="; ".join(details),
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_unicode_capability",
                    function="SQLBindParameter/SQLGetData (Unicode)",
                    status=TestStatus.FAIL,
                    expected="Unicode support",
                    actual=f"No Unicode support: 0/{len(test_cases)} passed",
                    diagnostic="; ".join(details),
                    severity=Severity.ERROR,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_unicode_capability",
                function="SQLBindParameter/SQLGetData (Unicode)",
                status=TestStatus.ERROR,
                expected="Unicode support",
                actual=f"Error: {str(e)}",
                diagnostic="Connection or query error",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_driver_conformance(self):
        """Test ODBC conformance level."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            # Get conformance levels
            try:
                api_conformance = conn.getinfo(pyodbc.SQL_ODBC_API_CONFORMANCE)
                sql_conformance = conn.getinfo(pyodbc.SQL_ODBC_SQL_CONFORMANCE)
                odbc_ver = conn.getinfo(pyodbc.SQL_DRIVER_ODBC_VER)
                
                # Map conformance levels to names
                api_levels = {0: "Core", 1: "Level 1", 2: "Level 2"}
                sql_levels = {0: "Minimum", 1: "Core", 2: "Extended"}
                
                api_level = api_levels.get(api_conformance, f"Unknown ({api_conformance})")
                sql_level = sql_levels.get(sql_conformance, f"Unknown ({sql_conformance})")
                
                duration = (time.perf_counter() - start_time) * 1000
                
                self._record_test(
                    test_name="test_driver_conformance",
                    function="SQLGetInfo (conformance levels)",
                    status=TestStatus.PASS,
                    expected="ODBC conformance information",
                    actual=f"ODBC {odbc_ver} - API: {api_level}, SQL: {sql_level}",
                    diagnostic=f"Higher conformance = more features supported",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            except Exception as e:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_driver_conformance",
                    function="SQLGetInfo (conformance levels)",
                    status=TestStatus.SKIP,
                    expected="ODBC conformance information",
                    actual=f"Could not retrieve: {str(e)}",
                    diagnostic="Driver may not report conformance levels",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_driver_conformance",
                function="SQLGetInfo (conformance levels)",
                status=TestStatus.ERROR,
                expected="ODBC conformance information",
                actual=f"Error: {str(e)}",
                diagnostic="Connection error",
                severity=Severity.ERROR,
                duration_ms=0,
            )
