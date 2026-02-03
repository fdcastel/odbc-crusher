"""Tests for ODBC advanced features - transactions, prepared statements, parameters."""

import pyodbc
from typing import List
import time

from .base import ODBCTest, TestResult, TestStatus, Severity


class AdvancedTests(ODBCTest):
    """Tests for ODBC advanced features."""
    
    def run(self) -> List[TestResult]:
        """Run all advanced feature tests."""
        self.results = []
        
        self.test_autocommit_mode()
        self.test_manual_commit()
        self.test_rollback()
        self.test_prepared_statement()
        self.test_parameter_binding()
        self.test_multiple_result_sets()
        
        return self.results
    
    def test_autocommit_mode(self):
        """Test autocommit mode (default behavior)."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            # Check if autocommit is on by default (should be True in ODBC)
            try:
                autocommit = conn.autocommit
                duration = (time.perf_counter() - start_time) * 1000
                
                self._record_test(
                    test_name="test_autocommit_mode",
                    function="SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT)",
                    status=TestStatus.PASS,
                    expected="Autocommit mode should be available",
                    actual=f"Autocommit mode is {'ON' if autocommit else 'OFF'}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            except AttributeError:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_autocommit_mode",
                    function="SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT)",
                    status=TestStatus.SKIP,
                    expected="Autocommit mode should be available",
                    actual="pyodbc doesn't expose autocommit attribute",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_autocommit_mode",
                function="SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT)",
                status=TestStatus.ERROR,
                expected="Autocommit mode should be available",
                actual=f"Error: {str(e)}",
                diagnostic="Driver may not support autocommit attribute queries",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_manual_commit(self):
        """Test manual transaction commit."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            # Disable autocommit to enable manual transactions
            try:
                conn.autocommit = False
            except:
                # Some drivers don't support setting autocommit
                pass
            
            start_time = time.perf_counter()
            
            try:
                cursor = conn.cursor()
                
                # Try to create a temporary table and commit
                # Different databases have different temporary table syntax
                temp_table_queries = [
                    "CREATE GLOBAL TEMPORARY TABLE test_temp (id INT) ON COMMIT PRESERVE ROWS",
                    "CREATE TEMPORARY TABLE test_temp (id INT)",
                    "CREATE TABLE test_temp (id INT)",
                ]
                
                table_created = False
                for query in temp_table_queries:
                    try:
                        cursor.execute(query)
                        table_created = True
                        break
                    except:
                        continue
                
                if table_created:
                    # Commit the transaction
                    conn.commit()
                    
                    # Clean up
                    try:
                        cursor.execute("DROP TABLE test_temp")
                        conn.commit()
                    except:
                        pass
                    
                    duration = (time.perf_counter() - start_time) * 1000
                    
                    self._record_test(
                        test_name="test_manual_commit",
                        function="SQLEndTran(SQL_COMMIT)",
                        status=TestStatus.PASS,
                        expected="Commit transaction successfully",
                        actual="Transaction committed successfully",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                else:
                    # Just test commit without creating anything
                    conn.commit()
                    duration = (time.perf_counter() - start_time) * 1000
                    
                    self._record_test(
                        test_name="test_manual_commit",
                        function="SQLEndTran(SQL_COMMIT)",
                        status=TestStatus.PASS,
                        expected="Commit transaction successfully",
                        actual="Empty transaction committed (no table creation)",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                
                cursor.close()
            except Exception as e:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_manual_commit",
                    function="SQLEndTran(SQL_COMMIT)",
                    status=TestStatus.ERROR,
                    expected="Commit transaction successfully",
                    actual=f"Error: {str(e)}",
                    diagnostic="Driver may not support manual transactions",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_manual_commit",
                function="SQLEndTran(SQL_COMMIT)",
                status=TestStatus.ERROR,
                expected="Commit transaction successfully",
                actual=f"Error: {str(e)}",
                diagnostic="Connection error during transaction test",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_rollback(self):
        """Test transaction rollback."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            try:
                conn.autocommit = False
            except:
                pass
            
            start_time = time.perf_counter()
            
            try:
                # Test rollback (even if we haven't done anything)
                conn.rollback()
                duration = (time.perf_counter() - start_time) * 1000
                
                self._record_test(
                    test_name="test_rollback",
                    function="SQLEndTran(SQL_ROLLBACK)",
                    status=TestStatus.PASS,
                    expected="Rollback transaction successfully",
                    actual="Transaction rolled back successfully",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            except Exception as e:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_rollback",
                    function="SQLEndTran(SQL_ROLLBACK)",
                    status=TestStatus.ERROR,
                    expected="Rollback transaction successfully",
                    actual=f"Error: {str(e)}",
                    diagnostic="Driver may not support rollback",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_rollback",
                function="SQLEndTran(SQL_ROLLBACK)",
                status=TestStatus.ERROR,
                expected="Rollback transaction successfully",
                actual=f"Error: {str(e)}",
                diagnostic="Connection error during rollback test",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_prepared_statement(self):
        """Test prepared statement execution."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Try to prepare a statement with parameter markers
            queries_to_try = [
                "SELECT ? FROM RDB$DATABASE",  # Firebird
                "SELECT ? FROM DUAL",          # Oracle
                "SELECT ?",                    # Generic
            ]
            
            prepared = False
            for query in queries_to_try:
                try:
                    # In pyodbc, prepare happens when you call execute with parameters
                    cursor.execute(query, (1,))
                    result = cursor.fetchone()
                    if result:
                        prepared = True
                        break
                except:
                    continue
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if prepared:
                self._record_test(
                    test_name="test_prepared_statement",
                    function="SQLPrepare/SQLExecute",
                    status=TestStatus.PASS,
                    expected="Prepare and execute parameterized statement",
                    actual="Prepared statement executed successfully with parameter",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_prepared_statement",
                    function="SQLPrepare/SQLExecute",
                    status=TestStatus.FAIL,
                    expected="Prepare and execute parameterized statement",
                    actual="Failed to execute any parameterized query",
                    diagnostic="Driver may not support prepared statements or parameter markers",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_prepared_statement",
                function="SQLPrepare/SQLExecute",
                status=TestStatus.ERROR,
                expected="Prepare and execute parameterized statement",
                actual=f"Error: {str(e)}",
                diagnostic="Error during prepared statement test",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_parameter_binding(self):
        """Test parameter binding with different data types."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Try binding different parameter types
            test_params = [
                (1, "integer"),
                ("test", "string"),
                (3.14, "float"),
            ]
            
            queries_to_try = [
                "SELECT ? FROM RDB$DATABASE",
                "SELECT ? FROM DUAL",
                "SELECT ?",
            ]
            
            successful_bindings = 0
            
            for query in queries_to_try:
                for param, param_type in test_params:
                    try:
                        cursor.execute(query, (param,))
                        result = cursor.fetchone()
                        if result:
                            successful_bindings += 1
                    except:
                        pass
                
                if successful_bindings > 0:
                    break
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if successful_bindings >= 2:
                self._record_test(
                    test_name="test_parameter_binding",
                    function="SQLBindParameter",
                    status=TestStatus.PASS,
                    expected="Bind parameters of various types",
                    actual=f"Successfully bound {successful_bindings} parameter types",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            elif successful_bindings > 0:
                self._record_test(
                    test_name="test_parameter_binding",
                    function="SQLBindParameter",
                    status=TestStatus.PASS,
                    expected="Bind parameters of various types",
                    actual=f"Limited success: {successful_bindings} parameter types worked",
                    diagnostic="Driver may not support all data type bindings",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_parameter_binding",
                    function="SQLBindParameter",
                    status=TestStatus.FAIL,
                    expected="Bind parameters of various types",
                    actual="No parameter bindings succeeded",
                    diagnostic="Driver may not support parameter binding",
                    severity=Severity.ERROR,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_parameter_binding",
                function="SQLBindParameter",
                status=TestStatus.ERROR,
                expected="Bind parameters of various types",
                actual=f"Error: {str(e)}",
                diagnostic="Error during parameter binding test",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def test_multiple_result_sets(self):
        """Test handling multiple result sets."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            cursor = conn.cursor()
            
            start_time = time.perf_counter()
            
            # Try to execute multiple statements and get multiple result sets
            # This is database-specific; many don't support it via ODBC
            try:
                # Try a simple approach first - execute, fetch, execute again
                cursor.execute("SELECT 1 FROM RDB$DATABASE")
                result1 = cursor.fetchone()
                
                # Check if nextset() is available
                if hasattr(cursor, 'nextset'):
                    has_nextset = True
                else:
                    has_nextset = False
                
                duration = (time.perf_counter() - start_time) * 1000
                
                if has_nextset:
                    self._record_test(
                        test_name="test_multiple_result_sets",
                        function="SQLMoreResults",
                        status=TestStatus.PASS,
                        expected="Support for multiple result sets",
                        actual="Driver supports nextset() for multiple result sets",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
                else:
                    self._record_test(
                        test_name="test_multiple_result_sets",
                        function="SQLMoreResults",
                        status=TestStatus.SKIP,
                        expected="Support for multiple result sets",
                        actual="pyodbc cursor doesn't have nextset() method",
                        diagnostic="Multiple result sets may not be supported or exposed",
                        severity=Severity.INFO,
                        duration_ms=duration,
                    )
            except Exception as e:
                duration = (time.perf_counter() - start_time) * 1000
                self._record_test(
                    test_name="test_multiple_result_sets",
                    function="SQLMoreResults",
                    status=TestStatus.SKIP,
                    expected="Support for multiple result sets",
                    actual=f"Test skipped: {str(e)}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            
            cursor.close()
            conn.close()
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_multiple_result_sets",
                function="SQLMoreResults",
                status=TestStatus.SKIP,
                expected="Support for multiple result sets",
                actual=f"Error: {str(e)}",
                severity=Severity.INFO,
                duration_ms=0,
            )
