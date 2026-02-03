"""Unit tests for connection module."""

import pytest
from unittest.mock import Mock, patch
import pyodbc

from odbc_crusher.connection import check_connection, _get_connection_diagnostic


def test_connection_diagnostic_dsn_not_found():
    """Test diagnostic for DSN not found error."""
    diagnostic = _get_connection_diagnostic("IM002", "Data source name not found")
    assert "DSN" in diagnostic
    assert "odbcad32" in diagnostic


def test_connection_diagnostic_auth_failed():
    """Test diagnostic for authentication failure."""
    diagnostic = _get_connection_diagnostic("28000", "Login failed")
    assert "username" in diagnostic.lower() or "password" in diagnostic.lower()


def test_connection_diagnostic_timeout():
    """Test diagnostic for timeout."""
    diagnostic = _get_connection_diagnostic("08001", "timeout")
    assert "network" in diagnostic.lower() or "connectivity" in diagnostic.lower()


@patch('odbc_crusher.connection.pyodbc.connect')
def test_connection_success(mock_connect):
    """Test successful connection."""
    # Mock successful connection
    mock_conn = Mock()
    mock_connect.return_value = mock_conn
    
    result = check_connection("DSN=test")
    
    assert result["success"] is True
    assert result["error"] is None
    mock_connect.assert_called_once()
    mock_conn.close.assert_called_once()


@patch('odbc_crusher.connection.pyodbc.connect')
def test_connection_failure(mock_connect):
    """Test failed connection."""
    # Mock connection failure
    mock_connect.side_effect = pyodbc.Error("28000", "Login failed")
    
    result = check_connection("DSN=test")
    
    assert result["success"] is False
    assert result["error"] is not None
    assert "28000" in result["error"]
    assert result["diagnostic"] is not None


@patch('odbc_crusher.connection.pyodbc.connect')
def test_connection_verbose(mock_connect):
    """Test connection with verbose output."""
    # Mock successful connection with getinfo
    mock_conn = Mock()
    mock_conn.getinfo.side_effect = lambda x: {
        pyodbc.SQL_DRIVER_NAME: "Test Driver",
        pyodbc.SQL_DBMS_NAME: "TestDB",
    }.get(x, "Unknown")
    
    mock_connect.return_value = mock_conn
    
    result = check_connection("DSN=test", verbose=True)
    
    assert result["success"] is True
    assert "driver" in result["info"]
    assert "dbms_name" in result["info"]
