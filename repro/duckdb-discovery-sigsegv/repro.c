/*
 * Minimal reproducer: duckdb-odbc 1.5.2.0 segfaults during ODBC discovery.
 *
 * Discovery is the SQLGetInfo / SQLGetTypeInfo / SQLGetFunctions sequence that
 * a driver manager and essentially every ODBC client issues immediately after
 * connecting, before any query. odbc-crusher wraps the whole sequence in one
 * crash guard, so all it can report is "the driver crashed somewhere in
 * discovery" - which is true but not filable. This program makes the same
 * calls, one at a time, announcing each on stderr *before* it is made and
 * flushing immediately.
 *
 *   The last line printed is the call that crashed.
 *
 * No odbc-crusher code is involved: plain C against the driver manager, so a
 * DuckDB maintainer can build and run it without this repository.
 *
 * Build:  cc -Wall -Wextra -O0 -g repro.c -lodbc -o repro
 * Run:    ./repro ["<connection string>"]
 *
 * Default connection string is the one CI uses. Pass your own to test another
 * configuration - in particular `Database=<file>` versus the `:memory:`
 * default, since those take different paths through the driver.
 *
 * Exit codes:  0 all calls returned    1 could not connect    2 bad usage
 * A crash is a signal (SIGSEGV/139), not an exit code - that is the finding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bug is on Linux, but the file stays portable so it can be compiled
 * anywhere - including to check whether the crash is platform-specific.
 * <windows.h> has to precede <sql.h>: the Windows SDK's sqltypes.h uses DWORD
 * and leaves SQLLEN/SQLULEN undefined without it. Same ordering rule as
 * AGENTS.md records for this repository. */
#ifdef _WIN32
#  include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

static SQLHENV g_env = SQL_NULL_HENV;
static SQLHDBC g_dbc = SQL_NULL_HDBC;

/* Announce before acting: if the process dies inside the call, this line is
 * already on the wire and names the culprit. */
static void step(const char *what) {
    fprintf(stderr, "  -> %s\n", what);
    fflush(stderr);
}

static void banner(const char *what) {
    fprintf(stderr, "\n=== %s ===\n", what);
    fflush(stderr);
}

static void dump_diag(SQLSMALLINT type, SQLHANDLE h) {
    SQLCHAR state[6], msg[1024];
    SQLINTEGER native;
    SQLSMALLINT len;
    SQLSMALLINT rec = 1;
    while (SQLGetDiagRec(type, h, rec, state, &native, msg, sizeof(msg), &len)
           == SQL_SUCCESS) {
        fprintf(stderr, "     [%s] (%d) %s\n", state, (int)native, msg);
        rec++;
    }
    fflush(stderr);
}

struct info_entry {
    SQLUSMALLINT id;
    const char *name;
};

/* Exactly what DriverInfo::collect() asks for, in order. */
static const struct info_entry kStringInfo[] = {
    {SQL_DRIVER_NAME, "SQL_DRIVER_NAME"},
    {SQL_DRIVER_VER, "SQL_DRIVER_VER"},
    {SQL_DRIVER_ODBC_VER, "SQL_DRIVER_ODBC_VER"},
    {SQL_DBMS_NAME, "SQL_DBMS_NAME"},
    {SQL_DBMS_VER, "SQL_DBMS_VER"},
    {SQL_CATALOG_NAME, "SQL_CATALOG_NAME"},
    {SQL_PROCEDURES, "SQL_PROCEDURES"},
    {SQL_ODBC_VER, "SQL_ODBC_VER"},
    {SQL_DATABASE_NAME, "SQL_DATABASE_NAME"},
    {SQL_SERVER_NAME, "SQL_SERVER_NAME"},
    {SQL_USER_NAME, "SQL_USER_NAME"},
    {SQL_CATALOG_TERM, "SQL_CATALOG_TERM"},
    {SQL_SCHEMA_TERM, "SQL_SCHEMA_TERM"},
    {SQL_TABLE_TERM, "SQL_TABLE_TERM"},
    {SQL_PROCEDURE_TERM, "SQL_PROCEDURE_TERM"},
    {SQL_IDENTIFIER_QUOTE_CHAR, "SQL_IDENTIFIER_QUOTE_CHAR"},
};

static const struct info_entry kUIntInfo[] = {
    {SQL_SQL_CONFORMANCE, "SQL_SQL_CONFORMANCE"},
    {SQL_ODBC_INTERFACE_CONFORMANCE, "SQL_ODBC_INTERFACE_CONFORMANCE"},
    {SQL_MAX_CONCURRENT_ACTIVITIES, "SQL_MAX_CONCURRENT_ACTIVITIES"},
    {SQL_MAX_IDENTIFIER_LEN, "SQL_MAX_IDENTIFIER_LEN"},
    {SQL_STRING_FUNCTIONS, "SQL_STRING_FUNCTIONS"},
    {SQL_NUMERIC_FUNCTIONS, "SQL_NUMERIC_FUNCTIONS"},
    {SQL_TIMEDATE_FUNCTIONS, "SQL_TIMEDATE_FUNCTIONS"},
    {SQL_SYSTEM_FUNCTIONS, "SQL_SYSTEM_FUNCTIONS"},
    {SQL_CONVERT_FUNCTIONS, "SQL_CONVERT_FUNCTIONS"},
    {SQL_OJ_CAPABILITIES, "SQL_OJ_CAPABILITIES"},
    {SQL_DATETIME_LITERALS, "SQL_DATETIME_LITERALS"},
    {SQL_TIMEDATE_ADD_INTERVALS, "SQL_TIMEDATE_ADD_INTERVALS"},
    {SQL_TIMEDATE_DIFF_INTERVALS, "SQL_TIMEDATE_DIFF_INTERVALS"},
    /* The convert matrix - one SQLGetInfo per target type. */
    {SQL_CONVERT_CHAR, "SQL_CONVERT_CHAR"},
    {SQL_CONVERT_VARCHAR, "SQL_CONVERT_VARCHAR"},
    {SQL_CONVERT_LONGVARCHAR, "SQL_CONVERT_LONGVARCHAR"},
    {SQL_CONVERT_WCHAR, "SQL_CONVERT_WCHAR"},
    {SQL_CONVERT_WVARCHAR, "SQL_CONVERT_WVARCHAR"},
    {SQL_CONVERT_WLONGVARCHAR, "SQL_CONVERT_WLONGVARCHAR"},
    {SQL_CONVERT_INTEGER, "SQL_CONVERT_INTEGER"},
    {SQL_CONVERT_SMALLINT, "SQL_CONVERT_SMALLINT"},
    {SQL_CONVERT_BIGINT, "SQL_CONVERT_BIGINT"},
    {SQL_CONVERT_TINYINT, "SQL_CONVERT_TINYINT"},
    {SQL_CONVERT_DECIMAL, "SQL_CONVERT_DECIMAL"},
    {SQL_CONVERT_NUMERIC, "SQL_CONVERT_NUMERIC"},
    {SQL_CONVERT_DOUBLE, "SQL_CONVERT_DOUBLE"},
    {SQL_CONVERT_FLOAT, "SQL_CONVERT_FLOAT"},
    {SQL_CONVERT_REAL, "SQL_CONVERT_REAL"},
    {SQL_CONVERT_DATE, "SQL_CONVERT_DATE"},
    {SQL_CONVERT_TIME, "SQL_CONVERT_TIME"},
    {SQL_CONVERT_TIMESTAMP, "SQL_CONVERT_TIMESTAMP"},
    {SQL_CONVERT_BIT, "SQL_CONVERT_BIT"},
    {SQL_CONVERT_BINARY, "SQL_CONVERT_BINARY"},
    {SQL_CONVERT_VARBINARY, "SQL_CONVERT_VARBINARY"},
    {SQL_CONVERT_LONGVARBINARY, "SQL_CONVERT_LONGVARBINARY"},
    {SQL_CONVERT_GUID, "SQL_CONVERT_GUID"},
};

static void phase_getinfo_strings(void) {
    banner("Phase 1: SQLGetInfo, string values");
    for (size_t i = 0; i < sizeof(kStringInfo) / sizeof(kStringInfo[0]); i++) {
        char buf[1024];
        SQLSMALLINT len = 0;
        memset(buf, 0, sizeof(buf));
        step(kStringInfo[i].name);
        SQLRETURN rc = SQLGetInfo(g_dbc, kStringInfo[i].id, buf,
                                  (SQLSMALLINT)sizeof(buf), &len);
        if (SQL_SUCCEEDED(rc)) {
            /* len is the driver's claim, not a fact - print bounded. */
            fprintf(stderr, "     rc=%d len=%d value=\"%.*s\"\n", (int)rc,
                    (int)len, (int)sizeof(buf) - 1, buf);
        } else {
            fprintf(stderr, "     rc=%d (no value)\n", (int)rc);
        }
        fflush(stderr);
    }
}

static void phase_getinfo_uints(void) {
    banner("Phase 2: SQLGetInfo, integer values");
    for (size_t i = 0; i < sizeof(kUIntInfo) / sizeof(kUIntInfo[0]); i++) {
        SQLUINTEGER value = 0;
        step(kUIntInfo[i].name);
        SQLRETURN rc = SQLGetInfo(g_dbc, kUIntInfo[i].id, &value,
                                  sizeof(value), NULL);
        fprintf(stderr, "     rc=%d value=%lu\n", (int)rc,
                (unsigned long)value);
        fflush(stderr);
    }
}

static void phase_gettypeinfo(void) {
    banner("Phase 3: SQLGetTypeInfo(SQL_ALL_TYPES) + fetch loop");
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    step("SQLAllocHandle(SQL_HANDLE_STMT)");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, g_dbc, &stmt))) {
        fprintf(stderr, "     could not allocate a statement handle\n");
        return;
    }

    step("SQLGetTypeInfo(SQL_ALL_TYPES)");
    SQLRETURN rc = SQLGetTypeInfo(stmt, SQL_ALL_TYPES);
    if (!SQL_SUCCEEDED(rc)) {
        fprintf(stderr, "     rc=%d\n", (int)rc);
        dump_diag(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return;
    }

    /* Columns 1-6 are the ones DiscoveryTypeInfo reads per row. Crusher uses
     * SQLGetData rather than SQLBindCol, so this does too. */
    int row = 0;
    while (1) {
        char label[64];
        snprintf(label, sizeof(label), "SQLFetch (row %d)", row + 1);
        step(label);
        rc = SQLFetch(stmt);
        if (rc == SQL_NO_DATA) {
            fprintf(stderr, "     SQL_NO_DATA after %d row(s)\n", row);
            fflush(stderr);
            break;
        }
        if (!SQL_SUCCEEDED(rc)) {
            fprintf(stderr, "     rc=%d\n", (int)rc);
            dump_diag(SQL_HANDLE_STMT, stmt);
            break;
        }
        row++;

        for (SQLUSMALLINT col = 1; col <= 6; col++) {
            char buf[256];
            SQLLEN ind = 0;
            memset(buf, 0, sizeof(buf));
            snprintf(label, sizeof(label), "SQLGetData(row %d, col %u)", row,
                     (unsigned)col);
            step(label);
            SQLGetData(stmt, col, SQL_C_CHAR, buf, sizeof(buf), &ind);
        }
        if (row > 200) {
            fprintf(stderr, "     stopping after 200 rows\n");
            break;
        }
    }

    step("SQLFreeHandle(SQL_HANDLE_STMT)");
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
}

static void phase_getfunctions(void) {
    banner("Phase 4: SQLGetFunctions");
    /* The spec-mandated array size for SQL_API_ODBC3_ALL_FUNCTIONS. A driver
     * that writes past element 250 corrupts the caller's stack. */
    SQLUSMALLINT bitmap[SQL_API_ODBC3_ALL_FUNCTIONS_SIZE];
    memset(bitmap, 0, sizeof(bitmap));
    step("SQLGetFunctions(SQL_API_ODBC3_ALL_FUNCTIONS)");
    SQLRETURN rc = SQLGetFunctions(g_dbc, SQL_API_ODBC3_ALL_FUNCTIONS, bitmap);
    fprintf(stderr, "     rc=%d\n", (int)rc);
    fflush(stderr);
}

int main(int argc, char **argv) {
    const char *conn = "Driver={DuckDB Driver};";
    if (argc > 2) {
        fprintf(stderr, "usage: %s [\"<connection string>\"]\n", argv[0]);
        return 2;
    }
    if (argc == 2) conn = argv[1];

    fprintf(stderr, "connection string: %s\n", conn);
    fflush(stderr);

    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_env))) {
        fprintf(stderr, "SQLAllocHandle(ENV) failed\n");
        return 1;
    }
    SQLSetEnvAttr(g_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, g_env, &g_dbc))) {
        fprintf(stderr, "SQLAllocHandle(DBC) failed\n");
        return 1;
    }

    banner("Connecting");
    SQLCHAR out[2048];
    SQLSMALLINT out_len = 0;
    SQLRETURN rc = SQLDriverConnect(g_dbc, NULL, (SQLCHAR *)conn, SQL_NTS, out,
                                    (SQLSMALLINT)sizeof(out), &out_len,
                                    SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(rc)) {
        fprintf(stderr, "SQLDriverConnect failed rc=%d\n", (int)rc);
        dump_diag(SQL_HANDLE_DBC, g_dbc);
        return 1;
    }
    fprintf(stderr, "connected.\n");
    fflush(stderr);

    phase_getinfo_strings();
    phase_getinfo_uints();
    phase_gettypeinfo();
    phase_getfunctions();

    banner("All discovery calls returned - no crash");
    SQLDisconnect(g_dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, g_dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, g_env);
    return 0;
}
