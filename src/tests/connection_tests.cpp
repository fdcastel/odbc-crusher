#include "connection_tests.hpp"
#include "core/guarded_buffer.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> ConnectionTests::run() {
    return {
        test_connection_info(),
        test_connection_string_format(),
        test_multiple_statements(),
        test_connection_attributes(),
        test_connection_timeout(),
        test_connection_pooling(),
        test_reused_connection_starts_clean(),   // I8
    };
}

TestResult ConnectionTests::test_connection_info() {
    return run_test(
        "test_connection_info", "SQLGetInfo",
        "Can retrieve connection information",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetInfo",
        [&](TestResult& r) {
            // D61: guarded. ASan caught unixODBC running strlen off the end
            // of the sibling buffer below - READ of size 258 - once the mock
            // was told to omit its terminator, which is the one driver
            // behaviour this probe is most likely to meet in the field. The
            // length declared to SQLGetInfo is unchanged.
            constexpr size_t kDbNameCapacity = 256;
            core::GuardedBuffer<char> dbname(kDbNameCapacity, '\0');
            SQLSMALLINT dbname_len = 0;

            SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_DATABASE_NAME,
                                       dbname.data(),
                                       static_cast<SQLSMALLINT>(kDbNameCapacity),
                                       &dbname_len);

            if (SQL_SUCCEEDED(ret)) {
                // A3: dbname_len is the total available length, and
                // SQL_SUCCEEDED accepts the 01004 that comes with truncation.
                // Firebird's SQL_DATABASE_NAME is a filesystem path, so >255
                // is realistic rather than theoretical.
                const auto db = bounded_string(dbname.data(), kDbNameCapacity,
                                               dbname_len);
                r.actual = "Database name: " + db.value;
                if (db.truncated) {
                    r.actual += " (truncated at " + std::to_string(kDbNameCapacity - 1) +
                                " bytes; driver reported " +
                                std::to_string(dbname_len) + ")";
                }
            } else {
                // B1: was SKIP_INCONCLUSIVE, and the suggestion even said
                // "SQLGetInfo succeeded" on the branch where it had not.
                // SQLGetInfo is Core and SQL_DATABASE_NAME is one of the
                // info types every driver must answer; a driver that cannot
                // is a finding. (An *empty* name is fine and lands in the
                // branch above - this is the call failing outright.)
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLGetInfo(SQL_DATABASE_NAME) returned " +
                           std::to_string(ret) + " [" +
                           first_sqlstate(SQL_HANDLE_DBC, conn_.get_handle(),
                                          "no diagnostic") + "]";
                r.suggestion =
                    "SQL_DATABASE_NAME is a Core information type. Returning "
                    "an empty string is allowed; failing the call is not.";
            }
        });
}

TestResult ConnectionTests::test_connection_string_format() {
    return run_test(
        "test_connection_string_format", "SQLGetInfo(SQL_DRIVER_NAME)",
        "Connection is active and driver name is retrievable",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetInfo",
        [&](TestResult& r) {
            // D61: guarded — this is the call ASan flagged.
            constexpr size_t kDriverNameCapacity = 256;
            core::GuardedBuffer<char> driver_name(kDriverNameCapacity, '\0');
            SQLSMALLINT driver_name_len = 0;

            SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_DRIVER_NAME,
                                       driver_name.data(),
                                       static_cast<SQLSMALLINT>(kDriverNameCapacity),
                                       &driver_name_len);
            core::check_odbc_result(ret, SQL_HANDLE_DBC, conn_.get_handle(),
                                    "SQLGetInfo(SQL_DRIVER_NAME)");

            // A3: see test_connection_info above.
            const auto name = bounded_string(driver_name.data(),
                                             kDriverNameCapacity, driver_name_len);
            r.actual = "Driver: " + name.value;
            if (name.truncated) {
                r.actual += " (truncated; driver reported " +
                            std::to_string(driver_name_len) + " bytes)";
            }
        },
        TestStatus::FAIL);
}

TestResult ConnectionTests::test_multiple_statements() {
    return run_test(
        "test_multiple_statements", "SQLAllocHandle(STMT)",
        "Can allocate multiple statement handles on one connection",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLAllocHandle",
        [&](TestResult& r) {
            core::OdbcStatement stmt1(conn_);
            core::OdbcStatement stmt2(conn_);
            core::OdbcStatement stmt3(conn_);
            r.actual = "Successfully allocated 3 statement handles";
        },
        TestStatus::FAIL);
}

TestResult ConnectionTests::test_connection_attributes() {
    return run_test(
        "test_connection_attributes", "SQLGetConnectAttr",
        "Can get/set connection attributes",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetConnectAttr",
        [&](TestResult& r) {
            SQLUINTEGER autocommit = 0;
            SQLRETURN ret = SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                              &autocommit, 0, nullptr);
            if (SQL_SUCCEEDED(ret)) {
                std::ostringstream oss;
                oss << "Autocommit: " << (autocommit == SQL_AUTOCOMMIT_ON ? "ON" : "OFF");
                r.actual = oss.str();
            } else {
                // B1: SQLGetConnectAttr is Core and SQL_ATTR_AUTOCOMMIT is
                // not optional - every connection has one, and the whole
                // transaction category depends on being able to read it.
                // A14 is what happens downstream when this call fails and
                // nobody notices.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT) returned " +
                           std::to_string(ret) + " [" +
                           first_sqlstate(SQL_HANDLE_DBC, conn_.get_handle(),
                                          "no diagnostic") + "]";
                r.suggestion =
                    "SQL_ATTR_AUTOCOMMIT is a Core connection attribute and "
                    "must be readable. An application that cannot read it "
                    "cannot restore it, and will leave the connection in "
                    "whatever mode the last operation chose.";
            }
        });
}

TestResult ConnectionTests::test_connection_timeout() {
    return run_test(
        "test_connection_timeout", "SQLGetConnectAttr(SQL_ATTR_CONNECTION_TIMEOUT)",
        "Can query connection timeout setting",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetConnectAttr",
        [&](TestResult& r) {
            SQLUINTEGER timeout = 0;
            SQLRETURN ret = SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_CONNECTION_TIMEOUT,
                                              &timeout, 0, nullptr);
            if (SQL_SUCCEEDED(ret)) {
                // B1/B2: any timeout is legal, including 0 for "no timeout",
                // so there is nothing here to grade - the probe records what
                // the driver is configured with. Scoring it as PASS gave a
                // free point to every driver that answers the attribute.
                r.status = TestStatus::INFORMATIONAL;
                std::ostringstream oss;
                oss << "Connection timeout: " << timeout << " seconds";
                r.actual = oss.str();
            } else {
                r.actual = "Connection timeout attribute not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "SQL_ATTR_CONNECTION_TIMEOUT is an optional connection attribute";
            }
        });
}

TestResult ConnectionTests::test_connection_pooling() {
    return run_test(
        "test_connection_pooling", "SQLGetEnvAttr/SQLSetEnvAttr(SQL_ATTR_CONNECTION_POOLING)",
        "Can query/set connection pooling mode",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetEnvAttr",
        [&](TestResult& r) {
            // Note: SQL_ATTR_CONNECTION_POOLING must be set BEFORE allocating environment
            // For this test, we just query the current setting
            SQLUINTEGER pooling_mode = 0;
            SQLINTEGER indicator = 0;
            SQLHENV henv = conn_.get_environment().get();
            SQLRETURN ret = SQLGetEnvAttr(henv, SQL_ATTR_CONNECTION_POOLING,
                                          &pooling_mode, sizeof(pooling_mode), &indicator);
            if (SQL_SUCCEEDED(ret)) {
                std::ostringstream oss;
                oss << "Connection pooling mode: ";
                switch (pooling_mode) {
                    case SQL_CP_OFF:
                        oss << "OFF (SQL_CP_OFF)";
                        break;
                    case SQL_CP_ONE_PER_DRIVER:
                        oss << "ONE_PER_DRIVER (SQL_CP_ONE_PER_DRIVER)";
                        break;
                    case SQL_CP_ONE_PER_HENV:
                        oss << "ONE_PER_HENV (SQL_CP_ONE_PER_HENV)";
                        break;
                    default:
                        oss << "Unknown (" << pooling_mode << ")";
                        break;
                }
                // B1/B2: every pooling mode is legal, and pooling is the
                // driver manager's setting rather than the driver's, so
                // there is nothing about the driver to grade here.
                r.status = TestStatus::INFORMATIONAL;
                r.actual = oss.str();
            } else {
                // Many drivers don't support connection pooling query
                r.actual = "Connection pooling not supported by driver";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Connection pooling is an optional ODBC feature";
            }
        });
}

// I8 / PORT 10.B — what a reused connection hands to the next caller.
//
// Connection pooling gives the *same* handle to the next application, so
// anything the previous one left behind becomes its problem: an open
// transaction, autocommit switched off, a changed isolation level. A driver
// that pools without resetting turns one careless caller into a bug in
// somebody else's code.
//
// This deliberately does not turn pooling on: SQL_ATTR_CONNECTION_POOLING is
// an environment attribute that must be set before the environment handle
// exists, and the environment here is already open. What it checks instead is
// the property pooling depends on — that a fresh connection to the same
// target starts in the documented default state, whoever used it last.
TestResult ConnectionTests::test_reused_connection_starts_clean() {
    return run_test(
        "test_reconnected_handle_is_usable", "SQLDisconnect/SQLConnect",
        "A reconnected handle works, with no session state carried over",
        Severity::ERR, ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLDisconnect, SQLConnect",
        [&](TestResult& r) {
            auto sibling = open_sibling_connection();
            if (!sibling) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No connection string available to open a second "
                           "connection with";
                return;
            }

            // Leave it in the state a pooled connection is most often
            // returned in: autocommit off, with work in flight.
            SQLRETURN rc = SQLSetConnectAttr(
                sibling->get_handle(), SQL_ATTR_AUTOCOMMIT,
                reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver declined SQL_AUTOCOMMIT_OFF, so it cannot "
                           "be left in that state to begin with";
                return;
            }

            RoundTripTableGuard table(conn_, "ODBC_CRUSHER_REUSE_CONN",
                                      "INTEGER");
            if (!table.ok()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not create a table to test with: " +
                           table.last_error();
                return;
            }

            {
                core::OdbcStatement stmt(*sibling);
                stmt.execute("INSERT INTO " + table.name() + " VALUES (7777)");
            }
            // SQLDisconnect may legitimately refuse while this is open — see
            // test_disconnect_with_open_transaction, which is the probe for
            // that. This one is about what comes back afterwards, so end the
            // transaction the documented way first.
            SQLEndTran(SQL_HANDLE_DBC, sibling->get_handle(), SQL_ROLLBACK);

            sibling->disconnect();
            sibling->connect(connection_string());

            // The graded question: is the handle actually usable again? A
            // driver that pools a connection badly hands back one that is
            // open but wedged — every statement on it failing, or failing
            // only for the second caller.
            try {
                core::OdbcStatement stmt(*sibling);
                stmt.execute("INSERT INTO " + table.name() + " VALUES (7778)");
                SQLEndTran(SQL_HANDLE_DBC, sibling->get_handle(), SQL_COMMIT);
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = std::string("Reconnected handle could not execute: ") +
                           e.what();
                r.suggestion = "A handle that reconnects must be as usable as "
                               "a new one. Anything less makes connection "
                               "pooling unsafe.";
                return;
            }

            // And the rolled-back row must not have come back with it.
            core::OdbcStatement check(conn_);
            check.execute("SELECT COUNT(*) FROM " + table.name() +
                          " WHERE ID = 7777");
            SQLINTEGER stale = -1;
            SQLLEN ind = 0;
            if (check.fetch() &&
                SQL_SUCCEEDED(SQLGetData(check.get_handle(), 1, SQL_C_SLONG,
                                         &stale, 0, &ind)) && stale != 0) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "A rolled-back row reappeared after reconnecting "
                           "(found " + std::to_string(stale) + ")";
                r.suggestion = "Reconnecting must not resurrect session state "
                               "the previous connection discarded";
                return;
            }

            // Whether SQL_ATTR_AUTOCOMMIT survived is reported and
            // deliberately not graded. Connection attributes belong to the
            // handle, not the session, and persist until changed or the
            // handle is freed — so a driver that keeps the caller's setting
            // across a reconnect is right, and the first version of this
            // probe was wrong to fail it.
            SQLUINTEGER autocommit = 0;
            const bool readable = SQL_SUCCEEDED(SQLGetConnectAttr(
                sibling->get_handle(), SQL_ATTR_AUTOCOMMIT, &autocommit, 0,
                nullptr));
            r.status = TestStatus::PASS;
            r.actual = "Reconnected handle executes and commits normally; "
                       "no discarded work came back. SQL_ATTR_AUTOCOMMIT "
                       "after reconnect: " +
                       (readable ? (autocommit == SQL_AUTOCOMMIT_ON
                                        ? std::string("ON (reset)")
                                        : std::string("OFF (retained, which "
                                                      "the handle contract "
                                                      "allows)"))
                                 : std::string("not readable"));
        });
}

} // namespace odbc_crusher::tests
