#include "utf8_sanitize.hpp"

#include <cstdio>
#include <utility>
#include <vector>

namespace odbc_crusher::reporting {

namespace {

// Length of the well-formed UTF-8 sequence starting at `p`, or 0 if the byte
// there cannot start one. `avail` bounds the read, so a sequence truncated by
// the end of the string is ill-formed rather than an overrun — the case that
// matters here, since an unterminated driver buffer is usually cut mid-glyph.
size_t valid_sequence_length(const unsigned char* p, size_t avail) {
    const unsigned char b0 = p[0];
    if (b0 < 0x80) return 1;                       // ASCII, NUL included

    size_t len = 0;
    unsigned int cp = 0;
    if ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1Fu; }
    else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0Fu; }
    else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07u; }
    else return 0;                                 // continuation, or 0xF8+

    if (avail < len) return 0;
    for (size_t i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }

    // A sequence can parse and still be ill-formed. nlohmann rejects all
    // three of these, so accepting them here would defeat the point.
    static const unsigned int kSmallest[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kSmallest[len]) return 0;             // overlong
    if (cp > 0x10FFFFu) return 0;                  // out of range
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;  // surrogate half
    return len;
}

}  // namespace

std::string sanitize_utf8(const std::string& text) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const size_t n = text.size();

    // Scan first and copy only if there is something to fix: the overwhelming
    // majority of strings in a report are clean, and this runs over the whole
    // document before every snapshot.
    size_t i = 0;
    while (i < n) {
        const size_t len = valid_sequence_length(bytes + i, n - i);
        if (len == 0) break;
        i += len;
    }
    if (i == n) return text;

    std::string out;
    out.reserve(n + 8);
    out.append(text, 0, i);
    while (i < n) {
        const size_t len = valid_sequence_length(bytes + i, n - i);
        if (len > 0) {
            out.append(text, i, len);
            i += len;
        } else {
            char marker[8];
            std::snprintf(marker, sizeof(marker), "<0x%02X>",
                          static_cast<unsigned>(bytes[i]));
            out += marker;
            ++i;
        }
    }
    return out;
}

void sanitize_utf8_in_place(nlohmann::json& doc) {
    switch (doc.type()) {
        case nlohmann::json::value_t::string: {
            auto& value = doc.get_ref<std::string&>();
            std::string clean = sanitize_utf8(value);
            // A replacement is always six bytes where there was one, so a
            // size change is an exact test for "something was fixed" and
            // saves an assignment on every clean string.
            if (clean.size() != value.size()) value = std::move(clean);
            break;
        }
        case nlohmann::json::value_t::array:
            for (auto& element : doc) sanitize_utf8_in_place(element);
            break;
        case nlohmann::json::value_t::object: {
            std::vector<std::pair<std::string, std::string>> renames;
            for (auto& entry : doc.items()) {
                sanitize_utf8_in_place(entry.value());
                std::string clean = sanitize_utf8(entry.key());
                if (clean != entry.key()) renames.emplace_back(entry.key(),
                                                               std::move(clean));
            }
            // Rehanging a key mid-iteration would invalidate the iterator, so
            // the renames happen after the walk.
            for (auto& [from, to] : renames) {
                doc[to] = std::move(doc[from]);
                doc.erase(from);
            }
            break;
        }
        default:
            break;   // numbers, booleans, null — nothing to sanitize
    }
}

} // namespace odbc_crusher::reporting
