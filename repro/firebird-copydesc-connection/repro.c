/*
 * Minimal reproducer: after Firebird ODBC 3.0.1.21 faults inside SQLCopyDesc
 * on Linux, the connection it faulted on is unusable — the next ODBC call on
 * that handle never returns.
 *
 * Two separate claims, and this program separates them:
 *
 *   (1) SQLCopyDesc into an explicitly allocated descriptor faults.
 *       Reproduced here in plain C. Already fixed on the 3.5 line, so it is
 *       included as the *setup* for (2) rather than as a finding.
 *
 *   (2) Having survived that fault, the connection is dead: an ordinary call
 *       on the same handle blocks forever, while the identical call on a
 *       fresh connection returns at once.
 *
 * (2) is the one worth proving, because it is what makes one crashed test
 * category cost every category after it — odbc-crusher's runs were being
 * killed at a 570-second CI cap with thirteen categories unmeasured, and the
 * first two diagnoses blamed whichever probe happened to make the next call.
 *
 * Surviving a SIGSEGV is not something an ordinary application does, and this
 * program is explicit about that: the handler below exists only so that the
 * state *after* the fault can be observed at all. An ordinary application
 * simply dies at step (1). The finding in (2) matters to anything that does
 * carry a crash guard — a test harness, a language runtime with its own
 * signal handling, a server that traps and continues.
 *
 * No odbc-crusher code is involved: plain C against the driver manager.
 *
 * Build:  cc -Wall -Wextra -O0 -g repro.c -lodbc -o repro
 * Run:    ./repro ["<connection string>"]
 *
 * Exit codes:
 *   0  the fault happened AND the connection survived it (claim (2) refuted)
 *   1  could not connect
 *   2  bad usage
 *   3  setup did not work
 *   4  SQLCopyDesc did not fault at all (nothing to observe; claim (1) gone)
 *   5  the fault happened AND the connection was dead afterwards  <-- the finding
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

#ifdef _WIN32
#  error "This reproducer is Linux-specific: the behaviour it documents does not occur on Windows."
#endif
#include <sql.h>
#include <sqlext.h>

static const char *kDefaultConn =
    "Driver={Firebird ODBC Driver};"
    "DBNAME=localhost/3050:/tmp/crusher.fdb;"
    "UID=SYSDBA;PWD=masterkey;CHARSET=UTF8;";

/* How long to give a call before calling it hung. Ten seconds is far beyond
 * any honest answer from a local server — the healthy control below returns in
 * well under a millisecond. */
#define HANG_SECONDS 10

static sigjmp_buf g_fault;
static volatile sig_atomic_t g_faulted;
static sigjmp_buf g_alarm;
static volatile sig_atomic_t g_hung;

static void on_fault(int sig)
{
    (void)sig;
    g_faulted = 1;
    siglongjmp(g_fault, 1);
}

static void on_alarm(int sig)
{
    (void)sig;
    g_hung = 1;
    siglongjmp(g_alarm, 1);
}

static void say(const char *what)
{
    fprintf(stderr, "  -> %s\n", what);
    fflush(stderr);
}

/* One ordinary call on `dbc`, under a watchdog. Returns 1 if it came back,
 * 0 if it was still inside the driver when the alarm fired.
 *
 * SQLGetInfo(SQL_DBMS_NAME) on purpose: it is about the least a connection can
 * be asked to do, it touches no statement, and every driver answers it. If
 * *that* does not return, the connection is not merely unhealthy. */
static int call_survives(SQLHDBC dbc, const char *label)
{
    struct sigaction sa, prev;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, &prev);

    g_hung = 0;
    if (sigsetjmp(g_alarm, 1) == 0) {
        alarm(HANG_SECONDS);
        SQLCHAR buf[256] = {0};
        SQLSMALLINT len = 0;
        SQLRETURN rc = SQLGetInfo(dbc, SQL_DBMS_NAME, buf, sizeof(buf), &len);
        alarm(0);
        sigaction(SIGALRM, &prev, NULL);
        fprintf(stderr, "     %s: SQLGetInfo returned %d (\"%s\")\n",
                label, (int)rc, (char *)buf);
        fflush(stderr);
        return 1;
    }

    alarm(0);
    sigaction(SIGALRM, &prev, NULL);
    fprintf(stderr, "     %s: SQLGetInfo DID NOT RETURN after %d seconds\n",
            label, HANG_SECONDS);
    fflush(stderr);
    return 0;
}

static int connect_to(SQLHENV env, SQLHDBC *out, const char *conn_str)
{
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, env, out)))
        return 0;
    SQLCHAR out_buf[1024] = {0};
    SQLSMALLINT out_len = 0;
    return SQL_SUCCEEDED(SQLDriverConnect(*out, NULL, (SQLCHAR *)conn_str,
                                          SQL_NTS, out_buf, sizeof(out_buf),
                                          &out_len, SQL_DRIVER_NOPROMPT));
}

int main(int argc, char **argv)
{
    const char *conn_str = (argc > 1) ? argv[1] : kDefaultConn;
    if (argc > 2) {
        fprintf(stderr, "usage: %s [\"<connection string>\"]\n", argv[0]);
        return 2;
    }

    SQLHENV env = SQL_NULL_HENV;
    say("SQLAllocHandle(ENV) + SQL_OV_ODBC3");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env)))
        return 1;
    SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);

    SQLHDBC dbc = SQL_NULL_HDBC;
    say("SQLDriverConnect (the connection under test)");
    if (!connect_to(env, &dbc, conn_str)) {
        fprintf(stderr, "     could not connect\n");
        return 1;
    }

    /* Baseline: the connection answers before anything has gone wrong. Without
     * this the "it hung afterwards" observation would prove nothing about the
     * fault — it has to be shown healthy first. */
    say("SQLGetInfo on the healthy connection (baseline)");
    if (!call_survives(dbc, "baseline")) {
        fprintf(stderr, "     the connection was already unusable; nothing to test\n");
        return 3;
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    say("SQLAllocHandle(STMT) + SQLPrepare(SELECT ...)");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt)))
        return 3;
    if (!SQL_SUCCEEDED(SQLPrepare(stmt,
            (SQLCHAR *)"SELECT 1 FROM RDB$DATABASE", SQL_NTS))) {
        fprintf(stderr, "     could not prepare the SELECT\n");
        return 3;
    }

    SQLHDESC ird = SQL_NULL_HDESC;
    SQLHDESC target = SQL_NULL_HDESC;
    say("SQLGetStmtAttr(SQL_ATTR_IMP_ROW_DESC) — the source descriptor");
    if (!SQL_SUCCEEDED(SQLGetStmtAttr(stmt, SQL_ATTR_IMP_ROW_DESC,
                                      &ird, 0, NULL)))
        return 3;
    say("SQLAllocHandle(DESC) — an explicitly allocated target");
    if (!SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DESC, dbc, &target)))
        return 3;

    struct sigaction sa, prev_segv, prev_bus;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_fault;
    sigaction(SIGSEGV, &sa, &prev_segv);
    sigaction(SIGBUS, &sa, &prev_bus);

    g_faulted = 0;
    say("SQLCopyDesc(IRD -> explicit descriptor)   <-- expected to fault");
    if (sigsetjmp(g_fault, 1) == 0) {
        SQLRETURN rc = SQLCopyDesc(ird, target);
        fprintf(stderr, "     SQLCopyDesc returned %d without faulting\n",
                (int)rc);
        fflush(stderr);
    } else {
        fprintf(stderr, "     SQLCopyDesc raised a fault; caught and continuing\n");
        fflush(stderr);
    }
    sigaction(SIGSEGV, &prev_segv, NULL);
    sigaction(SIGBUS, &prev_bus, NULL);

    if (!g_faulted) {
        fprintf(stderr,
                "\nSQLCopyDesc did not fault on this build, so there is no "
                "post-fault state to observe.\n");
        return 4;
    }

    /* The claim. Same connection, same call that worked a moment ago. */
    say("SQLGetInfo on the SAME connection, after the fault");
    const int same_ok = call_survives(dbc, "after the fault");

    /* And the control: a brand-new connection, made after the fault, proves
     * the server and the driver library are both still fine — so whatever is
     * broken belongs to the connection, not to the process or the server. */
    SQLHDBC fresh = SQL_NULL_HDBC;
    say("SQLDriverConnect for a FRESH connection (the control)");
    int fresh_ok = 0;
    if (connect_to(env, &fresh, conn_str)) {
        say("SQLGetInfo on the fresh connection");
        fresh_ok = call_survives(fresh, "fresh connection");
    } else {
        fprintf(stderr, "     could not open a fresh connection either\n");
    }

    fprintf(stderr, "\n=== result ===\n");
    fprintf(stderr, "  SQLCopyDesc faulted:              yes\n");
    fprintf(stderr, "  same connection usable after:     %s\n",
            same_ok ? "yes" : "NO - the call never returned");
    fprintf(stderr, "  fresh connection usable after:    %s\n",
            fresh_ok ? "yes" : "no");
    fflush(stderr);

    if (!same_ok && fresh_ok) {
        fprintf(stderr,
                "\nThe fault leaves the connection it happened on unusable, "
                "while a new one works.\nThat is the finding: recovering from "
                "the crash is not enough, the connection has to be replaced.\n");
        return 5;
    }
    if (same_ok) {
        fprintf(stderr,
                "\nThe connection survived the fault. The claim that it does "
                "not is refuted on this build.\n");
        return 0;
    }
    fprintf(stderr,
            "\nNeither connection worked afterwards, so this does not isolate "
            "the connection as the broken thing.\n");
    return 3;
}
