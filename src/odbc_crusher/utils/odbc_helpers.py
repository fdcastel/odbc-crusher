"""Utility functions for ODBC driver testing and workarounds."""

import pyodbc
import time
from typing import Optional, Dict, Any, Callable


def retry_connection(
    connection_string: str,
    max_retries: int = 3,
    delay_seconds: float = 1.0,
    timeout: int = 10
) -> Optional[pyodbc.Connection]:
    """
    Attempt to connect with retry logic.
    
    Some ODBC drivers report false errors when file is temporarily in use.
    This function retries the connection to work around such issues.
    
    Args:
        connection_string: ODBC connection string
        max_retries: Maximum number of retry attempts
        delay_seconds: Delay between retries
        timeout: Connection timeout in seconds
        
    Returns:
        pyodbc.Connection if successful, None otherwise
    """
    last_error = None
    
    for attempt in range(max_retries):
        try:
            conn = pyodbc.connect(connection_string, timeout=timeout)
            return conn
        except pyodbc.Error as e:
            last_error = e
            if attempt < max_retries - 1:
                time.sleep(delay_seconds)
    
    # All retries failed
    raise last_error


def safe_getinfo(conn: pyodbc.Connection, info_type: int, default: str = "Unknown") -> str:
    """
    Safely get connection info, returning default if not supported.
    
    Args:
        conn: ODBC connection
        info_type: SQL_* info type constant
        default: Default value if getinfo fails
        
    Returns:
        Info value or default
    """
    try:
        return str(conn.getinfo(info_type))
    except:
        return default


def test_query_with_fallbacks(
    cursor: pyodbc.Cursor,
    queries: list,
    description: str = "test query"
) -> tuple[bool, Optional[str], Optional[Any]]:
    """
    Try multiple query variations and return the first that works.
    
    Different databases support different SQL syntax. This function tries
    multiple variations to find one that works.
    
    Args:
        cursor: ODBC cursor
        queries: List of query strings to try
        description: Description of what's being tested
        
    Returns:
        Tuple of (success, query_used, result)
    """
    for query in queries:
        try:
            cursor.execute(query)
            result = cursor.fetchone()
            return (True, query, result)
        except:
            continue
    
    return (False, None, None)


def close_safely(closeable, name: str = "object"):
    """
    Safely close a connection/cursor, catching any errors.
    
    Some drivers may fail during close operations. This ensures
    we continue even if close fails.
    
    Args:
        closeable: Object with a close() method
        name: Name for error reporting
    """
    try:
        if closeable:
            closeable.close()
    except Exception as e:
        # Log but don't fail
        pass
