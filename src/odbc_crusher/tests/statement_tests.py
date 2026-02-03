"""Tests for ODBC statement execution and basic queries."""

import pyodbc
from typing import List
import time

from .base import ODBCTest, TestResult, TestStatus, Severity


class StatementTests(ODBCTest):
    """Tests for ODBC statement execution functions."""
    
    def run(self) -> List[TestResult]:
        """Run all statement execution tests."""
        self.results = []
        
        self.test_simple_query()
        self.test_query_with_results()
        self.test_empty_result_set()
        self.test_multiple_statements()
        
        return self.results
    
    def test_simple_query(self):
        """Test executing a simple query."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Try a simple query that should work on most databases
            # We'll try multiple approaches as different DBs support different syntax
            queries_to_try = [
                ("SELECT 1", "Standard SQL"),
                ("SELECT 1 FROM RDB$DATABASE", "Firebird/InterBase"),
                ("SELECT 1 FROM DUAL", "Oracle"),
                ("SELECT 1 AS result", "Generic with alias"),
            ]
            
            success = False
            query_used = None
            error_msg = None
            
            for query, desc in queries_to_try:
                try:
                    cursor.execute(query)
                    result = cursor.fetchone()
                    if result:
                        success = True
                        query_used = f"{query} ({desc})"
                        break
                except:
                    continue
            
            duration = (time.perf_counter() - start_time) * 1000
            
            cursor.close()
            conn.close()
            
            if success:
                self._record_test(
                    test_name="test_simple_query",
                    function="SQLExecDirect",
                    status=TestStatus.PASS,
                    expected="Execute simple SELECT query successfully",
                    actual=f"Query executed successfully: {query_used}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_simple_query",
                    function="SQLExecDirect",
                    status=TestStatus.FAIL,
                    expected="Execute simple SELECT query successfully",
                    actual="All test queries failed",
                    diagnostic="Driver may not support basic SELECT queries or requires specific syntax",
                    severity=Severity.CRITICAL,
                    duration_ms=duration,
                )
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_simple_query",
                function="SQLExecDirect",
                status=TestStatus.ERROR,
                expected="Execute simple SELECT query successfully",
                actual=f"Error: {str(e)}",
                diagnostic="Query execution failed - check connection and driver capabilities",
                severity=Severity.CRITICAL,
                duration_ms=0,
            )
    
    def test_query_with_results(self):
        """Test executing query and fetching results."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Try to query system tables (different syntax for different DBs)
            queries_to_try = [
                "SELECT 1 AS num, 'test' AS txt",
                "SELECT 1 AS num, 'test' AS txt FROM RDB$DATABASE",
                "SELECT 1 AS num, 'test' AS txt FROM DUAL",
            ]
            
            success = False
            row_count = 0
            column_count = 0
            
            for query in queries_to_try:
                try:
                    cursor.execute(query)
                    
                    # Check column count
                    if cursor.description:
                        column_count = len(cursor.description)
                    
                    # Fetch results
                    row = cursor.fetchone()
                    if row:
                        row_count = 1
                        success = True
                        break
                except:
                    continue
            
            duration = (time.perf_counter() - start_time) * 1000
            
            cursor.close()
            conn.close()
            
            if success:
                self._record_test(
                    test_name="test_query_with_results",
                    function="SQLFetch/SQLGetData",
                    status=TestStatus.PASS,
                    expected="Fetch query results successfully",
                    actual=f"Fetched {row_count} row(s) with {column_count} column(s)",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_query_with_results",
                    function="SQLFetch/SQLGetData",
                    status=TestStatus.FAIL,
                    expected="Fetch query results successfully",
                    actual="Failed to fetch results from any test query",
                    diagnostic="Driver may not support result set fetching",
                    severity=Severity.CRITICAL,
                    duration_ms=duration,
                )
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_query_with_results",
                function="SQLFetch/SQLGetData",
                status=TestStatus.ERROR,
                expected="Fetch query results successfully",
                actual=f"Error: {str(e)}",
                diagnostic="Result fetching failed - check driver implementation",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_empty_result_set(self):
        """Test handling queries with no results."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Try queries that should return empty results
            queries_to_try = [
                "SELECT 1 WHERE 1=0",
                "SELECT 1 FROM RDB$DATABASE WHERE 1=0",
                "SELECT 1 FROM DUAL WHERE 1=0",
            ]
            
            success = False
            
            for query in queries_to_try:
                try:
                    cursor.execute(query)
                    row = cursor.fetchone()
                    
                    # Should return None for empty result set
                    if row is None:
                        success = True
                        break
                except:
                    continue
            
            duration = (time.perf_counter() - start_time) * 1000
            
            cursor.close()
            conn.close()
            
            if success:
                self._record_test(
                    test_name="test_empty_result_set",
                    function="SQLFetch (empty results)",
                    status=TestStatus.PASS,
                    expected="Handle empty result set correctly (return NULL/None)",
                    actual="Empty result set handled correctly",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_empty_result_set",
                    function="SQLFetch (empty results)",
                    status=TestStatus.FAIL,
                    expected="Handle empty result set correctly (return NULL/None)",
                    actual="Failed to execute or handle empty result set",
                    diagnostic="Driver may not properly handle empty result sets",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_empty_result_set",
                function="SQLFetch (empty results)",
                status=TestStatus.ERROR,
                expected="Handle empty result set correctly (return NULL/None)",
                actual=f"Error: {str(e)}",
                diagnostic="Error handling empty results",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_multiple_statements(self):
        """Test executing multiple statements in sequence."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            # Execute multiple queries in sequence
            query = "SELECT 1"
            firebird_query = "SELECT 1 FROM RDB$DATABASE"
            
            executed_count = 0
            
            for i in range(3):
                cursor = conn.cursor()
                try:
                    cursor.execute(query)
                    cursor.fetchone()
                    executed_count += 1
                except:
                    try:
                        cursor.execute(firebird_query)
                        cursor.fetchone()
                        executed_count += 1
                    except:
                        pass
                finally:
                    cursor.close()
            
            duration = (time.perf_counter() - start_time) * 1000
            
            conn.close()
            
            if executed_count == 3:
                self._record_test(
                    test_name="test_multiple_statements",
                    function="SQLExecDirect (multiple)",
                    status=TestStatus.PASS,
                    expected="Execute multiple statements sequentially",
                    actual=f"Executed {executed_count} statements successfully",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            elif executed_count > 0:
                self._record_test(
                    test_name="test_multiple_statements",
                    function="SQLExecDirect (multiple)",
                    status=TestStatus.FAIL,
                    expected="Execute multiple statements sequentially",
                    actual=f"Only {executed_count}/3 statements executed",
                    diagnostic="Driver may have issues with statement reuse or resource limits",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_multiple_statements",
                    function="SQLExecDirect (multiple)",
                    status=TestStatus.FAIL,
                    expected="Execute multiple statements sequentially",
                    actual="No statements executed successfully",
                    diagnostic="Driver cannot execute basic statements",
                    severity=Severity.CRITICAL,
                    duration_ms=duration,
                )
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_multiple_statements",
                function="SQLExecDirect (multiple)",
                status=TestStatus.ERROR,
                expected="Execute multiple statements sequentially",
                actual=f"Error: {str(e)}",
                diagnostic="Multiple statement execution failed",
                severity=Severity.ERROR,
                duration_ms=0,
            )
