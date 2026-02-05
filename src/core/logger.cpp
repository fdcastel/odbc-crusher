#include "logger.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>

namespace odbc_crusher::core {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() = default;

Logger::~Logger() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

void Logger::set_output(std::string_view filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
    
    if (!filename.empty()) {
        file_stream_.open(std::string(filename), std::ios::app);
    }
}

void Logger::set_console_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    console_enabled_ = enabled;
}

void Logger::log(LogLevel level, std::string_view file, int line,
                std::string_view function, std::string_view message) {
    if (level < min_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Format: [TIMESTAMP] [LEVEL] [file:line] [function] message
    std::ostringstream oss;
    oss << "[" << timestamp() << "] "
        << "[" << std::setw(5) << level_to_string(level) << "] "
        << "[" << file << ":" << line << "] "
        << "[" << function << "] "
        << message;
    
    std::string formatted = oss.str();
    
    if (console_enabled_) {
        if (level >= LogLevel::ERROR) {
            std::cerr << formatted << std::endl;
        } else {
            std::cout << formatted << std::endl;
        }
    }
    
    if (file_stream_.is_open()) {
        file_stream_ << formatted << std::endl;
        file_stream_.flush(); // Ensure immediate write
    }
}

void Logger::log_branch(bool condition, std::string_view file, int line,
                       std::string_view function,
                       std::string_view true_msg,
                       std::string_view false_msg) {
    if (LogLevel::DEBUG < min_level_) {
        return;
    }
    
    std::ostringstream oss;
    oss << "BRANCH: " << (condition ? "TRUE" : "FALSE") << " - ";
    
    if (condition) {
        oss << true_msg;
    } else if (!false_msg.empty()) {
        oss << false_msg;
    } else {
        oss << "condition false";
    }
    
    log(LogLevel::DEBUG, file, line, function, oss.str());
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "?????";
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

} // namespace odbc_crusher::core
