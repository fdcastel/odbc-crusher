"""Base test class for ODBC tests."""

from typing import Dict, Any, Optional, List
from abc import ABC, abstractmethod
from enum import Enum
import time


class TestStatus(Enum):
    """Test execution status."""
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    ERROR = "ERROR"


class Severity(Enum):
    """Test result severity levels."""
    CRITICAL = "CRITICAL"  # Driver is unusable
    ERROR = "ERROR"        # Major functionality broken
    WARNING = "WARNING"    # Minor issue or edge case
    INFO = "INFO"          # Informational only


class TestResult:
    """Represents the result of a single ODBC test."""
    
    def __init__(
        self,
        test_name: str,
        function: str,
        status: TestStatus,
        expected: str,
        actual: str,
        diagnostic: Optional[str] = None,
        severity: Severity = Severity.INFO,
        duration_ms: float = 0.0,
    ):
        self.test_name = test_name
        self.function = function
        self.status = status
        self.expected = expected
        self.actual = actual
        self.diagnostic = diagnostic
        self.severity = severity
        self.duration_ms = duration_ms
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert test result to dictionary."""
        return {
            "test_name": self.test_name,
            "function": self.function,
            "status": self.status.value,
            "expected": self.expected,
            "actual": self.actual,
            "diagnostic": self.diagnostic,
            "severity": self.severity.value,
            "duration_ms": round(self.duration_ms, 2),
        }


class ODBCTest(ABC):
    """Base class for all ODBC tests."""
    
    def __init__(self, connection_string: str):
        self.connection_string = connection_string
        self.results: List[TestResult] = []
    
    @abstractmethod
    def run(self) -> List[TestResult]:
        """
        Execute all tests in this test class.
        
        Returns:
            List of TestResult objects
        """
        pass
    
    def _record_test(
        self,
        test_name: str,
        function: str,
        status: TestStatus,
        expected: str,
        actual: str,
        diagnostic: Optional[str] = None,
        severity: Severity = Severity.INFO,
        duration_ms: float = 0.0,
    ) -> TestResult:
        """
        Record a test result.
        
        Args:
            test_name: Name of the test
            function: ODBC function being tested
            status: Test status (PASS/FAIL/SKIP/ERROR)
            expected: Expected behavior/result
            actual: Actual behavior/result
            diagnostic: Diagnostic message and suggested fix
            severity: Severity level
            duration_ms: Test execution time in milliseconds
            
        Returns:
            TestResult object
        """
        result = TestResult(
            test_name=test_name,
            function=function,
            status=status,
            expected=expected,
            actual=actual,
            diagnostic=diagnostic,
            severity=severity,
            duration_ms=duration_ms,
        )
        self.results.append(result)
        return result
    
    def _time_test(self, func, *args, **kwargs):
        """
        Time a test function execution.
        
        Returns:
            Tuple of (result, duration_ms)
        """
        start_time = time.perf_counter()
        result = func(*args, **kwargs)
        end_time = time.perf_counter()
        duration_ms = (end_time - start_time) * 1000
        return result, duration_ms
