// UTF-8 sanitizing — IMPROVEMENT_PLAN.md D52.
//
// Found on Linux and macOS the moment D2 gave the .so the Windows export
// surface: with only the W entry points exported, unixODBC converts, and a
// driver string that is not properly terminated comes back to the application
// as a partial wide-to-narrow conversion. One such byte — 0x83 — reached the
// report and nlohmann::json::dump() threw type_error.316, so crusher wrote no
// report at all. A tool for testing badly behaved drivers cannot lose its
// entire product to a byte the driver was supposed to be caught returning.
#include <gtest/gtest.h>

#include "reporting/utf8_sanitize.hpp"

#include <nlohmann/json.hpp>
#include <string>

using odbc_crusher::reporting::sanitize_utf8;
using odbc_crusher::reporting::sanitize_utf8_in_place;

namespace {

// Everything the sanitizer emits must survive a dump() that is *not* using
// the replace error handler — that is the whole claim being made.
void expect_dumpable(const std::string& text) {
    nlohmann::json j = sanitize_utf8(text);
    EXPECT_NO_THROW((void)j.dump()) << "sanitized text still not dumpable";
}

}  // namespace

TEST(Utf8Sanitize, LeavesValidTextExactlyAsItWas) {
    // ASCII, two-byte, three-byte and four-byte sequences, and an embedded
    // NUL — all well-formed, none of it the sanitizer's business.
    const std::string ascii = "No NUL within 256 bytes; length 12";
    const std::string accented = "cria\xC3\xA7\xC3\xA3o";                 // criação
    const std::string cjk = "\xE6\x95\xB0\xE6\x8D\xAE";                   // 数据
    const std::string emoji = "\xF0\x9F\x92\xA5";                         // 💥
    const std::string with_nul("a\0b", 3);

    EXPECT_EQ(sanitize_utf8(ascii), ascii);
    EXPECT_EQ(sanitize_utf8(accented), accented);
    EXPECT_EQ(sanitize_utf8(cjk), cjk);
    EXPECT_EQ(sanitize_utf8(emoji), emoji);
    EXPECT_EQ(sanitize_utf8(with_nul), with_nul);
    EXPECT_EQ(sanitize_utf8(""), "");
}

TEST(Utf8Sanitize, NamesTheOffendingByteRatherThanHidingIt) {
    // The exact shape of the CI failure: a lone 0x83 in the middle of text.
    EXPECT_EQ(sanitize_utf8("mockodbc.dll\x83XX"), "mockodbc.dll<0x83>XX");
    // The byte value is the finding, so it must be legible, not folded into
    // a single "something was wrong" marker.
    EXPECT_EQ(sanitize_utf8("\xFF\xFE"), "<0xFF><0xFE>");
}

TEST(Utf8Sanitize, RejectsSequencesThatParseButAreIllFormed) {
    // nlohmann rejects all of these, so accepting any would defeat the point.
    EXPECT_EQ(sanitize_utf8("\xC0\xAF"), "<0xC0><0xAF>");          // overlong '/'
    EXPECT_EQ(sanitize_utf8("\xE0\x80\xAF"), "<0xE0><0x80><0xAF>");// overlong
    EXPECT_EQ(sanitize_utf8("\xED\xA0\x80"), "<0xED><0xA0><0x80>");// surrogate
    EXPECT_EQ(sanitize_utf8("\xF5\x80\x80\x80"),
              "<0xF5><0x80><0x80><0x80>");                          // > U+10FFFF
    // Truncated by the end of the buffer — the usual shape of an
    // unterminated driver string cut mid-glyph.
    EXPECT_EQ(sanitize_utf8("ab\xE6\x95"), "ab<0xE6><0x95>");
    // A continuation byte with nothing in front of it.
    EXPECT_EQ(sanitize_utf8("\xA9!"), "<0xA9>!");

    expect_dumpable("\xC0\xAF");
    expect_dumpable("\xED\xA0\x80");
    expect_dumpable("ab\xE6\x95");
}

TEST(Utf8Sanitize, ValidTextAfterABadByteIsStillDecoded) {
    // The scan must resynchronise: one bad byte must not turn the rest of the
    // string into a column of markers.
    EXPECT_EQ(sanitize_utf8("\x83""cria\xC3\xA7\xC3\xA3o"),
              "<0x83>cria\xC3\xA7\xC3\xA3o");
}

TEST(Utf8SanitizeJson, ReachesValuesKeysAndNestedContainers) {
    nlohmann::json doc;
    doc["driver_info"]["driver_name"] = "mockodbc.dll\x83";
    doc["categories"][0]["tests"][0]["actual"] = "bad \xFF byte";
    doc["categories"][0]["tests"][0]["duration_us"] = 42;
    // convert_matrix is keyed by names the driver supplied, so a key can be
    // just as ill-formed as a value.
    doc["scalar_functions"]["convert_matrix"]["VARCHAR\x90"] = 7;

    EXPECT_THROW((void)doc.dump(), nlohmann::json::type_error)
        << "precondition: this document is what used to lose the report";

    sanitize_utf8_in_place(doc);

    EXPECT_NO_THROW((void)doc.dump());
    EXPECT_EQ(doc["driver_info"]["driver_name"], "mockodbc.dll<0x83>");
    EXPECT_EQ(doc["categories"][0]["tests"][0]["actual"], "bad <0xFF> byte");
    EXPECT_EQ(doc["categories"][0]["tests"][0]["duration_us"], 42);
    ASSERT_TRUE(doc["scalar_functions"]["convert_matrix"]
                    .contains("VARCHAR<0x90>"));
    EXPECT_EQ(doc["scalar_functions"]["convert_matrix"]["VARCHAR<0x90>"], 7);
}
