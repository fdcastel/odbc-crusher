#pragma once

#include <string>
#include <string_view>
#include <sstream>
#include <fstream>
#include <chrono>
#include <mutex>

namespace odbc_crusher::core {

/**
 * @brief Log levels for debugging and diagnostics
 */
enum class LogLevel {
    TRACE,   // Very detailed, every function call
    DEBUG,   // Debug information, branch decisions
    INFO,    // Informational messages
    WARN,    // Warnings
    ERROR,   // Errors
    FATAL    // Fatal errors
};

/**
 * @brief Simple thread-safe logger for debugging
 * 
 * Purpose: Provide detailed logging for developers (human or agent) to understand
 * what happened without step-by-step debugging. Logs all branch decisions (IFs).
 * 
 * Usage:
 *   Logger::instance().set_level(LogLevel::DEBUG);
 *   Logger::instance().set_output("odbc_crusher.log");
 *   
 *   LOG_DEBUG("Connecting to database");
 *   LOG_TRACE("SQLAllocHandle returned: {}", ret);
 *   LOG_IF(connection_failed, "Connection failed, retrying...");
 */
class Logger {
public:
    /**
     * @brief Get singleton instance
     */
    static Logger& instance();

    /**
     * @brief Set minimum log level
     */
    void set_level(LogLevel level);

    /**
     * @brief Set output file (empty for console only)
     */
    void set_output(std::string_view filename);

    /**
     * @brief Enable/disable console output
     */
    void set_console_enabled(bool enabled);

    /**
     * @brief Log a message
     */
    void log(LogLevel level, std::string_view file, int line,
             std::string_view function, std::string_view message);

    /**
     * @brief Log a conditional branch decision
     * @param condition The condition value
     * @param true_msg Message if condition is true
     * @param false_msg Message if condition is false (optional)
     */
    void log_branch(bool condition, std::string_view file, int line,
                   std::string_view function,
                   std::string_view true_msg,
                   std::string_view false_msg = "");

private:
    Logger();
    ~Logger();

    LogLevel min_level_ = LogLevel::INFO;
    bool console_enabled_ = true;
    std::ofstream file_stream_;
    std::mutex mutex_;

    std::string level_to_string(LogLevel level);
    std::string timestamp();
};

} // namespace odbc_crusher::core

// Convenience macros - simple string-based logging
#define LOG_TRACE(msg) \
    odbc_crusher::core::Logger::instance().log( \
        odbc_crusher::core::LogLevel::TRACE, __FILE__, __LINE__, __func__, msg)

#define LOG_DEBUG(msg) \
    odbc_crusher::core::Logger::instance().log( \
        odbc_crusher::core::LogLevel::DEBUG, __FILE__, __LINE__, __func__, msg)

#define LOG_INFO(msg) \
    odbc_crusher::core::Logger::instance().log( \
        odbc_crusher::core::LogLevel::INFO, __FILE__, __LINE__, __func__, msg)

#define LOG_WARN(msg) \
    odbc_crusher::core::Logger::instance().log( \
        odbc_crusher::core::LogLevel::WARN, __FILE__, __LINE__, __func__, msg)

#define LOG_ERROR(msg) \
    odbc_crusher::core::Logger::instance().log( \
        odbc_crusher::core::LogLevel::ERROR, __FILE__, __LINE__, __func__, msg)

#define LOG_FATAL(msg) \
    odbc_crusher::core::Logger::instance().log( \
        odbc_crusher::core::LogLevel::FATAL, __FILE__, __LINE__, __func__, msg)

// Log branch decisions (IF statements)
#define LOG_IF(condition, true_msg, ...) \
    odbc_crusher::core::Logger::instance().log_branch( \
        (condition), __FILE__, __LINE__, __func__, \
        true_msg, ##__VA_ARGS__)
