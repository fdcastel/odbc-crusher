// to_sqlwchar — IMPROVEMENT_PLAN.md C14.
//
// It used to push each UTF-8 *byte* as one code unit, so "café" (63 61 66 C3
// A9) became U+0063 U+0061 U+0066 U+00C3 U+00A9 — "cafÃ©". Mojibake, from a
// helper with 44 call sites, in a header that unicode_tests.cpp includes.
//
// The width cases are the ones A10 had to fix in make_wchar_buf for the same
// reason: SQLWCHAR is 2 bytes on Windows and 4 on most unixODBC builds, so a
// supplementary codepoint is a surrogate pair on one and a single unit on the
// other. A test that hard-coded either would pass on one platform and report
// a conforming driver as broken on the other, which is exactly what A10 was.
#include <gtest/gtest.h>

#include "tests/sqlwchar_utils.hpp"

#include <cstdint>
#include <vector>

using odbc_crusher::tests::to_sqlwchar;

namespace {

// The buffer without its terminator, which every assertion here is about.
std::vector<uint32_t> units(const char* s) {
    auto v = to_sqlwchar(s);
    EXPECT_FALSE(v.empty());
    EXPECT_EQ(v.back(), 0) << "the buffer must be NUL-terminated";
    std::vector<uint32_t> out;
    for (size_t i = 0; i + 1 < v.size(); ++i) out.push_back(v[i]);
    return out;
}

}  // namespace

TEST(SqlWcharUtilsTest, AsciiIsOneUnitPerCharacter) {
    EXPECT_EQ(units("abc"), (std::vector<uint32_t>{'a', 'b', 'c'}));
    EXPECT_EQ(units(""), std::vector<uint32_t>{});
    EXPECT_TRUE(to_sqlwchar(nullptr).empty());
}

TEST(SqlWcharUtilsTest, TwoByteSequenceIsOneCodepointNotTwoBytes) {
    // The bug, at its smallest. "é" is C3 A9; the old code produced
    // {0x00C3, 0x00A9} and this asserts the one codepoint it actually is.
    EXPECT_EQ(units("\xC3\xA9"), (std::vector<uint32_t>{0x00E9}));
    // And in context, which is the shape a probe would have compared against.
    EXPECT_EQ(units("caf\xC3\xA9"),
              (std::vector<uint32_t>{'c', 'a', 'f', 0x00E9}));
}

TEST(SqlWcharUtilsTest, ThreeByteSequenceDecodes) {
    // 数据 — two CJK codepoints, six bytes.
    EXPECT_EQ(units("\xE6\x95\xB0\xE6\x8D\xAE"),
              (std::vector<uint32_t>{0x6570, 0x636E}));
}

TEST(SqlWcharUtilsTest, SupplementaryCodepointMatchesThisBuildsWidth) {
    // U+1F600, four UTF-8 bytes. On a 2-byte SQLWCHAR this is a surrogate
    // pair; on a 4-byte one it is a single unit. Asserting either
    // unconditionally is the A10 mistake.
    const auto got = units("\xF0\x9F\x98\x80");
    // `if constexpr`, because MSVC treats a runtime branch on a compile-time
    // constant as C4127 and /WX makes that an error.
    if constexpr (sizeof(SQLWCHAR) >= 4) {
        EXPECT_EQ(got, (std::vector<uint32_t>{0x1F600}));
    } else {
        EXPECT_EQ(got, (std::vector<uint32_t>{0xD83D, 0xDE00}))
            << "a 2-byte SQLWCHAR needs the surrogate pair";
    }
}

TEST(SqlWcharUtilsTest, IllFormedInputBecomesTheReplacementCharacter) {
    // Passing the bytes through is what produced mojibake; a helper that
    // invents characters makes a probe assert against them and report a
    // conforming driver as broken.
    EXPECT_EQ(units("\xFF"), (std::vector<uint32_t>{0xFFFD}));
    EXPECT_EQ(units("\xA9"), (std::vector<uint32_t>{0xFFFD}));   // lone cont.
    EXPECT_EQ(units("\xC0\xAF"),
              (std::vector<uint32_t>{0xFFFD, 0xFFFD}));          // overlong
    EXPECT_EQ(units("\xED\xA0\x80"),
              (std::vector<uint32_t>{0xFFFD, 0xFFFD, 0xFFFD}));  // surrogate
    // Truncated at the end of the string.
    EXPECT_EQ(units("a\xE6\x95"),
              (std::vector<uint32_t>{'a', 0xFFFD, 0xFFFD}));
}

TEST(SqlWcharUtilsTest, ValidTextAfterABadByteStillDecodes) {
    // The scan must resynchronise rather than give up.
    EXPECT_EQ(units("\xFF" "ok"), (std::vector<uint32_t>{0xFFFD, 'o', 'k'}));
}
