"""Connection testing and management."""

from typing import Dict, Any, Optional
import pyodbc


def check_connection(connection_string: str, verbose: bool = False) -> Dict[str, Any]:
    """
    Test ODBC connection with the provided connection string.
    
    Args:
        connection_string: ODBC connection string
        verbose: If True, gather additional connection information
        
    Returns:
        Dictionary containing:
            - success: bool indicating if connection succeeded
            - error: error message if failed, None otherwise
            - diagnostic: suggested fix if failed
            - info: additional connection info if verbose and successful
    """
    result: Dict[str, Any] = {
        "success": False,
        "error": None,
        "diagnostic": None,
        "info": {}
    }
    
    try:
        # Attempt to connect
        conn = pyodbc.connect(connection_string, timeout=10)
        
        result["success"] = True
        
        # Gather additional info if verbose
        if verbose:
            try:
                cursor = conn.cursor()
                
                # Try to get database info
                result["info"]["driver"] = conn.getinfo(pyodbc.SQL_DRIVER_NAME)
                result["info"]["driver_version"] = conn.getinfo(pyodbc.SQL_DRIVER_VER)
                result["info"]["dbms_name"] = conn.getinfo(pyodbc.SQL_DBMS_NAME)
                result["info"]["dbms_version"] = conn.getinfo(pyodbc.SQL_DBMS_VER)
                result["info"]["odbc_version"] = conn.getinfo(pyodbc.SQL_ODBC_VER)
                
                cursor.close()
            except Exception as e:
                # Don't fail if we can't get info
                result["info"]["warning"] = f"Could not retrieve all connection info: {e}"
        
        conn.close()
        
    except pyodbc.Error as e:
        result["success"] = False
        
        # Extract error details
        if len(e.args) > 1:
            sqlstate = e.args[0]
            error_message = e.args[1]
        else:
            sqlstate = "Unknown"
            error_message = str(e)
        
        result["error"] = f"[{sqlstate}] {error_message}"
        
        # Provide diagnostics based on common error codes
        result["diagnostic"] = _get_connection_diagnostic(sqlstate, error_message)
    
    except Exception as e:
        result["success"] = False
        result["error"] = f"Unexpected error: {str(e)}"
        result["diagnostic"] = "This may be a pyodbc installation issue or invalid connection string format"
    
    return result


def _get_connection_diagnostic(sqlstate: str, error_message: str) -> str:
    """
    Provide diagnostic suggestions based on ODBC error codes.
    
    Args:
        sqlstate: SQL state code
        error_message: Error message
        
    Returns:
        Diagnostic suggestion string
    """
    # Common ODBC error codes and diagnostics
    diagnostics = {
        "IM002": "Data source name not found. Check DSN name in ODBC Data Source Administrator (odbcad32.exe)",
        "28000": "Authentication failed. Verify username and password in connection string",
        "08001": "Unable to connect to data source. Check server name, network connectivity, and firewall settings",
        "08004": "Connection rejected by data source. Server may be refusing connections or database doesn't exist",
        "HY000": "General error. Check the specific error message for details",
        "HYC00": "Optional feature not implemented by the driver",
        "IM001": "Driver does not support this function",
        "IM003": "Specified driver could not be loaded. Check driver installation and architecture (32-bit vs 64-bit)",
        "S1000": "General driver error. Check driver-specific documentation",
        "42000": "Syntax error or access violation",
    }
    
    # Check if we have a diagnostic for this SQLSTATE
    if sqlstate in diagnostics:
        return diagnostics[sqlstate]
    
    # Check for common error message patterns
    error_lower = error_message.lower()
    
    if "login" in error_lower or "authentication" in error_lower:
        return "Authentication issue. Verify credentials and user permissions"
    elif "timeout" in error_lower:
        return "Connection timeout. Check network connectivity and server availability"
    elif "driver" in error_lower and "not found" in error_lower:
        return "Driver not found. Verify driver is installed and name matches exactly"
    elif "refused" in error_lower:
        return "Connection refused. Check if server is running and accepting connections on the specified port"
    
    return "Check connection string format and verify all connection parameters"


def get_connection_info(connection_string: str) -> Optional[Dict[str, str]]:
    """
    Get detailed information about the ODBC driver and database.
    
    Args:
        connection_string: ODBC connection string
        
    Returns:
        Dictionary with connection information or None if connection fails
    """
    try:
        conn = pyodbc.connect(connection_string, timeout=10)
        
        info = {
            "driver_name": conn.getinfo(pyodbc.SQL_DRIVER_NAME),
            "driver_version": conn.getinfo(pyodbc.SQL_DRIVER_VER),
            "driver_odbc_version": conn.getinfo(pyodbc.SQL_DRIVER_ODBC_VER),
            "dbms_name": conn.getinfo(pyodbc.SQL_DBMS_NAME),
            "dbms_version": conn.getinfo(pyodbc.SQL_DBMS_VER),
            "odbc_version": conn.getinfo(pyodbc.SQL_ODBC_VER),
            "server_name": conn.getinfo(pyodbc.SQL_SERVER_NAME),
            "database_name": conn.getinfo(pyodbc.SQL_DATABASE_NAME),
            "user_name": conn.getinfo(pyodbc.SQL_USER_NAME),
        }
        
        conn.close()
        return info
        
    except Exception:
        return None
