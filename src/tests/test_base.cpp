#include "test_base.hpp"

namespace odbc_crusher::tests {

TestResult TestBase::make_result(
    const std::string& test_name,
    const std::string& function,
    TestStatus status,
    const std::string& expected,
    const std::string& actual,
    Severity severity,
    ConformanceLevel conformance,
    const std::string& spec_reference
) {
    TestResult result;
    result.test_name = test_name;
    result.function = function;
    result.status = status;
    result.severity = severity;
    result.conformance = conformance;
    result.spec_reference = spec_reference;
    result.expected = expected;
    result.actual = actual;
    result.duration = std::chrono::microseconds(0);
    return result;
}

} // namespace odbc_crusher::tests
