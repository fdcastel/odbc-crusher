"""Tests for ODBC handle management functions."""

import pyodbc
from typing import List
import time

from .base import ODBCTest, TestResult, TestStatus, Severity


class HandleTests(ODBCTest):
    """Tests for ODBC handle allocation and management."""
    
    def run(self) -> List[TestResult]:
        """Run all handle management tests."""
        self.results = []
        
        self.test_environment_handle()
        self.test_connection_handle()
        self.test_statement_handle()
        self.test_handle_reuse()
        
        return self.results
    
    def test_environment_handle(self):
        """Test environment handle creation (implicit in pyodbc)."""
        try:
            start_time = time.perf_counter()
            
            # In pyodbc, environment handle is managed implicitly
            # We test this by creating and destroying a connection
            conn = pyodbc.connect(self.connection_string, timeout=10)
            conn.close()
            
            duration = (time.perf_counter() - start_time) * 1000
            
            self._record_test(
                test_name="test_environment_handle",
                function="SQLAllocHandle(SQL_HANDLE_ENV)",
                status=TestStatus.PASS,
                expected="Environment handle allocated and freed successfully",
                actual="Environment handle managed successfully by pyodbc",
                severity=Severity.INFO,
                duration_ms=duration,
            )
        except Exception as e:
            self._record_test(
                test_name="test_environment_handle",
                function="SQLAllocHandle(SQL_HANDLE_ENV)",
                status=TestStatus.ERROR,
                expected="Environment handle allocated and freed successfully",
                actual=f"Error: {str(e)}",
                diagnostic="Environment handle allocation failed - driver may not support ODBC 3.x",
                severity=Severity.CRITICAL,
                duration_ms=0,
            )
    
    def test_connection_handle(self):
        """Test connection handle allocation and freeing."""
        try:
            start_time = time.perf_counter()
            
            # Test connection handle creation
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            # Verify we can get connection info (handle is valid)
            try:
                driver_name = conn.getinfo(pyodbc.SQL_DRIVER_NAME)
                handle_valid = True
            except:
                handle_valid = False
            
            # Free the handle
            conn.close()
            
            duration = (time.perf_counter() - start_time) * 1000
            
            if handle_valid:
                self._record_test(
                    test_name="test_connection_handle",
                    function="SQLAllocHandle(SQL_HANDLE_DBC)",
                    status=TestStatus.PASS,
                    expected="Connection handle allocated and freed successfully",
                    actual="Connection handle created, verified, and freed successfully",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_connection_handle",
                    function="SQLAllocHandle(SQL_HANDLE_DBC)",
                    status=TestStatus.FAIL,
                    expected="Connection handle allocated and freed successfully",
                    actual="Connection handle created but failed validation",
                    diagnostic="Driver may not properly initialize connection handles",
                    severity=Severity.ERROR,
                    duration_ms=duration,
                )
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_connection_handle",
                function="SQLAllocHandle(SQL_HANDLE_DBC)",
                status=TestStatus.ERROR,
                expected="Connection handle allocated and freed successfully",
                actual=f"Error: {str(e)}",
                diagnostic="Connection handle allocation failed - check connection string",
                severity=Severity.CRITICAL,
                duration_ms=0,
            )
    
    def test_statement_handle(self):
        """Test statement handle allocation and freeing."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = time.perf_counter()
            
            # Allocate statement handle (cursor in pyodbc)
            cursor = conn.cursor()
            
            # Verify handle is valid by getting cursor description
            # (should be None before executing a query)
            description = cursor.description
            
            # Free statement handle
            cursor.close()
            
            # Try to create multiple cursors
            cursor1 = conn.cursor()
            cursor2 = conn.cursor()
            cursor3 = conn.cursor()
            
            cursor1.close()
            cursor2.close()
            cursor3.close()
            
            conn.close()
            
            duration = (time.perf_counter() - start_time) * 1000
            
            self._record_test(
                test_name="test_statement_handle",
                function="SQLAllocHandle(SQL_HANDLE_STMT)",
                status=TestStatus.PASS,
                expected="Statement handles allocated and freed successfully",
                actual="Created and freed 4 statement handles successfully",
                severity=Severity.INFO,
                duration_ms=duration,
            )
            
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_statement_handle",
                function="SQLAllocHandle(SQL_HANDLE_STMT)",
                status=TestStatus.ERROR,
                expected="Statement handles allocated and freed successfully",
                actual=f"Error: {str(e)}",
                diagnostic="Statement handle allocation failed - driver may have resource limits",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_handle_reuse(self):
        """Test reusing freed handles (connection pooling behavior)."""
        try:
            start_time = time.perf_counter()
            
            # Create and close connection multiple times
            for i in range(5):
                conn = pyodbc.connect(self.connection_string, timeout=10)
                cursor = conn.cursor()
                cursor.close()
                conn.close()
            
            duration = (time.perf_counter() - start_time) * 1000
            
            self._record_test(
                test_name="test_handle_reuse",
                function="SQLFreeHandle/SQLAllocHandle (repeated)",
                status=TestStatus.PASS,
                expected="Handles allocated and freed repeatedly without leaks",
                actual="Created and freed 5 connection and statement handle pairs",
                severity=Severity.INFO,
                duration_ms=duration,
            )
            
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_handle_reuse",
                function="SQLFreeHandle/SQLAllocHandle (repeated)",
                status=TestStatus.FAIL,
                expected="Handles allocated and freed repeatedly without leaks",
                actual=f"Failed after some iterations: {str(e)}",
                diagnostic="Driver may have memory leak or resource exhaustion issues",
                severity=Severity.WARNING,
                duration_ms=0,
            )
