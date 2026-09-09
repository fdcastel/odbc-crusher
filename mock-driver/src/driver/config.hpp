#pragma once

#include "common.hpp"
#include <string>
#include <unordered_map>
#include <chrono>

namespace mock_odbc {

// Driver behavior mode
enum class BehaviorMode {
    Success,    // All operations succeed
    Failure,    // Operations fail
    Random,     // Random success/failure
    Partial     // Some operations fail based on FailOn
};

// Configuration from connection string
struct DriverConfig {
    // Behavior mode
    BehaviorMode mode = BehaviorMode::Success;
    
    // Catalog preset
    std::string catalog = "Default";
    
    // Data types
    std::string types = "AllTypes";
    
    // Result set size
    int result_set_size = 100;
    
    // Functions to fail on
    std::vector<std::string> fail_on;

    // D87: the entries of `fail_on` that name nothing this driver can fail
    // on. Kept rather than dropped, so the connection can say which ones -
    // a misspelt name used to open a normal connection and inject no faults,
    // and the caller's only clue was a run where nothing went wrong.
    std::vector<std::string> unknown_fail_on;
    
    // SQLSTATE to return on failure
    std::string error_code = "42000";
    
    // Simulated latency. D27: microseconds, because `Latency=500us`
    // used to truncate to 0 - the value was stored in milliseconds and
    // every suffix that was not `ms` or `us` was read as milliseconds,
    // so `Latency=10s` meant 10 ms.
    std::chrono::microseconds latency{0};
    
    // Max connections
    int max_connections = 0;  // 0 = unlimited
    
    // Transaction mode
    std::string transaction_mode = "Autocommit";
    
    // Isolation level, from `IsolationLevel=`. I6: this existed and was
    // assigned to the connection, but nothing ever parsed a key for it, so it
    // was always the default - the silent-no-op shape D26 fixed for FailOn.
    int isolation_level = SQL_TXN_READ_COMMITTED;

    // I6: `DirtyReads=true` - behave as READ UNCOMMITTED while continuing to
    // *report* READ COMMITTED.
    //
    // The lying configuration, and it is the point of the row. An honest READ
    // UNCOMMITTED cannot fail an isolation probe: a driver showing a dirty read
    // at that level is correct, and a probe failing there would be grading
    // wrongly. A driver that promises READ COMMITTED and delivers dirty reads
    // is a real bug and the only configuration that can make such a probe
    // fail.
    bool dirty_reads = false;
    
    // Random failure probability (0-100)
    int failure_probability = 50;
    
    // Driver name for SQLGetInfo
    std::string driver_name = "Mock ODBC Driver";
    std::string driver_version = "01.00.0000";
    std::string driver_odbc_version = "03.80";
    std::string dbms_name = "MockDB";
    std::string dbms_version = "01.00.0000";
    
    // Phase 10.1: Buffer validation mode
    enum class BufferValidationMode {
        Strict,   // Strictly validate buffers (null termination, no overflow)
        Lenient   // Allow some buffer issues for testing app resilience
    };
    BufferValidationMode buffer_validation = BufferValidationMode::Strict;
    
    // Phase 10.2: Error queue management
    int error_count = 1;  // Number of diagnostic records to generate per error
    
    // Phase 10.3: State machine validation
    enum class StateCheckingMode {
        Strict,   // Strict state machine validation
        Lenient   // Lenient - allow some state violations
    };
    StateCheckingMode state_checking = StateCheckingMode::Strict;

    // Silent-corruption mode — drives the §5.1 E2E scenarios that prove the
    // §1.4 verify_rows_persisted chain actually catches a misbehaving driver.
    // Each mode keeps SQLExecute/SQLExecDirect returning SUCCESS while
    // tampering with stored or returned data, the exact shape that produced
    // the Firebird ≤3.5.0 / older MSSQL parameter-binding bugs.
    enum class SilentCorruptionMode {
        None,             // Default — store/return values verbatim
        DropInserts,      // Accept INSERT, return SUCCESS, store nothing
        MangleVarchar,    // Replace each stored string with a transformed copy
        TruncateNumeric,  // Round stored doubles to integer, lose precision
        NullAsEmpty,      // Fetch NULL char/wchar cells as empty string + ind=0
                          // (Oracle-style empty-vs-null conflation)
        MangleUnicode,    // Fetch char/wchar cells with non-ASCII codepoints
                          // collapsed to '?' (codepage-bound driver pattern)

        // D34. The modes above all target either stored rows or the character
        // fetch path, so nothing could make a *numeric* value come back wrong
        // — which is why probes asserting `SELECT 42` returns 42 had no mock
        // configuration that could fail them.
        SkewNumeric,      // Every numeric cell comes back +1, on both the
                          // bound-column path and SQLGetData
        SkewNumericBound, // Only the bound-column path is skewed; SQLGetData
                          // returns the true value, so a probe that reads the
                          // same column both ways sees them disagree

        // D83. DropInserts' sibling, one statement over. UPDATE resolves its
        // SET targets (so a bad column is still 42S22) and reports the true
        // number of matched rows, then writes none of them. The count stays
        // honest deliberately - the lie is in the data, which is what makes
        // it silent, and what a probe asserting only SQLRowCount cannot see.
        DropUpdates,

        // D86. The driver D85 found the mock being: a SQLGetData continuation
        // that outlives the result set it belongs to. Reading the same cell
        // of the same row in a second result set resumes at the first read's
        // offset, so the value comes back with its leading characters gone -
        // or, once the offset is past the end, as SQL_NO_DATA with the
        // caller's buffer untouched. Silent both ways.
        StaleGetDataOffset
    };
    SilentCorruptionMode silent_corruption = SilentCorruptionMode::None;

    // SQLNativeSql pass-through — when true, SQLNativeSql returns the input
    // string verbatim without translating any ODBC escape sequences. Drives
    // the PORT plan port 4 e2e canary; correct drivers must translate
    // `{fn ...}`, `{d ...}`, `{oj ...}`, etc. to native SQL.
    bool native_sql_pass_through = false;

    // PORT plan port 3 canary — when true, MOCK_INOUT's callback skips the
    // OUT and INOUT writebacks (output_values left empty). Mimics drivers
    // that accept the {?=CALL …} escape syntactically but only honour
    // SQL_PARAM_INPUT direction.
    bool procedures_broken_inout = false;

    // PORT plan port 6 canary — when > 0, the Nth row (1-indexed) of any
    // array-parameter execute is forced to fail with SQLSTATE 23000, while
    // the surrounding rows succeed. Drives the per-row status probe.
    int array_bind_row_fails_at = 0;

    // D35 — when true, every SQLFetch that returns a row also posts SQLSTATE
    // 01004 and returns SQL_SUCCESS_WITH_INFO instead of SQL_SUCCESS. Real
    // drivers do warn per-row (truncation, 01S07 fractional truncation, or a
    // driver-specific 01000), and an application must keep fetching. Without
    // this knob nothing could distinguish a fetch loop written as
    // `SQLFetch(h) == SQL_SUCCESS` from one written as `SQL_SUCCEEDED(...)`,
    // which is why 14 loops in the probe suite had the former.
    bool fetch_returns_warning = false;

    // PORT plan port 6 — when false, SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE)
    // with size > 1 returns HYC00 (driver doesn't support array parameter
    // execution). Lets the SKIP_UNSUPPORTED probe land here.
    bool supports_array_bind = true;

    // Check if a function should fail
    bool should_fail(const std::string& function_name) const;

    // Apply latency if configured
    void apply_latency() const;
};

// D78: which Unicode entry point is this call inside?
//
// Every `should_fail` call site names an ANSI function, because the 31 W
// wrappers all convert their arguments and delegate. So `FailOn=SQLPrepare`
// failed both widths and nothing could fail `SQLPrepareW` alone - which meant
// the tool's W-then-ANSI fallbacks had no configuration that could exercise
// them.
//
// A marker rather than a check per wrapper: `should_fail` is already the one
// place that decides, and the ANSI site below it already knows how to report a
// failure with the right SQLSTATE, the configured `ErrorCode` and the right
// handle. Thirty-one copies of that block would be thirty-one chances to get
// one of them wrong.
//
// `thread_local` for the same reason config.cpp's random generator is: which
// entry point one thread is inside means nothing to another. It also keeps new
// state off DriverConfig, which BehaviorController::config() copies on every
// call.
//
// Semantics worth stating: a W name in `FailOn` means "the call entered through
// this W entry point", not "this exact function". Each wrapper makes one
// delegate call, so the two are indistinguishable - except for
// SQLBrowseConnectW, which delegates to SQLDriverConnectW and so leaves both
// names active.
class WEntryScope {
public:
    explicit WEntryScope(const char* name) noexcept;
    ~WEntryScope() noexcept;

    WEntryScope(const WEntryScope&) = delete;
    WEntryScope& operator=(const WEntryScope&) = delete;

    // True when a call on this thread is inside a W entry point whose
    // lower-cased name is `lower_name`.
    static bool active(const std::string& lower_name) noexcept;

private:
    const char* name_;
    WEntryScope* prev_;
};

// Parse connection string into configuration
DriverConfig parse_connection_string(const std::string& conn_str);

// Parse key=value pairs from connection string
std::unordered_map<std::string, std::string> parse_connection_string_pairs(
    const std::string& conn_str);

// Get a string value from parsed pairs (case-insensitive)
std::string get_string_value(const std::unordered_map<std::string, std::string>& pairs,
                              const std::string& key, 
                              const std::string& default_value = "");

// Get an integer value from parsed pairs
int get_int_value(const std::unordered_map<std::string, std::string>& pairs,
                   const std::string& key, 
                   int default_value = 0);

} // namespace mock_odbc
