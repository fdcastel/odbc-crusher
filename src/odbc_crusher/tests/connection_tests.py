"""Tests for ODBC connection functions."""

import pyodbc
from typing import List

from .base import ODBCTest, TestResult, TestStatus, Severity


class ConnectionTests(ODBCTest):
    """Tests for ODBC connection establishment and management."""
    
    def run(self) -> List[TestResult]:
        """Run all connection tests."""
        self.results = []
        
        self.test_basic_connection()
        self.test_connection_attributes()
        self.test_multiple_connections()
        
        return self.results
    
    def test_basic_connection(self):
        """Test basic connection establishment and disconnection."""
        try:
            start_time = self._get_time()
            conn = pyodbc.connect(self.connection_string, timeout=10)
            conn.close()
            duration = self._get_duration(start_time)
            
            self._record_test(
                test_name="test_basic_connection",
                function="SQLConnect/SQLDisconnect",
                status=TestStatus.PASS,
                expected="Connection established and closed successfully",
                actual="Connection established and closed successfully",
                severity=Severity.INFO,
                duration_ms=duration,
            )
        except pyodbc.Error as e:
            duration = self._get_duration(start_time)
            error_msg = str(e)
            
            self._record_test(
                test_name="test_basic_connection",
                function="SQLConnect/SQLDisconnect",
                status=TestStatus.FAIL,
                expected="Connection established and closed successfully",
                actual=f"Connection failed: {error_msg}",
                diagnostic="Verify connection string, credentials, and network connectivity",
                severity=Severity.CRITICAL,
                duration_ms=duration,
            )
    
    def test_connection_attributes(self):
        """Test getting connection attributes."""
        try:
            conn = pyodbc.connect(self.connection_string, timeout=10)
            
            start_time = self._get_time()
            
            # Try to get various connection attributes
            attributes_tested = []
            try:
                driver_name = conn.getinfo(pyodbc.SQL_DRIVER_NAME)
                attributes_tested.append(f"SQL_DRIVER_NAME: {driver_name}")
            except:
                pass
            
            try:
                dbms_name = conn.getinfo(pyodbc.SQL_DBMS_NAME)
                attributes_tested.append(f"SQL_DBMS_NAME: {dbms_name}")
            except:
                pass
            
            duration = self._get_duration(start_time)
            conn.close()
            
            if attributes_tested:
                self._record_test(
                    test_name="test_connection_attributes",
                    function="SQLGetInfo",
                    status=TestStatus.PASS,
                    expected="Retrieve connection attributes",
                    actual=f"Retrieved {len(attributes_tested)} attributes: {', '.join(attributes_tested[:2])}",
                    severity=Severity.INFO,
                    duration_ms=duration,
                )
            else:
                self._record_test(
                    test_name="test_connection_attributes",
                    function="SQLGetInfo",
                    status=TestStatus.FAIL,
                    expected="Retrieve connection attributes",
                    actual="Failed to retrieve any connection attributes",
                    diagnostic="Driver may not support SQLGetInfo properly",
                    severity=Severity.WARNING,
                    duration_ms=duration,
                )
                
        except pyodbc.Error as e:
            self._record_test(
                test_name="test_connection_attributes",
                function="SQLGetInfo",
                status=TestStatus.ERROR,
                expected="Retrieve connection attributes",
                actual=f"Error: {str(e)}",
                diagnostic="Connection failed before attributes could be tested",
                severity=Severity.ERROR,
                duration_ms=0,
            )
    
    def test_multiple_connections(self):
        """Test creating multiple simultaneous connections."""
        connections = []
        try:
            start_time = self._get_time()
            
            # Try to create 3 connections
            for i in range(3):
                conn = pyodbc.connect(self.connection_string, timeout=10)
                connections.append(conn)
            
            # Close all connections
            for conn in connections:
                conn.close()
            
            duration = self._get_duration(start_time)
            
            self._record_test(
                test_name="test_multiple_connections",
                function="SQLConnect (multiple)",
                status=TestStatus.PASS,
                expected="Create and close 3 simultaneous connections",
                actual="Successfully created and closed 3 connections",
                severity=Severity.INFO,
                duration_ms=duration,
            )
            
        except pyodbc.Error as e:
            # Close any open connections
            for conn in connections:
                try:
                    conn.close()
                except:
                    pass
            
            self._record_test(
                test_name="test_multiple_connections",
                function="SQLConnect (multiple)",
                status=TestStatus.FAIL,
                expected="Create and close 3 simultaneous connections",
                actual=f"Failed after {len(connections)} connections: {str(e)}",
                diagnostic="Driver may have connection limit or resource issues",
                severity=Severity.WARNING,
                duration_ms=0,
            )
    
    def _get_time(self):
        """Get current time for timing tests."""
        import time
        return time.perf_counter()
    
    def _get_duration(self, start_time):
        """Calculate duration in milliseconds."""
        import time
        return (time.perf_counter() - start_time) * 1000
