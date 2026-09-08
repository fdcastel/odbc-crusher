#pragma once

// Tearing the test categories down without letting the driver take the process
// with it — IMPROVEMENT_PLAN.md D76.
//
// `run_test_category` wraps `TestBase::run()` and nothing else, so for as long
// as the crash guard has existed the categories' *destructors* have run
// unguarded — and three of them talk to the driver. `ArrayParamTests::table_`,
// `TransactionTests::table_` and `ParameterBindingTests::tables_` each hold a
// `RoundTripTableGuard` past `run()`, and `~RoundTripTableGuard` issues
// `SQLSetConnectAttr` (via `ScopedAutocommitOn`) and a `DROP TABLE`. Those ran
// when `main`'s `categories` vector went out of scope: outside any guard, and
// *after* the report had already been written, so a driver that faults while
// dropping a table killed the process with nothing in the report to say why.
//
// The constructors need no such treatment and are deliberately left alone: all
// 23 are `: TestBase(connection) {}` and touch nothing. Guarding them would be
// guarding nothing, which is worse than not guarding — it suggests a risk that
// was checked and found.
//
// Returns one result per category whose destructor faulted, so the caller can
// report them. It reports nothing itself: that keeps the tests library free of
// a dependency on the reporting layer, and makes this testable with a stub
// category whose destructor faults on purpose.

#include "test_base.hpp"

#include <memory>
#include <vector>

namespace odbc_crusher::tests {

// Destroy each category in turn, each inside the crash guard. Every element is
// left null afterwards, whether it faulted or not.
std::vector<TestResult> teardown_categories(
    std::vector<std::unique_ptr<TestBase>>& categories);

} // namespace odbc_crusher::tests
