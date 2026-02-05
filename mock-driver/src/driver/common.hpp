#pragma once

// Common includes for all mock driver components

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Undefine UNICODE to avoid ODBC macro conflicts
#ifdef UNICODE
#undef UNICODE
#endif
#ifdef _UNICODE
#undef _UNICODE
#endif

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <optional>
#include <variant>
#include <chrono>
#include <mutex>
#include <atomic>

namespace mock_odbc {

// Forward declarations
class EnvironmentHandle;
class ConnectionHandle;
class StatementHandle;
class DescriptorHandle;

// Handle types
enum class HandleType {
    ENV = SQL_HANDLE_ENV,
    DBC = SQL_HANDLE_DBC,
    STMT = SQL_HANDLE_STMT,
    DESC = SQL_HANDLE_DESC
};

// Magic number to validate handles
constexpr uint32_t HANDLE_MAGIC = 0x4D4F434B;  // "MOCK"

} // namespace mock_odbc
