#pragma once

// Making driver-supplied bytes safe to put in a JSON document — D52.
//
// Every string in the report comes from the driver under test: info values,
// diagnostic messages, type names, and the `actual` text of a probe that
// copied a buffer the driver wrote. None of it is guaranteed to be UTF-8, and
// a tool whose whole purpose is to survive a badly behaved driver must not
// lose the entire report to one stray byte — which is exactly what happened:
// nlohmann::json::dump() throws type_error.316 on the first ill-formed
// sequence, so a single 0x83 from a truncated wide-to-narrow conversion left
// the run with no report at all.

#include <nlohmann/json.hpp>
#include <string>

namespace odbc_crusher::reporting {

// Return `text` with every byte that is not part of a well-formed UTF-8
// sequence replaced by a visible `<0xNN>` marker. Valid text — ASCII or
// otherwise — is returned unchanged.
//
// The byte value is kept rather than folded into U+FFFD because it is the
// finding: "the driver put 0x83 here" is what a driver developer needs, and
// a replacement character says only that something was wrong.
std::string sanitize_utf8(const std::string& text);

// Apply sanitize_utf8() to every string in `doc`, recursively. Object keys
// are sanitized too — `scalar_functions.convert_matrix` is keyed by names
// the driver supplied.
void sanitize_utf8_in_place(nlohmann::json& doc);

} // namespace odbc_crusher::reporting
