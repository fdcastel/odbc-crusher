#include "descriptor_tests.hpp"
#include <vector>
#include <string>
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> DescriptorTests::run() {
    return {
        test_implicit_descriptors(),
        test_ird_after_prepare(),
        test_apd_fields(),
        // P6 / P7 (IMPROVEMENT_PLAN_V2), *before* test_copy_desc and
        // deliberately so. That probe faults any driver carrying the
        // SQLCopyDesc empty-descriptor defect, and S3 keeps only what ran
        // before the fault - so a probe placed after it is unobservable on
        // exactly the builds these three are meant to measure.
        test_ird_records_describe_the_columns(),
        test_ird_length_fields_are_full_width(),
        test_colattribute_type_fields_follow_odbc(),
        test_copy_desc(),
        test_auto_populate_after_exec()
    };
}

// ── P6 / P7 (IMPROVEMENT_PLAN_V2) — what the records actually say ───────
//
// Five probes in this category obtain descriptor handles and read
// `SQL_DESC_COUNT`. `SQL_DESC_CONCISE_TYPE`, `SQL_DESC_NAME`,
// `SQL_DESC_NULLABLE` and `SQL_DESC_DATETIME_INTERVAL_CODE` were never read
// anywhere in the suite, and `SQLColAttribute` was called in exactly one place,
// for `SQL_DESC_UNSIGNED`. So the seven defects behind issue #316 — records
// left at their constructor defaults, raw internal types handed out as ODBC
// ones, length fields written at half width — sat behind reads nobody made.
// The category found the crash in this area only because a crash needs no
// assertion.
//
// The check that needs no per-engine knowledge is a **cross-check**:
// `SQLDescribeCol` and `SQLColAttribute` read the statement's metadata
// directly, so they were right while the descriptor API was wrong. Two paths
// describing the same column must agree, whatever the column happens to be.

namespace {

// A result set whose columns are worth describing: an integer, some text, and
// a date, so the type, name, nullability and datetime fields all have
// something to say. Built from the fixture table when there is one and from
// literals otherwise, because a probe that cannot get a result set has nothing
// to describe.
std::vector<std::string> describable_select_variants() {
    return {
        "SELECT ID, NAME FROM CRUSHER_FIXTURE ORDER BY ID",
        "SELECT 1, 'x'",
        "SELECT 1, 'x' FROM RDB$DATABASE",
        "SELECT 1, 'x' FROM DUAL",
    };
}

const char* type_name_or_code(SQLSMALLINT t) {
    switch (t) {
        case SQL_CHAR:        return "SQL_CHAR";
        case SQL_VARCHAR:     return "SQL_VARCHAR";
        case SQL_WCHAR:       return "SQL_WCHAR";
        case SQL_WVARCHAR:    return "SQL_WVARCHAR";
        case SQL_INTEGER:     return "SQL_INTEGER";
        case SQL_SMALLINT:    return "SQL_SMALLINT";
        case SQL_BIGINT:      return "SQL_BIGINT";
        case SQL_DOUBLE:      return "SQL_DOUBLE";
        case SQL_NUMERIC:     return "SQL_NUMERIC";
        case SQL_DECIMAL:     return "SQL_DECIMAL";
        case SQL_TYPE_DATE:   return "SQL_TYPE_DATE";
        case SQL_TYPE_TIME:   return "SQL_TYPE_TIME";
        case SQL_TYPE_TIMESTAMP: return "SQL_TYPE_TIMESTAMP";
        case SQL_DATETIME:    return "SQL_DATETIME";
        case SQL_C_DEFAULT:   return "SQL_C_DEFAULT(99) - an unfilled record";
        default:              return nullptr;
    }
}

std::string describe_type(SQLSMALLINT t) {
    const char* n = type_name_or_code(t);
    return n ? std::string(n) + " (" + std::to_string(t) + ")"
             : std::to_string(t);
}

// A prepared statement that actually has columns to describe.
//
// `prepare_first_working` stops at the first variant the driver *prepares*, and
// that is not the same question. The mock prepares `SELECT ... FROM
// CRUSHER_FIXTURE` for a table it does not have - only the syntax is checked -
// and then has no columns, so the first version of these probes reported
// SQL_DESC_COUNT = 0 as a descriptor defect when the statement simply had
// nothing in it. Same lesson as the block-cursor fixture: verify what you were
// given before grading it.
bool prepare_describable(core::OdbcStatement& stmt, TestResult& r,
                         std::string& out_query) {
    for (const auto& sql : describable_select_variants()) {
        if (!SQL_SUCCEEDED(SQLPrepare(
                stmt.get_handle(),
                reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())),
                SQL_NTS))) {
            continue;
        }
        SQLSMALLINT cols = 0;
        if (SQL_SUCCEEDED(SQLNumResultCols(stmt.get_handle(), &cols)) && cols >= 2) {
            out_query = sql;
            return true;
        }
    }
    r.status = TestStatus::SKIP_INCONCLUSIVE;
    r.actual = "No dialect variant produced a prepared statement with two "
               "columns to describe";
    return false;
}

}  // namespace

// P6 — the IRD's records must describe the columns, and agree with
// SQLDescribeCol about them.
TestResult DescriptorTests::test_ird_records_describe_the_columns() {
    return run_test(
        "test_ird_records_describe_the_columns",
        "SQLGetDescField(IRD)/SQLDescribeCol",
        "After SQLPrepare, each IRD record's type, name and nullability agree "
        "with SQLDescribeCol for the same column",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 Descriptors — the IRD is auto-populated by SQLPrepare",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            std::string query;
            if (!prepare_describable(stmt, r, query)) return;

            SQLHDESC ird = SQL_NULL_HDESC;
            if (!SQL_SUCCEEDED(SQLGetStmtAttr(stmt.get_handle(),
                                              SQL_ATTR_IMP_ROW_DESC, &ird, 0, nullptr))
                || ird == SQL_NULL_HDESC) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "Could not obtain the IRD handle";
                return;
            }

            SQLSMALLINT count = 0;
            if (!SQL_SUCCEEDED(SQLGetDescField(ird, 0, SQL_DESC_COUNT,
                                               &count, sizeof(count), nullptr))
                || count < 1) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQL_DESC_COUNT on the IRD is " + std::to_string(count) +
                           " after preparing a two-column SELECT";
                return;
            }

            std::string disagreements;
            std::ostringstream detail;
            for (SQLSMALLINT col = 1; col <= count; ++col) {
                // What SQLDescribeCol says. This path reads the statement
                // metadata directly, which is why it stayed right while the
                // descriptor API did not - so it is the reference here.
                SQLCHAR name_buf[128] = {0};
                SQLSMALLINT name_len = 0, dc_type = 0, dc_scale = 0, dc_nullable = 0;
                SQLULEN dc_size = 0;
                if (!SQL_SUCCEEDED(SQLDescribeCol(stmt.get_handle(), col,
                                                  name_buf, sizeof(name_buf),
                                                  &name_len, &dc_type, &dc_size,
                                                  &dc_scale, &dc_nullable))) {
                    continue;   // nothing to compare against for this column
                }

                // What the descriptor says.
                SQLSMALLINT desc_concise = 0, desc_nullable = 0;
                SQLCHAR desc_name[128] = {0};
                SQLINTEGER desc_name_len = 0;   // SQLGetDescField takes SQLINTEGER*
                SQLGetDescField(ird, col, SQL_DESC_CONCISE_TYPE,
                                &desc_concise, sizeof(desc_concise), nullptr);
                SQLGetDescField(ird, col, SQL_DESC_NULLABLE,
                                &desc_nullable, sizeof(desc_nullable), nullptr);
                SQLGetDescField(ird, col, SQL_DESC_NAME,
                                desc_name, sizeof(desc_name), &desc_name_len);

                detail << "col " << col << ": SQLDescribeCol type="
                       << describe_type(dc_type) << " name='"
                       << reinterpret_cast<const char*>(name_buf)
                       << "'; IRD concise=" << describe_type(desc_concise)
                       << " name='" << reinterpret_cast<const char*>(desc_name)
                       << "'. ";

                if (desc_concise != dc_type) {
                    if (!disagreements.empty()) disagreements += "; ";
                    disagreements += "col " + std::to_string(col) +
                                     " type: SQLDescribeCol says " +
                                     describe_type(dc_type) +
                                     " but SQL_DESC_CONCISE_TYPE says " +
                                     describe_type(desc_concise);
                }
                if (desc_nullable != dc_nullable) {
                    if (!disagreements.empty()) disagreements += "; ";
                    disagreements += "col " + std::to_string(col) +
                                     " nullable: " + std::to_string(dc_nullable) +
                                     " vs " + std::to_string(desc_nullable);
                }
                if (name_len > 0 && desc_name[0] == '\0') {
                    if (!disagreements.empty()) disagreements += "; ";
                    disagreements += "col " + std::to_string(col) +
                                     " name: SQLDescribeCol says '" +
                                     reinterpret_cast<const char*>(name_buf) +
                                     "' but SQL_DESC_NAME is empty";
                }
            }

            r.actual = detail.str();
            if (!disagreements.empty()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual += "Disagreements: " + disagreements;
                r.suggestion =
                    "SQLPrepare sizes the implementation row descriptor, and its "
                    "records must describe the columns from that moment. A "
                    "record left at its constructor defaults answers type 99 "
                    "with an empty name and SQL_SUCCESS, so an application that "
                    "inspects the IRD instead of calling SQLDescribeCol is told "
                    "the result set has no types and no names. The two APIs "
                    "describe the same column and must agree.";
            }
        });
}

// P6 — the three length fields, at their full width.
TestResult DescriptorTests::test_ird_length_fields_are_full_width() {
    return run_test(
        "test_ird_length_fields_are_full_width",
        "SQLGetDescField(SQL_DESC_LENGTH/OCTET_LENGTH/DISPLAY_SIZE)",
        "SQL_DESC_LENGTH, SQL_DESC_OCTET_LENGTH and SQL_DESC_DISPLAY_SIZE are "
        "written across the whole of the application's SQLLEN",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 Descriptors — field widths",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            std::string query;
            if (!prepare_describable(stmt, r, query)) return;
            SQLHDESC ird = SQL_NULL_HDESC;
            if (!SQL_SUCCEEDED(SQLGetStmtAttr(stmt.get_handle(),
                                              SQL_ATTR_IMP_ROW_DESC, &ird, 0, nullptr))
                || ird == SQL_NULL_HDESC) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not obtain the IRD handle";
                return;
            }

            // Pre-fill with a pattern whose upper half is non-zero. A driver
            // that writes only 32 bits into an SQLLEN leaves the top half as it
            // found it, so the value comes back enormous - and on a 32-bit
            // build there is no upper half and this cannot fire, which is why
            // the probe reports what it measured rather than assuming.
            struct Field { SQLSMALLINT id; const char* name; };
            static const Field fields[] = {
                {SQL_DESC_LENGTH,       "SQL_DESC_LENGTH"},
                {SQL_DESC_OCTET_LENGTH, "SQL_DESC_OCTET_LENGTH"},
                {SQL_DESC_DISPLAY_SIZE, "SQL_DESC_DISPLAY_SIZE"},
            };
            const SQLLEN kPoison = static_cast<SQLLEN>(0x7F7F7F7F00000000LL);

            std::string bad;
            std::ostringstream detail;
            int checked = 0;
            for (const auto& f : fields) {
                SQLLEN value = kPoison;
                if (!SQL_SUCCEEDED(SQLGetDescField(ird, 1, f.id, &value,
                                                   sizeof(value), nullptr))) {
                    continue;
                }
                ++checked;
                detail << f.name << "=" << static_cast<long long>(value) << " ";
                if constexpr (sizeof(SQLLEN) > 4) {
                    const SQLLEN upper = value & static_cast<SQLLEN>(0xFFFFFFFF00000000LL);
                    if (upper == (kPoison & static_cast<SQLLEN>(0xFFFFFFFF00000000LL))) {
                        if (!bad.empty()) bad += ", ";
                        bad += f.name;
                    }
                }
            }

            if (checked == 0) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "The driver answered none of the three length fields "
                           "on the IRD";
                return;
            }
            r.actual = detail.str() + "(" + std::to_string(checked) + " field(s) read)";
            if (!bad.empty()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual += "; the upper 32 bits were left untouched for: " + bad;
                r.suggestion =
                    "These fields are SQLLEN / SQLULEN. Writing them through an "
                    "SQLINTEGER* leaves the upper half of the application's "
                    "variable holding whatever was there before, so the value "
                    "read back is garbage on every 64-bit build - and the call "
                    "still returns SQL_SUCCESS.";
            }
        });
}

// P7 — SQLColAttribute's type fields, and its numeric attribute's width.
TestResult DescriptorTests::test_colattribute_type_fields_follow_odbc() {
    return run_test(
        "test_colattribute_type_fields_follow_odbc",
        "SQLColAttribute(SQL_DESC_TYPE/SQL_DESC_CONCISE_TYPE)",
        "SQLColAttribute reports ODBC type codes, and writes its numeric "
        "attribute across the whole SQLLEN",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLColAttribute — type fields",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            auto attempt = execute_first_working(stmt, describable_select_variants());
            if (!attempt) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No dialect variant of a two-column SELECT executed";
                return;
            }

            // The poison pattern again: SQLColAttribute's NumericAttributePtr
            // is an SQLLEN*, and the same half-width write happens there.
            const SQLLEN kPoison = static_cast<SQLLEN>(0x7F7F7F7F00000000LL);
            SQLLEN concise = kPoison;
            SQLRETURN rc = SQLColAttribute(stmt.get_handle(), 1,
                                           SQL_DESC_CONCISE_TYPE,
                                           nullptr, 0, nullptr, &concise);
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQLColAttribute(SQL_DESC_CONCISE_TYPE) returned " +
                           first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "an error");
                return;
            }

            std::ostringstream oss;
            oss << "SQL_DESC_CONCISE_TYPE=" << static_cast<long long>(concise);

            std::string bad;
            if constexpr (sizeof(SQLLEN) > 4) {
              if ((concise & static_cast<SQLLEN>(0xFFFFFFFF00000000LL)) ==
                  (kPoison & static_cast<SQLLEN>(0xFFFFFFFF00000000LL))) {
                bad = "SQL_DESC_CONCISE_TYPE was written as 32 bits into a "
                      "64-bit SQLLEN, so its upper half is the caller's own "
                      "leftover";
              }
            }

            // SQL_DESC_TYPE for a datetime column must be SQL_DATETIME, with
            // the specific type carried in SQL_DESC_DATETIME_INTERVAL_CODE.
            // Reported rather than graded when the column is not a datetime:
            // this result set is built from whatever the engine gave us.
            SQLLEN verbose = 0;
            if (SQL_SUCCEEDED(SQLColAttribute(stmt.get_handle(), 1, SQL_DESC_TYPE,
                                              nullptr, 0, nullptr, &verbose))) {
                oss << "; SQL_DESC_TYPE=" << static_cast<long long>(verbose);
                const bool datetime_concise =
                    concise == SQL_TYPE_DATE || concise == SQL_TYPE_TIME ||
                    concise == SQL_TYPE_TIMESTAMP;
                if (datetime_concise && verbose != SQL_DATETIME) {
                    if (!bad.empty()) bad += "; ";
                    bad += "SQL_DESC_TYPE for a datetime column is " +
                           std::to_string(static_cast<long long>(verbose)) +
                           ", not SQL_DATETIME (" + std::to_string(SQL_DATETIME) + ")";
                }
            }

            // The interval code is a required field, and answering "unknown
            // field" for it is the defect, not answering 0.
            SQLLEN interval = 0;
            SQLRETURN irc = SQLColAttribute(stmt.get_handle(), 1,
                                            SQL_DESC_DATETIME_INTERVAL_CODE,
                                            nullptr, 0, nullptr, &interval);
            if (!SQL_SUCCEEDED(irc)) {
                const std::string state =
                    first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
                oss << "; SQL_DESC_DATETIME_INTERVAL_CODE rejected (" << state << ")";
                if (!bad.empty()) bad += "; ";
                bad += "SQL_DESC_DATETIME_INTERVAL_CODE is rejected as an "
                       "unknown field (" + state + ")";
            } else {
                oss << "; SQL_DESC_DATETIME_INTERVAL_CODE="
                    << static_cast<long long>(interval);
            }

            r.actual = oss.str();
            if (!bad.empty()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual += " — " + bad;
                r.suggestion =
                    "SQLColAttribute's descriptor fields are the ones the "
                    "specification names, not the driver's internal ones, and "
                    "its numeric attribute is an SQLLEN. Half-width writes and "
                    "a missing SQL_DESC_DATETIME_INTERVAL_CODE both return "
                    "SQL_SUCCESS on the calls around them, so an application "
                    "reads a wrong number with nothing to catch.";
            }
        });
}

TestResult DescriptorTests::test_implicit_descriptors() {
    return run_test(
        "test_implicit_descriptors",
        "SQLGetStmtAttr(SQL_ATTR_APP_PARAM_DESC/SQL_ATTR_IMP_ROW_DESC)",
        "Retrieve implicit APD, ARD, IPD, IRD descriptor handles",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetStmtAttr, Descriptor Handles",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            struct DescAttr {
                SQLINTEGER attr;
                const char* name;
            };

            DescAttr attrs[] = {
                {SQL_ATTR_APP_PARAM_DESC, "APD"},
                {SQL_ATTR_APP_ROW_DESC, "ARD"},
                {SQL_ATTR_IMP_PARAM_DESC, "IPD"},
                {SQL_ATTR_IMP_ROW_DESC, "IRD"}
            };

            int obtained = 0;
            std::ostringstream details;

            for (const auto& da : attrs) {
                SQLHDESC desc = SQL_NULL_HDESC;
                SQLRETURN rc = SQLGetStmtAttr(
                    stmt.get_handle(), da.attr,
                    &desc, 0, nullptr
                );

                if (SQL_SUCCEEDED(rc) && desc != SQL_NULL_HDESC) {
                    obtained++;
                    details << da.name << "=OK ";
                } else {
                    details << da.name << "=N/A ";
                }
            }

            // B1: every branch here passed or skipped, and the skip's own
            // suggestion said these handles are Core - so the probe knew the
            // answer and reported it as "unsupported" anyway. All four
            // implicit descriptors exist on every ODBC 3.x statement; a
            // driver that hands back none of them, or three of four, is
            // failing a Core requirement.
            r.actual = std::to_string(obtained) + "/4 implicit descriptor "
                       "handles obtained: " + details.str();
            if (obtained != 4) {
                r.status = TestStatus::FAIL;
                r.severity = obtained == 0 ? Severity::CRITICAL : Severity::ERR;
                r.suggestion =
                    "APD, ARD, IPD and IRD are implicitly allocated with every "
                    "statement in ODBC 3.x and SQLGetStmtAttr must return all "
                    "four. An application cannot inspect or rebind its own "
                    "parameter and row descriptors without them.";
            }
        });
}

TestResult DescriptorTests::test_ird_after_prepare() {
    return run_test(
        "test_ird_after_prepare",
        "SQLGetStmtAttr(SQL_ATTR_IMP_ROW_DESC)/SQLGetDescField",
        "IRD populated with column metadata after SQLPrepare",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLPrepare, IRD Auto-Population",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = prepare_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                // Get IRD handle
                SQLHDESC ird = SQL_NULL_HDESC;
                SQLRETURN rc = SQLGetStmtAttr(
                    stmt.get_handle(), SQL_ATTR_IMP_ROW_DESC,
                    &ird, 0, nullptr
                );

                if (SQL_SUCCEEDED(rc) && ird != SQL_NULL_HDESC) {
                    // Read SQL_DESC_COUNT from IRD
                    SQLSMALLINT count = 0;
                    rc = SQLGetDescField(
                        ird, 0, SQL_DESC_COUNT,
                        &count, sizeof(count), nullptr
                    );

                    if (SQL_SUCCEEDED(rc)) {
                        r.status = TestStatus::PASS;
                        r.actual = "IRD has " + std::to_string(count) + " column(s) after SQLPrepare";
                        success = true;
                        break;
                    }
                }            } while (false);

            if (!success) {
                // B1: the suggestion already said what the spec requires;
                // the status said it did not matter. SQL_DESC_COUNT on the
                // IRD after a successful prepare is how an application learns
                // the shape of the result set before executing.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "Could not read SQL_DESC_COUNT from the IRD after "
                           "a successful SQLPrepare";
                r.suggestion =
                    "The IRD is auto-populated with column metadata after "
                    "SQLPrepare. A driver that cannot answer SQL_DESC_COUNT "
                    "there forces an application to execute the statement "
                    "just to find out how many columns it returns.";
            }
        });
}

TestResult DescriptorTests::test_apd_fields() {
    return run_test(
        "test_apd_fields",
        "SQLGetStmtAttr(SQL_ATTR_APP_PARAM_DESC)/SQLSetDescField",
        "APD fields can be set for parameter binding",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLSetDescField, APD",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Get APD handle
            SQLHDESC apd = SQL_NULL_HDESC;
            SQLRETURN rc = SQLGetStmtAttr(
                stmt.get_handle(), SQL_ATTR_APP_PARAM_DESC,
                &apd, 0, nullptr
            );

            if (SQL_SUCCEEDED(rc) && apd != SQL_NULL_HDESC) {
                // Try to set DESC_COUNT
                SQLSMALLINT new_count = 1;
                rc = SQLSetDescField(
                    apd, 0, SQL_DESC_COUNT,
                    reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(new_count)), 0
                );

                if (SQL_SUCCEEDED(rc)) {
                    // Read it back
                    SQLSMALLINT check_count = 0;
                    rc = SQLGetDescField(
                        apd, 0, SQL_DESC_COUNT,
                        &check_count, sizeof(check_count), nullptr
                    );

                    if (SQL_SUCCEEDED(rc) && check_count == new_count) {
                        r.status = TestStatus::PASS;
                        r.actual = "APD DESC_COUNT set to 1 and verified";
                    } else {
                        // B1: this was a second PASS - "settable" - on the
                        // branch where the read-back disagreed with the write.
                        // A descriptor field that accepts a value and then
                        // reports a different one is worse than one that
                        // rejects the write: the application has no way to
                        // know its binding did not take.
                        r.status = TestStatus::FAIL;
                        r.severity = Severity::ERR;
                        r.actual = "SQLSetDescField(APD, SQL_DESC_COUNT, 1) "
                                   "succeeded but the read-back returned " +
                                   std::to_string(check_count);
                        r.suggestion =
                            "A descriptor field that was set successfully must "
                            "read back as the value that was set.";
                    }
                } else {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLSetDescField on APD not supported";
                    r.suggestion = "Descriptor field manipulation is Core conformance per ODBC 3.x";
                }
            } else {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "APD handle not available";
            }
        });
}

TestResult DescriptorTests::test_copy_desc() {
    return run_test(
        "test_copy_desc", "SQLCopyDesc",
        "Copy descriptor fields between statement handles",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLCopyDesc",
        [&](TestResult& r) {
            core::OdbcStatement stmt1(conn_);
            core::OdbcStatement stmt2(conn_);

            // Get ARD from stmt1 and stmt2
            SQLHDESC ard1 = SQL_NULL_HDESC;
            SQLHDESC ard2 = SQL_NULL_HDESC;

            SQLRETURN rc1 = SQLGetStmtAttr(stmt1.get_handle(), SQL_ATTR_APP_ROW_DESC, &ard1, 0, nullptr);
            SQLRETURN rc2 = SQLGetStmtAttr(stmt2.get_handle(), SQL_ATTR_APP_ROW_DESC, &ard2, 0, nullptr);

            if (SQL_SUCCEEDED(rc1) && SQL_SUCCEEDED(rc2) &&
                ard1 != SQL_NULL_HDESC && ard2 != SQL_NULL_HDESC) {

                SQLRETURN rc = SQLCopyDesc(ard1, ard2);

                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLCopyDesc succeeded between two statement ARDs";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLCopyDesc failed";
                    r.severity = Severity::WARNING;
                    r.suggestion = "SQLCopyDesc is a Core conformance function per ODBC 3.x SQLCopyDesc";
                }
            } else {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "ARD handles not available for copy";
            }
        });
}

TestResult DescriptorTests::test_auto_populate_after_exec() {
    return run_test(
        "test_auto_populate_after_exec",
        "SQLExecDirect/SQLNumResultCols",
        "Descriptors auto-populated after SQLExecDirect",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecDirect, IRD Auto-Population",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                // Check SQLNumResultCols (which reads from IRD)
                SQLSMALLINT num_cols = 0;
                SQLRETURN rc = SQLNumResultCols(stmt.get_handle(), &num_cols);

                if (SQL_SUCCEEDED(rc) && num_cols > 0) {
                    // Also verify column description works (reads from IRD)
                    core::GuardedBuffer<SQLCHAR> col_name(128, 0);  // D62
                    SQLSMALLINT name_len = 0, data_type = 0, nullable = 0;
                    SQLULEN col_size = 0;
                    SQLSMALLINT dec_digits = 0;

                    rc = SQLDescribeCol(stmt.get_handle(), 1,
                        col_name.data(), col_name.declared_bytes(), &name_len,
                        &data_type, &col_size, &dec_digits, &nullable);

                    if (SQL_SUCCEEDED(rc)) {
                        r.status = TestStatus::PASS;
                        r.actual = "After SQLExecDirect: " + std::to_string(num_cols) +
                                       " col(s), type=" + std::to_string(data_type);
                    } else {
                        // B1: a second PASS on the branch where SQLDescribeCol
                        // failed. The probe is about the IRD being populated
                        // after execute, and SQLNumResultCols answering while
                        // SQLDescribeCol does not means it is populated only
                        // half way - which is the defect, not a pass.
                        r.status = TestStatus::FAIL;
                        r.severity = Severity::ERR;
                        r.actual = "SQLNumResultCols reported " +
                                   std::to_string(num_cols) +
                                   " column(s) after execute, but "
                                   "SQLDescribeCol(1) failed - the IRD is "
                                   "only partly populated";
                    }
                    success = true;
                    break;
                }            } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query to test descriptor auto-population";
            }
        });
}

} // namespace odbc_crusher::tests
