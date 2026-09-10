/*
 * Minimal reproducer: Firebird ODBC 3.0.1.21 dies during SQLRowCount after a
 * parameter-array execute.
 *
 * odbc-crusher's `test_array_row_count_matches_its_claim` terminates the whole
 * process against this driver on Linux — once as SIGSEGV, once as SIGABRT with
 * glibc's `corrupted double-linked list`. Neither tells you *whose* write did
 * the damage: glibc detects a clobbered heap at a later malloc, not at the bad
 * write, so "the probe that was running" is a correlation and nothing more.
 *
 * This program is the answer to that. It makes the same ODBC calls in the same
 * order with **no odbc-crusher code in it at all**, so if it dies the same way,
 * the tool is out of the picture. And run under Valgrind it names the invalid
 * write at the instruction that performs it, with the library it is in — which
 * is the difference between "we think it is the driver" and knowing.
 *
 * Each call is announced on stderr *before* it is made and flushed
 * immediately, so:
 *
 *   The last line printed is the call that crashed.
 *
 * Build:  cc -Wall -Wextra -O0 -g repro.c -lodbc -o repro
 * Run:    ./repro ["<connection string>"]
 *   under Valgrind, which is the point:
 *         valgrind --track-origins=yes --num-callers=30 ./repro
 *
 * Exit codes:  0 every call returned    1 could not connect    2 bad usage
 *              3 setup (CREATE TABLE / prepare) did not work
 * A crash is a signal, not an exit code — that is the finding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#define SETS      5
#define NAME_LEN 21   /* 20 characters plus the terminator */

static const char *kDefaultConn =
    "Driver={Firebird ODBC Driver};"
    "DBNAME=localhost/3050:/tmp/crusher.fdb;"
    "UID=SYSDBA;PWD=masterkey;CHARSET=UTF8;";

/* Announce before doing, and flush: an unbuffered trail is the whole point. */
static void say(const char *what)
{
    fprintf(stderr, "  -> %s\n", what);
    fflush(stderr);
}

static void print_diag(SQLSMALLINT type, SQLHANDLE h, const char *when)
{
    SQLCHAR state[7] = {0}, msg[512] = {0};
    SQLINTEGER native = 0;
    SQLSMALLINT len = 0;
    if (SQL_SUCCEEDED(SQLGetDiagRec(type, h, 1, state, &native,
                                    msg, sizeof(msg), &len))) {
        fprintf(stderr, "     [%s] %s (native %ld) after %s\n",
                (char *)state, (char *)msg, (long)native, when);
    } else {
        fprintf(stderr, "     (no diagnostic record) after %s\n", when);
    }
    fflush(stderr);
}

int main(int argc, char **argv)
{
    const char *conn_str = (argc > 1) ? argv[1] : kDefaultConn;
    if (argc > 2) {
        fprintf(stderr, "usage: %s [\"<connection string>\"]\n", argv[0]);
        return 2;
    }

    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    say("SQLAllocHandle(ENV)");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env)))
        return 1;
    say("SQLSetEnvAttr(SQL_OV_ODBC3)");
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    say("SQLAllocHandle(DBC)");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc)))
        return 1;

    say("SQLDriverConnect");
    {
        SQLCHAR out[1024] = {0};
        SQLSMALLINT out_len = 0;
        SQLRETURN rc = SQLDriverConnect(dbc, NULL, (SQLCHAR *)conn_str, SQL_NTS,
                                        out, sizeof(out), &out_len,
                                        SQL_DRIVER_NOPROMPT);
        if (!SQL_SUCCEEDED(rc)) {
            print_diag(SQL_HANDLE_DBC, dbc, "SQLDriverConnect");
            return 1;
        }
    }

    /* The driver's own claim about what SQLRowCount will mean after an array
     * execute. Read first because that is the order the probe uses, and
     * because a driver that answers SQL_PARC_BATCH is promising one row count
     * per parameter set. */
    say("SQLGetInfo(SQL_PARAM_ARRAY_ROW_COUNTS)");
    {
        SQLUINTEGER claim = 0;
        if (SQL_SUCCEEDED(SQLGetInfo(dbc, SQL_PARAM_ARRAY_ROW_COUNTS,
                                     &claim, sizeof(claim), NULL))) {
            fprintf(stderr, "     driver claims %s\n",
                    claim == SQL_PARC_BATCH ? "SQL_PARC_BATCH"
                                            : "SQL_PARC_NO_BATCH");
            fflush(stderr);
        }
    }

    say("SQLAllocHandle(STMT) + CREATE TABLE");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt)))
        return 3;
    /* Ignore the result: the table may already exist from an earlier run. */
    SQLExecDirect(stmt,
                  (SQLCHAR *)"CREATE TABLE REPRO_ARRAY "
                             "(ID INTEGER, NAME VARCHAR(20))", SQL_NTS);
    SQLExecDirect(stmt, (SQLCHAR *)"DELETE FROM REPRO_ARRAY", SQL_NTS);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    stmt = SQL_NULL_HSTMT;

    say("SQLAllocHandle(STMT) for the array insert");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt)))
        return 3;

    /* Both attributes are set **before** SQLPrepare, deliberately. A driver
     * that chooses its executor at prepare time (Firebird ODBC issue #308)
     * otherwise runs a single-row insert and there is no array to get wrong. */
    say("SQLSetStmtAttr(SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN)");
    SQLSetStmtAttr(stmt, SQL_ATTR_PARAM_BIND_TYPE,
                   (SQLPOINTER)SQL_PARAM_BIND_BY_COLUMN, 0);
    say("SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE, 5)");
    if (!SQL_SUCCEEDED(SQLSetStmtAttr(stmt, SQL_ATTR_PARAMSET_SIZE,
                                      (SQLPOINTER)(SQLULEN)SETS, 0))) {
        print_diag(SQL_HANDLE_STMT, stmt, "SQLSetStmtAttr(PARAMSET_SIZE)");
        return 3;
    }

    say("SQLPrepare(INSERT INTO REPRO_ARRAY (ID, NAME) VALUES (?, ?))");
    if (!SQL_SUCCEEDED(SQLPrepare(stmt,
            (SQLCHAR *)"INSERT INTO REPRO_ARRAY (ID, NAME) VALUES (?, ?)",
            SQL_NTS))) {
        print_diag(SQL_HANDLE_STMT, stmt, "SQLPrepare");
        return 3;
    }

    /* Column-wise arrays, sized to the parameter set count. The character
     * array's element stride is NAME_LEN and that is what BufferLength says,
     * which is what the specification asks for. */
    SQLINTEGER ids[SETS];
    SQLLEN     id_ind[SETS];
    char       names[SETS][NAME_LEN];
    SQLLEN     name_ind[SETS];
    for (int i = 0; i < SETS; ++i) {
        ids[i] = 7100 + i;
        id_ind[i] = 0;
        snprintf(names[i], NAME_LEN, "rowcount-%d", i);
        name_ind[i] = SQL_NTS;
    }

    say("SQLBindParameter(1, SQL_C_SLONG -> SQL_INTEGER)");
    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                     0, 0, ids, 0, id_ind);
    say("SQLBindParameter(2, SQL_C_CHAR -> SQL_VARCHAR, BufferLength 21)");
    SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                     NAME_LEN - 1, 0, names, NAME_LEN, name_ind);

    say("SQLExecute (5 parameter sets)");
    SQLRETURN exec_rc = SQLExecute(stmt);
    fprintf(stderr, "     SQLExecute returned %d\n", (int)exec_rc);
    fflush(stderr);
    if (!SQL_SUCCEEDED(exec_rc))
        print_diag(SQL_HANDLE_STMT, stmt, "SQLExecute");

    /* This is the call odbc-crusher was making when the process died. */
    say("SQLRowCount   <-- the probe dies at or after this call");
    {
        SQLLEN row_count = -99;
        SQLRETURN rc = SQLRowCount(stmt, &row_count);
        fprintf(stderr, "     SQLRowCount returned %d, count = %lld\n",
                (int)rc, (long long)row_count);
        fflush(stderr);
    }

    say("SQLMoreResults loop (bounded at 20)");
    {
        int more = 0;
        while (SQLMoreResults(stmt) == SQL_SUCCESS) {
            SQLLEN next = 0;
            if (++more > 20) break;
            SQLRowCount(stmt, &next);
            fprintf(stderr, "     result set %d: row count %lld\n",
                    more, (long long)next);
            fflush(stderr);
        }
        fprintf(stderr, "     %d further result set(s)\n", more);
        fflush(stderr);
    }

    /* Freeing is where a clobbered heap usually surfaces: glibc checks its
     * metadata on free, so an abort here rather than earlier still points at a
     * write that happened before it. */
    say("SQLFreeHandle(STMT)   <-- a clobbered heap often surfaces here");
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    stmt = SQL_NULL_HSTMT;

    say("SQLDisconnect");
    SQLDisconnect(dbc);
    say("SQLFreeHandle(DBC/ENV)");
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    SQLFreeHandle(SQL_HANDLE_ENV, env);

    fprintf(stderr, "\nEvery call returned. No crash in this configuration.\n");
    fflush(stderr);
    return 0;
}
