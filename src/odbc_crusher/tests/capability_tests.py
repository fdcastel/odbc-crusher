"""Tests for ODBC driver capabilities and API support."""

import pyodbc
from typing import List
import time
import sys

from .base import ODBCTest, TestResult, TestStatus, Severity


class DriverCapabilityTests(ODBCTest):
    """Tests for driver capabilities and API support using SQLGetInfo."""
    
    def run(self) -> List[TestResult]:
        """Run all driver capability tests."""
        self.results = []
        
        self.test_driver_info()
        self.test_unicode_capability()
        self.test_sql_conformance()
        self.test_supported_features()
        
        return self.results
    
    def test_driver_info(self):
        """Test driver and DBMS information using SQLGetInfo."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            # Get driver information using SQLGetInfo (the RIGHT way!)
            driver_name = conn.getinfo(pyodbc.SQL_DRIVER_NAME)
            driver_ver = conn.getinfo(pyodbc.SQL_DRIVER_VER)
            driver_odbc_ver = conn.getinfo(pyodbc.SQL_DRIVER_ODBC_VER)
            
            dbms_name = conn.getinfo(pyodbc.SQL_DBMS_NAME)
            dbms_ver = conn.getinfo(pyodbc.SQL_DBMS_VER)
            
            odbc_ver = conn.getinfo(pyodbc.SQL_ODBC_VER)
            
            duration = (time.perf_counter() - start_time) * 1000
            
            # Determine API support from DLL name (still useful but now supplementary)
            api_hint = ""
            if 'w.dll' in driver_name.lower() or 'unicode' in driver_name.lower():
                api_hint = " [Unicode API]"
            elif 'a.dll' in driver_name.lower() or 'ansi' in driver_name.lower():
                api_hint = " [ANSI API]"
            
            self._record_test(
                test_name="test_driver_info",
                function="SQLGetInfo (Driver/DBMS information)",
                status=TestStatus.PASS,
                expected="Driver and DBMS information",
                actual=f"{driver_name} v{driver_ver}{api_hint} / {dbms_name} v{dbms_ver}",
                diagnostic=f"Driver ODBC: {driver_odbc_ver}, DM ODBC: {odbc_ver}",
                severity=Severity.INFO,
                duration_ms=duration,
            )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_driver_info",
                function="SQLGetInfo (Driver/DBMS information)",
                status=TestStatus.ERROR,
                expected="Driver and DBMS information",
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
    
    def test_sql_conformance(self):
        """Test SQL conformance level using SQLGetInfo."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            # Get SQL conformance level
            if hasattr(pyodbc, 'SQL_SQL_CONFORMANCE'):
                sql_conf = conn.getinfo(pyodbc.SQL_SQL_CONFORMANCE)
                conf_map = {
                    0: 'SQL-92 Entry',
                    1: 'FIPS127-2 Transitional',
                    2: 'SQL-92 Intermediate',
                    3: 'SQL-92 Full'
                }
                conformance_str = conf_map.get(sql_conf, f'Unknown level ({sql_conf})')
            else:
                conformance_str = "SQL_SQL_CONFORMANCE not available"
                sql_conf = None
            
            # Get ODBC interface conformance
            if hasattr(pyodbc, 'SQL_ODBC_INTERFACE_CONFORMANCE'):
                try:
                    odbc_conf = conn.getinfo(pyodbc.SQL_ODBC_INTERFACE_CONFORMANCE)
                    odbc_map = {0: 'Core', 1: 'Level 1', 2: 'Level 2'}
                    odbc_str = odbc_map.get(odbc_conf, f'Unknown ({odbc_conf})')
                except:
                    odbc_str = "Not reported"
            else:
                odbc_str = "Not available"
            
            duration = (time.perf_counter() - start_time) * 1000
            
            self._record_test(
                test_name="test_sql_conformance",
                function="SQLGetInfo (SQL conformance levels)",
                status=TestStatus.PASS,
                expected="SQL/ODBC conformance information",
                actual=f"SQL: {conformance_str}, ODBC Interface: {odbc_str}",
                diagnostic="Higher conformance = more SQL-92 features supported",
                severity=Severity.INFO,
                duration_ms=duration,
            )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_sql_conformance",
                function="SQLGetInfo (SQL conformance levels)",
                status=TestStatus.ERROR,
                expected="SQL/ODBC conformance information",
                actual=f"Error: {str(e)}",
                diagnostic="Connection error",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_supported_features(self):
        """Test supported features using SQLGetInfo."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            features = []
            
            # Check procedures support
            if hasattr(pyodbc, 'SQL_PROCEDURES'):
                procs = conn.getinfo(pyodbc.SQL_PROCEDURES)
                if procs == 'Y':
                    features.append("Procedures")
            
            # Check multiple result sets
            if hasattr(pyodbc, 'SQL_MULT_RESULT_SETS'):
                mrs = conn.getinfo(pyodbc.SQL_MULT_RESULT_SETS)
                if mrs == 'Y':
                    features.append("Multiple result sets")
            
            # Check outer joins
            if hasattr(pyodbc, 'SQL_OJ_CAPABILITIES'):
                oj_caps = conn.getinfo(pyodbc.SQL_OJ_CAPABILITIES)
                if oj_caps > 0:
                    oj_types = []
                    if hasattr(pyodbc, 'SQL_OJ_LEFT') and oj_caps & pyodbc.SQL_OJ_LEFT:
                        oj_types.append("LEFT")
                    if hasattr(pyodbc, 'SQL_OJ_RIGHT') and oj_caps & pyodbc.SQL_OJ_RIGHT:
                        oj_types.append("RIGHT")
                    if hasattr(pyodbc, 'SQL_OJ_FULL') and oj_caps & pyodbc.SQL_OJ_FULL:
                        oj_types.append("FULL")
                    if oj_types:
                        features.append(f"Outer joins ({', '.join(oj_types)})")
            
            # Check transaction support
            if hasattr(pyodbc, 'SQL_TXN_CAPABLE'):
                txn = conn.getinfo(pyodbc.SQL_TXN_CAPABLE)
                txn_map = {0: 'No transactions', 1: 'DML only', 2: 'DDL commits', 3: 'DDL ignored', 4: 'Full DDL+DML'}
                if txn > 0:
                    features.append(f"Transactions ({txn_map.get(txn, 'Yes')})")
            
            # Check catalog/schema support
            if hasattr(pyodbc, 'SQL_CATALOG_NAME'):
                cat = conn.getinfo(pyodbc.SQL_CATALOG_NAME)
                if cat == 'Y':
                    cat_term = conn.getinfo(pyodbc.SQL_CATALOG_TERM) if hasattr(pyodbc, 'SQL_CATALOG_TERM') else 'catalog'
                    features.append(f"Catalogs (term: '{cat_term}')")
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if features:
                self._record_test(
                    test_name="test_supported_features",
                    function="SQLGetInfo (feature detection)",
                    status=TestStatus.PASS,
                    expected="Driver feature support",
                    actual=f"{len(features)} features detected",
                    diagnostic="; ".join(features),
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_supported_features",
                    function="SQLGetInfo (feature detection)",
                    status=TestStatus.PASS,
                    expected="Driver feature support",
                    actual="Basic feature set only",
                    diagnostic="No advanced features detected via SQLGetInfo",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_supported_features",
                function="SQLGetInfo (feature detection)",
                status=TestStatus.ERROR,
                expected="Driver feature support",
                actual=f"Error: {str(e)}",
                diagnostic="Connection error",
                severity=Severity.ERROR,
                duration_ms=0,
            )
