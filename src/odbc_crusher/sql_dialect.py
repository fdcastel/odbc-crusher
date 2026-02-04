"""SQL dialect detection and query adaptation for different databases."""

import pyodbc
from typing import Optional, List, Tuple, Any
from enum import Enum


class DatabaseType(Enum):
    """Supported database types."""
    FIREBIRD = "firebird"
    MYSQL = "mysql"
    ORACLE = "oracle"
    SQLSERVER = "sqlserver"
    POSTGRESQL = "postgresql"
    UNKNOWN = "unknown"


class SQLDialect:
    """Handles SQL dialect differences across database systems."""
    
    def __init__(self, connection: pyodbc.Connection):
        """Initialize with database connection to detect type."""
        self.connection = connection
        self.db_type = self._detect_database_type()
    
    def _detect_database_type(self) -> DatabaseType:
        """Detect the database type from connection info."""
        try:
            dbms_name = self.connection.getinfo(pyodbc.SQL_DBMS_NAME).lower()
            
            if "firebird" in dbms_name:
                return DatabaseType.FIREBIRD
            elif "mysql" in dbms_name:
                return DatabaseType.MYSQL
            elif "oracle" in dbms_name:
                return DatabaseType.ORACLE
            elif "sql server" in dbms_name or "microsoft" in dbms_name:
                return DatabaseType.SQLSERVER
            elif "postgres" in dbms_name:
                return DatabaseType.POSTGRESQL
            else:
                return DatabaseType.UNKNOWN
        except:
            return DatabaseType.UNKNOWN
    
    def build_select_query(self, select_expr: str, from_clause: Optional[str] = None) -> str:
        """
        Build a SELECT query with appropriate FROM clause for the database.
        
        Args:
            select_expr: The SELECT expression (e.g., "CAST(? AS INTEGER)")
            from_clause: Optional specific FROM clause
        
        Returns:
            Complete SELECT query appropriate for the database
        
        Examples:
            >>> dialect.build_select_query("CAST(? AS INTEGER)")
            # Firebird: "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE"
            # MySQL: "SELECT CAST(? AS INTEGER)"
            # Oracle: "SELECT CAST(? AS INTEGER) FROM DUAL"
        """
        if from_clause:
            return f"SELECT {select_expr} FROM {from_clause}"
        
        # Database-specific FROM clause
        if self.db_type == DatabaseType.FIREBIRD:
            return f"SELECT {select_expr} FROM RDB$DATABASE"
        elif self.db_type == DatabaseType.ORACLE:
            return f"SELECT {select_expr} FROM DUAL"
        elif self.db_type in (DatabaseType.MYSQL, DatabaseType.SQLSERVER, DatabaseType.POSTGRESQL):
            # These databases allow SELECT without FROM
            return f"SELECT {select_expr}"
        else:
            # Unknown - try without FROM first
            return f"SELECT {select_expr}"
    
    def get_query_variants(self, select_expr: str) -> List[str]:
        """
        Get multiple query variants to try in order of likelihood.
        
        This is useful when the detected database type is unknown,
        or as a fallback mechanism.
        
        Returns:
            List of query strings to try in order
        """
        # Start with database-specific query
        queries = [self.build_select_query(select_expr)]
        
        # Add fallbacks in common order
        if self.db_type != DatabaseType.FIREBIRD:
            queries.append(f"SELECT {select_expr} FROM RDB$DATABASE")
        if self.db_type != DatabaseType.ORACLE:
            queries.append(f"SELECT {select_expr} FROM DUAL")
        if self.db_type not in (DatabaseType.MYSQL, DatabaseType.SQLSERVER, DatabaseType.POSTGRESQL):
            queries.append(f"SELECT {select_expr}")
        
        return queries
    
    def execute_with_fallback(
        self, 
        cursor: pyodbc.Cursor, 
        select_expr: str, 
        params: Optional[Tuple[Any, ...]] = None
    ) -> Optional[Any]:
        """
        Execute a SELECT query with automatic dialect fallback.
        
        Tries the primary query for the detected database type,
        then falls back to other common variants if it fails.
        
        Args:
            cursor: Database cursor
            select_expr: SELECT expression (e.g., "CAST(? AS INTEGER)")
            params: Optional tuple of parameters
        
        Returns:
            First column of first row, or None if all queries fail
        """
        queries = self.get_query_variants(select_expr)
        
        for query in queries:
            try:
                if params:
                    cursor.execute(query, params)
                else:
                    cursor.execute(query)
                result = cursor.fetchone()
                if result:
                    return result[0]
            except:
                continue
        
        return None
    
    def get_database_name(self) -> str:
        """Get human-readable database name."""
        return self.db_type.value.title()
    
    def supports_feature(self, feature: str) -> bool:
        """
        Check if database supports a specific feature.
        
        Args:
            feature: Feature name (e.g., 'binary_types', 'transaction_savepoints')
        
        Returns:
            True if feature is supported
        """
        feature_matrix = {
            DatabaseType.FIREBIRD: {
                'binary_types': True,
                'transaction_savepoints': True,
                'named_parameters': False,  # ODBC uses ? not :param
                'multiple_result_sets': False,
            },
            DatabaseType.MYSQL: {
                'binary_types': True,
                'transaction_savepoints': True,
                'named_parameters': False,
                'multiple_result_sets': False,
            },
            DatabaseType.SQLSERVER: {
                'binary_types': True,
                'transaction_savepoints': True,
                'named_parameters': True,  # Supports @param
                'multiple_result_sets': True,
            },
            DatabaseType.ORACLE: {
                'binary_types': True,
                'transaction_savepoints': True,
                'named_parameters': True,  # Supports :param
                'multiple_result_sets': False,
            },
            DatabaseType.POSTGRESQL: {
                'binary_types': True,
                'transaction_savepoints': True,
                'named_parameters': False,  # ODBC uses ?
                'multiple_result_sets': False,
            },
        }
        
        db_features = feature_matrix.get(self.db_type, {})
        return db_features.get(feature, False)
