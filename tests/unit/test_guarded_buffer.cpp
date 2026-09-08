// GuardedBuffer — IMPROVEMENT_PLAN.md D58.
//
// Three probes handed ODBC a heap buffer allocated to exactly the length they
// declared. ASan caught what that costs: unixODBC called strlen() on a 3-byte
// std::vector<char> in test_truncation_indicators and read byte 4 — a
// heap-buffer-overflow inside the tool whose stated contract is that it never
// crashes on a badly behaved driver. macOS's driver manager does the write
// rather than the read, putting a NUL at offset 10 of a 10-byte buffer, and
// the one probe that had hand-rolled a guard is the one that caught it.
//
// These stand in for the driver: they write past the declared length the way
// a driver manager does, and assert the guard turns that into a finding.
#include <gtest/gtest.h>

#include "core/guarded_buffer.hpp"

#include <cstring>
#include <string>

using odbc_crusher::core::GuardedBuffer;
namespace core = odbc_crusher::core;

TEST(GuardedBufferTest, DeclaredLengthIsWhatTheDriverIsTold) {
    GuardedBuffer<char> buf(10);
    EXPECT_EQ(buf.declared_elements(), 10u);
    // The point of the class: the allocation grew, the declared length did
    // not. A probe that reported the padded size would be testing a buffer
    // the application never claimed to have.
    EXPECT_EQ(buf.declared_bytes(), 10);
}

TEST(GuardedBufferTest, ByteLengthOfAWideBufferCountsBytesNotCharacters) {
    // Every ODBC BufferLength argument is in bytes, wide calls included, and
    // getting that wrong is how a probe hands SQLGetInfoW half a buffer.
    GuardedBuffer<SQLWCHAR> buf(8);
    EXPECT_EQ(buf.declared_elements(), 8u);
    EXPECT_EQ(buf.declared_bytes(),
              static_cast<SQLSMALLINT>(8 * sizeof(SQLWCHAR)));
}

TEST(GuardedBufferTest, AnUntouchedGuardIsNoFinding) {
    GuardedBuffer<char> buf(10);
    // A well-behaved driver writing exactly what it was allowed to.
    std::memset(buf.data(), 'A', 9);
    buf.data()[9] = '\0';
    EXPECT_FALSE(buf.guard_breach().has_value());
}

TEST(GuardedBufferTest, TheTerminatorWrittenOnePlaceLateIsCaught) {
    // Exactly what the macOS driver manager did: ten characters, then a NUL
    // at offset ten. One byte, and without the guard it is heap corruption.
    GuardedBuffer<char> buf(10);
    std::memset(buf.data(), 'A', 10);
    buf.data()[10] = '\0';

    auto breach = buf.guard_breach();
    ASSERT_TRUE(breach.has_value());
    EXPECT_EQ(*breach, 10u) << "the breach must be reported at the offset the "
                               "driver wrote, not at the start of the guard";
    // The hex must show the byte, since a lone 0x00 is a terminator one place
    // late and a run of text is a driver ignoring the length entirely — the
    // two have different causes and the message has to tell them apart.
    EXPECT_EQ(buf.guard_hex().substr(0, 4), "0x00");
}

TEST(GuardedBufferTest, AWholeStringWrittenPastTheEndIsCaught) {
    GuardedBuffer<char> buf(4);
    std::strcpy(buf.data(), "mockodbc.dll");   // 12 chars + NUL into 4

    auto breach = buf.guard_breach();
    ASSERT_TRUE(breach.has_value());
    EXPECT_EQ(*breach, 4u);
    // 16 guard elements is enough room for this to be visible rather than
    // merely fatal.
    EXPECT_NE(buf.guard_hex().find("0x6F"), std::string::npos)  // 'o'
        << "guard was: " << buf.guard_hex();
}

TEST(GuardedBufferTest, AWideOverrunIsCaughtAndPrintedAsWideUnits) {
    GuardedBuffer<SQLWCHAR> buf(4);
    for (size_t i = 0; i < 5; ++i) buf.data()[i] = static_cast<SQLWCHAR>('W');

    auto breach = buf.guard_breach();
    ASSERT_TRUE(breach.has_value());
    EXPECT_EQ(*breach, 4u);
    EXPECT_EQ(buf.guard_hex().substr(0, 2 + 2 * sizeof(SQLWCHAR)),
              sizeof(SQLWCHAR) == 2 ? "0x0057" : "0x00000057");
}

TEST(GuardedBufferTest, SentinelAndFillDifferSoAnOverrunIsVisible) {
    // If the guard were filled with the same byte as the declared region, a
    // driver writing that byte past the end would be invisible.
    GuardedBuffer<char> buf(4);
    ASSERT_FALSE(buf.guard_breach().has_value());
    buf.data()[4] = buf.data()[0];          // the region's fill byte
    EXPECT_TRUE(buf.guard_breach().has_value())
        << "the guard sentinel must differ from the region fill";
}

// D60: the one exception worth keeping here, because it is a property of the
// guard rather than of bounded_string: even the crudest possible reader -
// strlen straight off the pointer, which is what unixODBC does - terminates
// inside the allocation. A3's exhaustive bounded_string cases live in
// test_test_base.cpp and were not duplicated.
TEST(GuardedBufferTest, AGuardedBufferStopsARunawayScanInsideItsOwnMemory) {
    core::GuardedBuffer<char> buf(32, 'X');           // no zero in the region
    const size_t scanned = std::strlen(buf.data());
    EXPECT_EQ(scanned, 32u + core::GuardedBuffer<char>::kSentinelElements)
        << "the scan must stop at the guard's stopper, not run past it";
    EXPECT_FALSE(buf.guard_breach().has_value())
        << "and reading must not look like a write";
}


// ── D68: bounded_wchar_units ──────────────────────────────────────────────
//
// The wide half of A3's bounded_string. Two probes had written this by hand as
// `while (n < capacity - 1 && buf[n] != 0) ++n;` - a scan to a terminator in a
// function whose caller was holding StrLen_or_IndPtr - and under
// BufferValidation=Lenient both reported one extra U+0058 and FAILed a driver
// whose value was correct.

TEST(BoundedWcharTest, UsesTheReportedLengthNotTheTerminator) {
    SQLWCHAR buf[8];
    for (auto& u : buf) u = static_cast<SQLWCHAR>('X');   // no terminator
    buf[0] = 'a'; buf[1] = 'b'; buf[2] = 'c';

    // ODBC reports StrLen_or_IndPtr in BYTES even for SQL_C_WCHAR. Getting
    // that conversion wrong is the reason this is one function and not five.
    const SQLLEN reported = static_cast<SQLLEN>(3 * sizeof(SQLWCHAR));
    EXPECT_EQ(core::bounded_wchar_units(buf, 8, reported), 3u);
}

TEST(BoundedWcharTest, StopsAtATerminatorTheDriverDidWrite) {
    SQLWCHAR buf[8] = {'a', 'b', 0, 'x', 'y', 'z', 'w', 'v'};
    // The driver claims more than it terminated. Take the shorter - a value
    // past its own NUL is not a value.
    EXPECT_EQ(core::bounded_wchar_units(
                  buf, 8, static_cast<SQLLEN>(6 * sizeof(SQLWCHAR))),
              2u);
}

TEST(BoundedWcharTest, NeverRunsPastTheDeclaredBuffer) {
    SQLWCHAR buf[4];
    for (auto& u : buf) u = static_cast<SQLWCHAR>('X');
    // A driver reporting far more than the buffer holds is the truncation
    // case: SQL_SUCCESS_WITH_INFO plus a length that is *available*, not
    // *written*. One unit belongs to the terminator, so three is the most
    // that can have been delivered.
    EXPECT_EQ(core::bounded_wchar_units(
                  buf, 4, static_cast<SQLLEN>(4000 * sizeof(SQLWCHAR))),
              3u);
}

TEST(BoundedWcharTest, NullDataIsNoUnitsRatherThanAHugeCount) {
    SQLWCHAR buf[8];
    for (auto& u : buf) u = static_cast<SQLWCHAR>('X');
    EXPECT_EQ(core::bounded_wchar_units(buf, 8, SQL_NULL_DATA), 0u);
}

TEST(BoundedWcharTest, NoTotalFallsBackToWhatIsInTheBuffer) {
    SQLWCHAR buf[8] = {'a', 'b', 'c', 0, 0, 0, 0, 0};
    // The driver cannot say how much there is. Take what was written, which
    // is the only thing left to go on.
    EXPECT_EQ(core::bounded_wchar_units(buf, 8, SQL_NO_TOTAL), 3u);

    for (auto& u : buf) u = static_cast<SQLWCHAR>('X');
    EXPECT_EQ(core::bounded_wchar_units(buf, 8, SQL_NO_TOTAL), 7u)
        << "with no terminator either, the declared region bounds the answer";
}

TEST(BoundedWcharTest, NullBufferAndZeroCapacityAreNotACrash) {
    EXPECT_EQ(core::bounded_wchar_units(nullptr, 8, 4), 0u);
    SQLWCHAR buf[1] = {0};
    EXPECT_EQ(core::bounded_wchar_units(buf, 0, 4), 0u);
}

// ── D69: sqlstate_string ──────────────────────────────────────────────────
//
// SQLGetDiagRec returns the one string in ODBC with no length beside it, and
// twelve reads took it as a C string out of a six-byte array. Against a driver
// that omits the terminator there is then no NUL in the array at all: on
// Windows the report carried `42000X` plus uninitialised stack, and on macOS
// crusher was killed - `Abort trap: 6`, twice in one CI run, from the
// platform's fortified strlen catching a scan past a compile-time-known
// six-byte object.

TEST(SqlstateStringTest, ReadsTheFiveCharactersTheSpecDefines) {
    const SQLCHAR state[6] = {'4', '2', '0', '0', '0', 0};
    EXPECT_EQ(core::sqlstate_string(state), "42000");
}

TEST(SqlstateStringTest, StopsAtFiveWhenTheDriverWroteNoTerminator) {
    // The Lenient shape: five characters and a filler where the NUL belongs,
    // so there is no zero in the array. This is the case that aborted.
    const char state[6] = {'4', '2', '0', '0', '0', 'X'};
    EXPECT_EQ(core::sqlstate_string(state), "42000");
}

TEST(SqlstateStringTest, StopsEarlyWhenTheDriverTerminatedEarly) {
    // A driver may write a short state, or none at all. Both are its answer,
    // and neither should read into the bytes after it.
    const char short_state[6] = {'0', '1', 0, 'j', 'u', 'n'};
    EXPECT_EQ(core::sqlstate_string(short_state), "01");

    const char empty_state[6] = {0, 'j', 'u', 'n', 'k', '!'};
    EXPECT_EQ(core::sqlstate_string(empty_state), "");
}

TEST(SqlstateStringTest, NullPointerIsEmptyRatherThanACrash) {
    EXPECT_EQ(core::sqlstate_string(static_cast<const char*>(nullptr)), "");
}

// The property that made this worth a helper rather than a `substr` at each
// site: whatever the six bytes contain, exactly five of them are read.
TEST(SqlstateStringTest, NeverReadsTheSixthByte) {
    char state[6];
    std::memset(state, 'A', sizeof(state));
    const std::string s = core::sqlstate_string(state);
    EXPECT_EQ(s.size(), 5u);
    EXPECT_EQ(s, "AAAAA");
}
