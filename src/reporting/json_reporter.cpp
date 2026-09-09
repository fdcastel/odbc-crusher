#include "json_reporter.hpp"
#include "odbc_crusher/version.hpp"
#include "utf8_sanitize.hpp"
#include <cctype>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <system_error>

namespace odbc_crusher::reporting {

namespace {

// G4: ISO-8601 UTC, e.g. "2026-09-07T13:14:15Z". The report used to carry a
// raw std::time_t integer, which every consumer had to know was epoch seconds.
std::string iso8601_utc(std::time_t t) {
    std::tm tm_buf{};
#ifdef _WIN32
    if (gmtime_s(&tm_buf, &t) != 0) return {};
#else
    if (gmtime_r(&t, &tm_buf) == nullptr) return {};
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) == 0) return {};
    return buf;
}

// S4: the report is published as a CI artifact, and it carried
// `PWD=masterkey` in clear. Replace the value of every keyword that names a
// secret and leave the rest of the string legible - the connection string is
// the single most useful line in the report when two runs disagree, so
// dropping it outright would cost more than it saves.
std::string redact_secrets(const std::string& connection_string) {
    static const char* kSecretKeys[] = {"PWD", "PASSWORD", "NEWPWD", "SECRET",
                                        "TOKEN", "APIKEY", "API_KEY"};
    std::string upper;
    upper.reserve(connection_string.size());
    for (char c : connection_string) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    std::string out = connection_string;
    for (const char* key : kSecretKeys) {
        const std::string needle = std::string(key) + "=";
        size_t at = 0;
        while ((at = upper.find(needle, at)) != std::string::npos) {
            // Only at a keyword boundary, or PWD matches NEWPWD and TOKEN
            // matches REFRESHTOKEN.
            const bool at_boundary =
                at == 0 || upper[at - 1] == ';' ||
                std::isspace(static_cast<unsigned char>(upper[at - 1]));
            if (!at_boundary) {
                at += needle.size();
                continue;
            }
            const size_t value_start = at + needle.size();
            size_t value_end = out.find(';', value_start);
            if (value_end == std::string::npos) value_end = out.size();
            out.replace(value_start, value_end - value_start, "***");
            upper = out;
            for (auto& c : upper) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            at = value_start + 3;
        }
    }
    return out;
}

// S4: what produced this report, beyond the driver. Two reports of the same
// driver are only comparable if these agree, and nothing in either report used
// to say so - a changed runner image or a rebuilt crusher would have read as a
// driver regression.
std::string platform_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown";
#endif
}

std::string architecture_name() {
#if defined(_M_X64) || defined(__x86_64__)
    return "x86_64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

}  // namespace

void JsonReporter::report_start(const std::string& connection_string) {
    root_ = nlohmann::json::object();
    // G4: version the output contract. Integer, matching the convention
    // .github/drivers.json already uses. Bump it whenever an existing key
    // changes meaning, is renamed or is removed; purely additive changes keep
    // the number. Consumers should reject a version they do not know.
    root_["schema_version"] = kSchemaVersion;
    root_["connection_string"] = redact_secrets(connection_string);   // S4
    root_["timestamp"] = iso8601_utc(std::time(nullptr));

    // S4
    nlohmann::json environment;
    environment["crusher_version"] = ODBC_CRUSHER_VERSION;
    environment["platform"] = platform_name();
    environment["architecture"] = architecture_name();
    environment["pointer_bits"] = static_cast<int>(sizeof(void*) * 8);
    // The width of SQLLEN is the single most load-bearing ABI fact about an
    // ODBC build - P6's `SQL_DESC_LENGTH=9187201948296675328` is a driver
    // writing 32 bits of one - and it is not implied by the pointer size.
    environment["sqllen_bits"] = static_cast<int>(sizeof(SQLLEN) * 8);
#ifdef NDEBUG
    environment["build_type"] = "Release";
#else
    environment["build_type"] = "Debug";
#endif
    root_["environment"] = environment;

    categories_ = nlohmann::json::array();
}

void JsonReporter::report_category(const std::string& category_name,
                                   const std::vector<tests::TestResult>& results) {
    nlohmann::json category;
    category["name"] = category_name;
    
    nlohmann::json tests_array = nlohmann::json::array();
    
    for (const auto& result : results) {
        nlohmann::json test;
        test["test_name"] = result.test_name;
        test["function"] = result.function;
        
        test["status"] = tests::status_to_string(result.status);
        
        // Severity
        test["severity"] = tests::severity_to_string(result.severity);
        
        // Conformance
        test["conformance_level"] = tests::conformance_to_string(result.conformance);
        if (!result.spec_reference.empty()) {
            test["spec_reference"] = result.spec_reference;
        }
        
        test["expected"] = result.expected;
        test["actual"] = result.actual;
        test["duration_us"] = result.duration.count();
        
        if (result.diagnostic) {
            test["diagnostic"] = *result.diagnostic;
        }
        if (result.suggestion) {
            test["suggestion"] = *result.suggestion;
        }
        
        tests_array.push_back(test);
    }
    
    category["tests"] = tests_array;
    categories_.push_back(category);

    // F2: persist what we have, so a killed run still leaves a usable report.
    //
    // S6: a category carrying an ERROR bypasses the rate limiter. The limiter
    // exists so that 23 categories completing in quick succession do not
    // rewrite the whole document 23 times, and on a healthy run that costs
    // nothing because report_end() writes everything at the end. On a run that
    // is *killed* there is no report_end(), and everything since the last
    // snapshot is lost — which is how the U2 Linux run against Firebird
    // 3.0.1.21 produced a JSON holding one category when the text held ten.
    // Nine categories had completed inside a second of each other, so the
    // limiter skipped every one of their writes, and then the driver wedged
    // and nothing else was ever written.
    //
    // An ERROR means a category crashed the driver, which is both the evidence
    // most worth keeping and a good predictor that the run is about to end
    // badly. One extra write there is free by comparison.
    const bool category_had_error =
        std::any_of(results.begin(), results.end(), [](const tests::TestResult& r) {
            return r.status == tests::TestStatus::ERR;
        });
    if (category_had_error) {
        write_snapshot(false);
        wrote_snapshot_ = true;
        last_snapshot_ = std::chrono::steady_clock::now();
    } else {
        maybe_write_snapshot();
    }
}

void JsonReporter::report_summary(size_t total_tests, size_t passed, size_t failed,
                                  size_t skipped, size_t errors,
                                  size_t informational,
                                  std::chrono::microseconds total_duration) {
    // B2: `scored` is the pass rate's denominator - every result except the
    // informational ones, which are reported but have no right answer to
    // grade. Consumers reading `pass_rate` keep working; one that recomputed
    // it as passed/total_tests will now disagree with us, which is exactly
    // why `scored` and `informational` are published rather than left for a
    // reader to infer.
    const size_t scored = total_tests > informational
                        ? total_tests - informational : 0;

    nlohmann::json summary;
    summary["total_tests"] = total_tests;
    summary["passed"] = passed;
    summary["failed"] = failed;
    summary["skipped"] = skipped;
    summary["errors"] = errors;
    summary["informational"] = informational;
    summary["scored"] = scored;
    summary["total_duration_us"] = total_duration.count();

    if (scored > 0) {
        summary["pass_rate"] =
            (static_cast<double>(passed) * 100.0) / static_cast<double>(scored);
    } else {
        summary["pass_rate"] = 0.0;
    }
    
    root_["summary"] = summary;
    // categories are attached by write_snapshot() / report_end(), which own
    // that key — see F2.
}

// D82: a report the tool cannot parse must not be handed over as if it were
// one.
//
// Seen on macOS: `"categories": [` followed immediately by a bare `,`.
// Element 0 had serialised to zero characters, which nlohmann does for
// exactly one thing - a value whose `m_type` matches no case in
// `serializer::dump`'s switch, where the `JSON_ASSERT(false)` in the default
// arm is compiled out under NDEBUG. (A *discarded* value is not it; that
// prints `<discarded>`, which is also unparseable but visible.) Either way
// the node is corrupt, and the likeliest source is D71: unixODBC takes a wild
// write when handed an unterminated string, and the three scenarios that hit
// this are the three that hand it one.
//
// That cause is in someone else's library. This is about the consequence:
// crusher wrote the broken document, printed "JSON report written to:", and
// exited - so every consumer got a file that would not parse and nothing said
// why. The report is the product; it is the one thing that has to survive.
//
// Returns how many elements had to be replaced.
size_t quarantine_unserialisable(nlohmann::json& array_of_categories) {
    if (!array_of_categories.is_array()) return 0;

    constexpr auto kReplace = nlohmann::json::error_handler_t::replace;
    size_t replaced = 0;

    for (size_t i = 0; i < array_of_categories.size(); ++i) {
        auto& element = array_of_categories[i];

        // Round-trip each element on its own. Cheap next to the run itself,
        // and it localises the damage to one category instead of losing the
        // whole document - which is what happened.
        bool ok = false;
        try {
            ok = nlohmann::json::accept(element.dump(-1, ' ', false, kReplace));
        } catch (...) {
            ok = false;   // dump() itself threw: equally unusable
        }
        if (ok) continue;

        nlohmann::json marker = nlohmann::json::object();
        marker["name"] = "(unserialisable category #" + std::to_string(i) + ")";
        marker["tests"] = nlohmann::json::array();
        marker["error"] =
            "This category could not be serialised as JSON and was replaced so "
            "the rest of the report survives. Its in-memory representation was "
            "corrupt - see IMPROVEMENT_PLAN.md D82, and D71 for the wild write "
            "in the driver manager that is the likeliest cause.";
        element = std::move(marker);
        ++replaced;
    }

    return replaced;
}

void JsonReporter::maybe_write_snapshot() {
    if (output_file_.empty()) return;

    const auto now = std::chrono::steady_clock::now();
    if (wrote_snapshot_ && (now - last_snapshot_) < kSnapshotInterval) return;

    write_snapshot(false);
    wrote_snapshot_ = true;
    last_snapshot_ = now;
}

void JsonReporter::write_snapshot(bool complete) {
    if (output_file_.empty()) return;

    // D52: every string below came from the driver, and nothing guarantees it
    // is UTF-8. Done in place on the accumulators so each value is inspected
    // once however many snapshots follow it.
    sanitize_utf8_in_place(root_);
    sanitize_utf8_in_place(categories_);

    // D82: only on the final write. The intermediate snapshots exist to
    // survive a kill and run after every category; parsing a ~100 KB document
    // 23 times to guard against a fault seen twice is the wrong trade, and a
    // corrupt node found there would still be corrupt at the end.
    size_t quarantined = 0;
    if (complete) {
        quarantined = quarantine_unserialisable(categories_);
    }

    nlohmann::json doc = root_;
    doc["categories"] = categories_;
    doc["complete"] = complete;
    if (quarantined > 0) {
        doc["quarantined_categories"] = quarantined;
    }

    // Write to a sibling temp file and rename over the target, so a signal
    // landing mid-write can never leave a truncated document behind. rename()
    // within a directory is atomic on POSIX, and MoveFileEx-with-replace on
    // Windows via std::filesystem.
    const std::filesystem::path target(output_file_);
    std::filesystem::path tmp = target;
    tmp += ".partial";

    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            // Report it once, on the first failure, then stay quiet: this runs
            // after every category and a broken path would otherwise emit 23
            // identical lines.
            if (!write_failed_) {
                write_failed_ = true;
                std::cerr << "Error: Could not write to " << tmp.string() << std::endl;
            }
            return;
        }
        // Intermediate snapshots are written compact: they exist to be
        // machine-read after a kill, and re-serialising a pretty-printed
        // ~100 KB document after each of 23 categories measurably lengthened
        // the run. Only the final report is indented.
        // D52: `replace` is the backstop behind sanitize_utf8_in_place - if
        // an ill-formed byte ever reaches here by a route the walk above does
        // not cover, it becomes U+FFFD and the report is still written. Losing
        // the whole document to one byte is the one outcome this tool cannot
        // afford: the report is the product.
        constexpr auto kReplace = nlohmann::json::error_handler_t::replace;
        if (complete) {
            // D82: and check the bytes parse before handing them over. The
            // per-category quarantine above should have caught anything
            // wrong, so reaching this fallback means the damage is somewhere
            // that walk does not reach - and a small valid report saying so
            // beats a large invalid one that does not.
            std::string text = doc.dump(2, ' ', false, kReplace);
            if (!nlohmann::json::accept(text)) {
                nlohmann::json rescue = nlohmann::json::object();
                rescue["schema_version"] = kSchemaVersion;
                rescue["complete"] = true;
                rescue["categories"] = nlohmann::json::array();
                rescue["report_corrupt"] =
                    "The report could not be serialised as valid JSON. This is "
                    "a defect in odbc-crusher or in the memory it was given - "
                    "see IMPROVEMENT_PLAN.md D82. The run's results are lost; "
                    "the alternative is writing a document no consumer can "
                    "read, which is what used to happen.";
                text = rescue.dump(2, ' ', false, kReplace);
                std::cerr << "Error: the report did not serialise as valid "
                             "JSON and was replaced with a corruption notice."
                          << std::endl;
            }
            file << text << std::endl;
        } else {
            file << doc.dump(-1, ' ', false, kReplace) << std::endl;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        if (!write_failed_) {
            write_failed_ = true;
            std::cerr << "Error: Could not write to " << output_file_ << ": "
                      << ec.message() << std::endl;
        }
        std::filesystem::remove(tmp, ec);
    }
}

void JsonReporter::report_end() {
    if (output_file_.empty()) {
        // Print to stdout: exactly one document, so there is nothing partial
        // to mark, but it must still carry the same keys as the file form.
        sanitize_utf8_in_place(root_);            // D52
        sanitize_utf8_in_place(categories_);
        root_["categories"] = categories_;
        root_["complete"] = true;
        std::cout << root_.dump(2, ' ', false,
                                nlohmann::json::error_handler_t::replace)
                  << std::endl;
    } else {
        write_snapshot(true);
        if (!write_failed_) {
            // G1: this is progress chatter, not report data. On stderr so that
            // `-o json -f report.json` leaves stdout completely empty.
            std::cerr << "JSON report written to: " << output_file_ << std::endl;
        }
    }
}

// G2: a filtered run is a valid report of a smaller thing, and nothing in it
// used to say so. `categories_selected` is absent on a full run, so its mere
// presence is the signal; `categories_available` gives a consumer the whole
// list without having to know this build's registry.
void JsonReporter::report_selected_categories(
    const std::vector<std::string>& available,
    const std::vector<std::string>& selected) {
    root_["categories_available"] = available;
    if (selected.size() != available.size()) {
        root_["categories_selected"] = selected;
    }
}

void JsonReporter::report_driver_info(const discovery::DriverInfo::Properties& props) {
    nlohmann::json driver_info;
    driver_info["driver_name"] = props.driver_name;
    driver_info["driver_version"] = props.driver_ver;
    driver_info["driver_odbc_version"] = props.driver_odbc_ver;
    driver_info["odbc_version"] = props.odbc_ver;
    driver_info["driver_manager_version"] = props.driver_manager_ver;   // S4
    driver_info["dbms_name"] = props.dbms_name;
    driver_info["dbms_version"] = props.dbms_ver;
    driver_info["database_name"] = props.database_name;
    driver_info["server_name"] = props.server_name;
    driver_info["user_name"] = props.user_name;
    driver_info["sql_conformance"] = props.sql_conformance;
    driver_info["catalog_term"] = props.catalog_term;
    driver_info["schema_term"] = props.schema_term;
    driver_info["table_term"] = props.table_term;
    driver_info["procedure_term"] = props.procedure_term;
    driver_info["identifier_quote_char"] = props.identifier_quote_char;
    root_["driver_info"] = driver_info;
}

void JsonReporter::report_type_info(const std::vector<discovery::TypeInfo::DataType>& types) {
    nlohmann::json type_array = nlohmann::json::array();
    for (const auto& type : types) {
        nlohmann::json t;
        t["type_name"] = type.type_name;
        t["sql_data_type"] = type.sql_data_type;
        t["column_size"] = type.column_size;
        t["nullable"] = type.nullable;
        if (type.auto_unique_value.has_value()) {
            t["auto_unique_value"] = *type.auto_unique_value;
        }
        type_array.push_back(t);
    }
    root_["type_info"] = type_array;
}

void JsonReporter::report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs) {
    nlohmann::json func_info;
    func_info["supported_count"] = funcs.supported_count;
    func_info["total_checked"] = funcs.total_checked;
    func_info["supported"] = funcs.supported;
    func_info["unsupported"] = funcs.unsupported;
    root_["function_info"] = func_info;
}

void JsonReporter::report_scalar_functions(const discovery::DriverInfo::ScalarFunctionSupport& sf) {
    nlohmann::json scalar;
    scalar["string_functions"] = sf.string_functions;
    scalar["numeric_functions"] = sf.numeric_functions;
    scalar["timedate_functions"] = sf.timedate_functions;
    scalar["system_functions"] = sf.system_functions;
    scalar["string_bitmask"] = sf.string_bitmask;
    scalar["numeric_bitmask"] = sf.numeric_bitmask;
    scalar["timedate_bitmask"] = sf.timedate_bitmask;
    scalar["system_bitmask"] = sf.system_bitmask;
    scalar["convert_functions_bitmask"] = sf.convert_functions_bitmask;
    scalar["oj_capabilities"] = sf.oj_capabilities;
    scalar["datetime_literals"] = sf.datetime_literals;

    nlohmann::json convert_matrix;
    for (const auto& [name, mask] : sf.convert_matrix) {
        convert_matrix[name] = mask;
    }
    scalar["convert_matrix"] = convert_matrix;

    root_["scalar_functions"] = scalar;
}

} // namespace odbc_crusher::reporting
