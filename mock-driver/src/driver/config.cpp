#include "config.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <thread>
#include <random>

namespace mock_odbc {

namespace {

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Clamp an integer knob parsed from the connection string - D9.
//
// Every one of these values comes straight from a caller-supplied string and
// is used as a loop bound or an allocation size. `ResultSetSize=-1` reached
// `std::vector::reserve()`, where the int converts to SIZE_MAX and throws
// std::length_error; `ResultSetSize=500000000` threw std::bad_alloc. Neither
// had a handler between the throw and odbc32.dll. entry_guard.hpp now stops
// such a throw taking the host process down; clamping here stops it being
// thrown at all, which is the better outcome - an out-of-range knob is a typo
// in a test's connection string, not a condition worth failing the call over.
int clamp_int(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

} // anonymous namespace

namespace {

// D78: the active W entry points on this thread, innermost first.
//
// An intrusive stack of stack-allocated nodes - no allocation, and correct
// under nesting (SQLBrowseConnectW delegates to SQLDriverConnectW) and under
// exception unwinding, because the local is destroyed before MOCK_ENTRY_CATCH
// runs.
thread_local WEntryScope* g_w_entry = nullptr;

}  // namespace

WEntryScope::WEntryScope(const char* name) noexcept
    : name_(name), prev_(g_w_entry) {
    g_w_entry = this;
}

WEntryScope::~WEntryScope() noexcept {
    g_w_entry = prev_;
}

bool WEntryScope::active(const std::string& lower_name) noexcept {
    for (const WEntryScope* s = g_w_entry; s; s = s->prev_) {
        if (to_lower(s->name_) == lower_name) return true;
    }
    return false;
}

bool DriverConfig::should_fail(const std::string& function_name) const {
    // D26: `FailOn` names specific functions, so it is a per-function
    // override rather than a property of a mode - and it used to be read only
    // in Mode=Partial. Mode=Success is the default, so
    // `Driver={Mock ODBC Driver};FailOn=SQLTables` silently did nothing and a
    // caller had no way to know. Checked first, whatever the mode.
    if (!fail_on.empty()) {
        const std::string lower_name = to_lower(function_name);
        for (const auto& f : fail_on) {
            const std::string wanted = to_lower(f);
            if (wanted == lower_name) return true;
            // D78: ...or it names the W entry point this call arrived through.
            // An ANSI name still fails both widths, because every W call
            // reaches this same site through its delegate.
            if (WEntryScope::active(wanted)) return true;
        }
        // A named list that does not name this function means "not this one",
        // which is the whole point of naming it - so Partial says no here
        // rather than falling through to its own loop.
        if (mode == BehaviorMode::Partial) return false;
    }

    switch (mode) {
        case BehaviorMode::Success:
            return false;
            
        case BehaviorMode::Failure:
            return true;
            
        case BehaviorMode::Random: {
            // D20: this was a function-local `static std::mt19937` mutated by
            // every entry point that calls should_fail on its own *copy* of
            // the config - so it bypassed BehaviorController's mutex entirely
            // and two connections advanced the same 624-word generator state
            // concurrently. The observable symptom of that race is a
            // correlated failure pattern rather than a crash, which for a
            // fault-injection knob is worse than an outright break.
            //
            // thread_local rather than a mutex: the sequence carries no
            // meaning across threads, and per-thread is what a caller of
            // Mode=Random actually wants.
            thread_local std::mt19937 gen(std::random_device{}());
            std::uniform_int_distribution<> dis(1, 100);
            return dis(gen) <= failure_probability;
        }
        
        case BehaviorMode::Partial: {
            std::string lower_name = to_lower(function_name);
            for (const auto& f : fail_on) {
                if (to_lower(f) == lower_name) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

void DriverConfig::apply_latency() const {
    if (latency.count() > 0) {
        std::this_thread::sleep_for(latency);
    }
}

namespace {

// Strip the quoting braces from a value and un-double any escaped `}` inside
// it - D27. `{a}}b}` is the ODBC spelling of the value `a}b`.
std::string unbrace_value(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '{' || raw.back() != '}') return raw;
    const std::string inner = raw.substr(1, raw.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
        out += inner[i];
        if (inner[i] == '}' && i + 1 < inner.size() && inner[i + 1] == '}') ++i;
    }
    return out;
}

} // anonymous namespace

std::unordered_map<std::string, std::string> parse_connection_string_pairs(
    const std::string& conn_str) {
    std::unordered_map<std::string, std::string> result;
    
    // D27: `in_braces` used to be a bool set by a `{` *anywhere* in the value,
    // so `Key=a{b;Mode=Failure` swallowed the separator and Mode was never
    // parsed, and an unterminated `{` absorbed the whole rest of the string.
    // ODBC braces only quote a value when the `{` is the first character of
    // that value, and `}}` inside is an escaped `}`.
    std::string current;
    bool in_braces = false;
    bool value_started = false;   // have we seen the `=` of this pair yet?

    for (size_t i = 0; i < conn_str.size(); ++i) {
        const char c = conn_str[i];
        if (c == '=' && !in_braces && !value_started) {
            value_started = true;
            current += c;
            continue;
        }
        if (c == '{' && !in_braces && value_started
            && trim(current.substr(current.find('=') + 1)).empty()) {
            // A `{` at the start of the value opens a quoted value - but
            // only if it is ever closed. An unterminated one used to absorb
            // the whole rest of the connection string, so every pair after it
            // was lost; treating it as an ordinary character loses one
            // malformed value instead of all of them.
            bool closes = false;
            for (size_t j = i + 1; j < conn_str.size(); ++j) {
                if (conn_str[j] != '}') continue;
                if (j + 1 < conn_str.size() && conn_str[j + 1] == '}') {
                    ++j;          // an escaped `}` is not the closer
                    continue;
                }
                closes = true;
                break;
            }
            if (closes) {
                in_braces = true;
                current += c;
                continue;
            }
        }
        if (c == '}' && in_braces) {
            if (i + 1 < conn_str.size() && conn_str[i + 1] == '}') {
                // `}}` is an escaped `}` and does not close the value.
                current += "}}";
                ++i;
                continue;
            }
            in_braces = false;
            current += c;
            continue;
        }
        if (c == ';' && !in_braces) {
            value_started = false;
            // Parse key=value
            auto eq_pos = current.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = trim(current.substr(0, eq_pos));
                std::string value = trim(current.substr(eq_pos + 1));
                
                value = unbrace_value(value);
                
                result[to_lower(key)] = value;
            }
            current.clear();
        } else {
            current += c;
        }
    }
    
    // Handle last pair (no trailing semicolon)
    if (!current.empty()) {
        auto eq_pos = current.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = trim(current.substr(0, eq_pos));
            std::string value = trim(current.substr(eq_pos + 1));
            
            value = unbrace_value(value);
            if (false) {
            }
            
            result[to_lower(key)] = value;
        }
    }
    
    return result;
}

std::string get_string_value(const std::unordered_map<std::string, std::string>& pairs,
                              const std::string& key,
                              const std::string& default_value) {
    auto it = pairs.find(to_lower(key));
    if (it != pairs.end()) {
        return it->second;
    }
    return default_value;
}

int get_int_value(const std::unordered_map<std::string, std::string>& pairs,
                   const std::string& key,
                   int default_value) {
    auto it = pairs.find(to_lower(key));
    if (it != pairs.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

DriverConfig parse_connection_string(const std::string& conn_str) {
    DriverConfig config;
    
    auto pairs = parse_connection_string_pairs(conn_str);
    
    // Mode
    std::string mode_str = to_lower(get_string_value(pairs, "mode", "success"));
    if (mode_str == "failure" || mode_str == "fail") {
        config.mode = BehaviorMode::Failure;
    } else if (mode_str == "random") {
        config.mode = BehaviorMode::Random;
    } else if (mode_str == "partial") {
        config.mode = BehaviorMode::Partial;
    } else {
        config.mode = BehaviorMode::Success;
    }
    
    // Catalog
    config.catalog = get_string_value(pairs, "catalog", "Default");
    
    // Types
    config.types = get_string_value(pairs, "types", "AllTypes");
    
    // Result set size. The upper bound is deliberately generous but finite:
    // a million generated rows is far past any plausible test and still well
    // inside what the process can allocate.
    config.result_set_size =
        clamp_int(get_int_value(pairs, "resultsetsize", 100), 0, 1000000);
    
    // FailOn - comma-separated list of functions
    std::string fail_on_str = get_string_value(pairs, "failon", "");
    if (!fail_on_str.empty()) {
        std::istringstream iss(fail_on_str);
        std::string func;
        while (std::getline(iss, func, ',')) {
            config.fail_on.push_back(trim(func));
        }
    }
    
    // Error code
    config.error_code = get_string_value(pairs, "errorcode", "42000");
    
    // Latency
    std::string latency_str = get_string_value(pairs, "latency", "0");
    if (!latency_str.empty()) {
        int value = 0;
        try {
            value = std::stoi(latency_str);
        } catch (...) {}
        
        // D27: any suffix that was not `ms` or `us` fell through to
        // milliseconds, so `Latency=10s` meant 10 ms - three orders of
        // magnitude out - and `Latency=500us` truncated to 0. Units are
        // matched at the end of the string, longest first, and the value is
        // kept in microseconds so a sub-millisecond latency survives.
        const std::string suffix = [&] {
            std::string t = to_lower(latency_str);
            while (!t.empty() && (std::isdigit(static_cast<unsigned char>(t.front()))
                                  || t.front() == '-' || t.front() == '+'
                                  || std::isspace(static_cast<unsigned char>(t.front())))) {
                t.erase(t.begin());
            }
            return trim(t);
        }();

        if (suffix == "us") {
            config.latency = std::chrono::microseconds(value);
        } else if (suffix == "s") {
            config.latency = std::chrono::microseconds(
                static_cast<long long>(value) * 1000000);
        } else {
            // "ms", empty, or anything unrecognised: milliseconds, which is
            // what the README documents as the default unit.
            config.latency = std::chrono::microseconds(
                static_cast<long long>(value) * 1000);
        }
    }
    
    // Max connections (0 = unlimited). Capped at the SQLUSMALLINT range,
    // because that is what SQLGetInfo(SQL_MAX_DRIVER_CONNECTIONS) can report.
    config.max_connections =
        clamp_int(get_int_value(pairs, "maxconnections", 0), 0, 0xFFFF);
    
    // Transaction mode
    config.transaction_mode = get_string_value(pairs, "transactionmode", "Autocommit");
    
    // Failure probability
    // A percentage; anything outside 0-100 would make Mode=Random either
    // never fail or always fail, silently.
    config.failure_probability =
        clamp_int(get_int_value(pairs, "failureprobability", 50), 0, 100);
    
    // Phase 10.1: Buffer validation mode
    std::string buffer_val_str = to_lower(get_string_value(pairs, "buffervalidation", "strict"));
    if (buffer_val_str == "lenient") {
        config.buffer_validation = DriverConfig::BufferValidationMode::Lenient;
    } else {
        config.buffer_validation = DriverConfig::BufferValidationMode::Strict;
    }
    
    // Phase 10.2: Error count
    config.error_count = get_int_value(pairs, "errorcount", 1);
    if (config.error_count < 1) config.error_count = 1;
    if (config.error_count > 10) config.error_count = 10;  // Max 10 errors
    
    // Phase 10.3: State checking mode
    std::string state_check_str = to_lower(get_string_value(pairs, "statechecking", "strict"));
    if (state_check_str == "lenient") {
        config.state_checking = DriverConfig::StateCheckingMode::Lenient;
    } else {
        config.state_checking = DriverConfig::StateCheckingMode::Strict;
    }

    std::string corruption_str = to_lower(get_string_value(pairs, "silentcorruption", "none"));
    if (corruption_str == "dropinserts") {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::DropInserts;
    } else if (corruption_str == "manglevarchar") {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::MangleVarchar;
    } else if (corruption_str == "truncatenumeric") {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::TruncateNumeric;
    } else if (corruption_str == "nullasempty") {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::NullAsEmpty;
    } else if (corruption_str == "skewnumeric") {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::SkewNumeric;
    } else if (corruption_str == "skewnumericbound") {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::SkewNumericBound;
    } else if (corruption_str == "mangleunicode") {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::MangleUnicode;
    } else {
        config.silent_corruption = DriverConfig::SilentCorruptionMode::None;
    }

    // SQLNativeSql pass-through — only the literal string "true" enables it.
    std::string pass_through_str =
        to_lower(get_string_value(pairs, "nativesqlpassthrough", "false"));
    config.native_sql_pass_through = (pass_through_str == "true");

    // Procedures=BrokenInout — drives the PORT plan port 3 e2e canary.
    std::string procs_str =
        to_lower(get_string_value(pairs, "procedures", ""));
    config.procedures_broken_inout = (procs_str == "brokeninout");

    // PORT plan port 6 — array-bind misbehavior knobs.
    std::string fetch_warn_str =
        to_lower(get_string_value(pairs, "fetchreturnswarning", "false"));
    config.fetch_returns_warning =
        (fetch_warn_str == "true" || fetch_warn_str == "yes");

    config.array_bind_row_fails_at =
        clamp_int(get_int_value(pairs, "arraybindrowfailsat", 0), 0, 1000000);
    std::string supports_str =
        to_lower(get_string_value(pairs, "supportsarraybind", "true"));
    config.supports_array_bind = (supports_str != "false" && supports_str != "no");

    return config;
}

} // namespace mock_odbc
