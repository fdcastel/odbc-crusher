"""Unit tests for test framework base classes."""

import pytest
from odbc_crusher.tests.base import (
    ODBCTest,
    TestResult,
    TestStatus,
    Severity,
)


def test_test_result_creation():
    """Test creating a TestResult object."""
    result = TestResult(
        test_name="test_example",
        function="SQLConnect",
        status=TestStatus.PASS,
        expected="Success",
        actual="Success",
        severity=Severity.INFO,
        duration_ms=10.5,
    )
    
    assert result.test_name == "test_example"
    assert result.function == "SQLConnect"
    assert result.status == TestStatus.PASS
    assert result.severity == Severity.INFO
    assert result.duration_ms == 10.5


def test_test_result_to_dict():
    """Test converting TestResult to dictionary."""
    result = TestResult(
        test_name="test_example",
        function="SQLConnect",
        status=TestStatus.FAIL,
        expected="Success",
        actual="Failed",
        diagnostic="Check credentials",
        severity=Severity.ERROR,
        duration_ms=15.25,
    )
    
    result_dict = result.to_dict()
    
    assert result_dict["test_name"] == "test_example"
    assert result_dict["status"] == "FAIL"
    assert result_dict["severity"] == "ERROR"
    assert result_dict["diagnostic"] == "Check credentials"
    assert result_dict["duration_ms"] == 15.25


def test_test_status_enum():
    """Test TestStatus enum values."""
    assert TestStatus.PASS.value == "PASS"
    assert TestStatus.FAIL.value == "FAIL"
    assert TestStatus.SKIP.value == "SKIP"
    assert TestStatus.ERROR.value == "ERROR"


def test_severity_enum():
    """Test Severity enum values."""
    assert Severity.CRITICAL.value == "CRITICAL"
    assert Severity.ERROR.value == "ERROR"
    assert Severity.WARNING.value == "WARNING"
    assert Severity.INFO.value == "INFO"
