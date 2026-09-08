#include "category_teardown.hpp"

#include "core/crash_guard.hpp"

namespace odbc_crusher::tests {

std::vector<TestResult> teardown_categories(
    std::vector<std::unique_ptr<TestBase>>& categories) {
    std::vector<TestResult> crashes;

    for (auto& category : categories) {
        if (!category) continue;

        // The name has to be taken before the object goes: after the fault
        // there is nothing left to ask, and "a category crashed" without
        // saying which is the shape D63 spent a round fixing.
        const std::string name = category->category_name();

        auto guard = core::execute_with_crash_guard([&]() {
            category.reset();
        });
        if (!guard.crashed) continue;

        // D76: the object is left as it was when the fault interrupted it.
        // siglongjmp skipped the rest of its destructor, so releasing the
        // pointer leaks it deliberately rather than running the same
        // destructor again and faulting a second time - this time inside the
        // handler that is meant to catch it.
        (void)category.release();

        TestResult r;
        r.test_name = name + " (DRIVER CRASH IN TEARDOWN)";
        r.function = "N/A";
        r.status = TestStatus::ERR;
        r.severity = Severity::CRITICAL;
        r.conformance = ConformanceLevel::CORE;
        r.expected = "Test category tears down without crashing";
        r.actual = guard.description;
        r.diagnostic =
            "The driver crashed while this category was being destroyed - the "
            "point at which its temporary tables are dropped and its "
            "connection attributes restored. The probes themselves completed; "
            "what failed is the cleanup after them.";
        r.suggestion =
            "Check the driver's DROP TABLE path and SQLSetConnectAttr("
            "SQL_ATTR_AUTOCOMMIT). A crash here leaves the table behind, so "
            "the next run may start from a dirty schema.";
        r.duration = std::chrono::microseconds(0);
        crashes.push_back(std::move(r));
    }

    return crashes;
}

} // namespace odbc_crusher::tests
