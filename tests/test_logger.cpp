#include <gtest/gtest.h>
#include "core/logger.hpp"
#include <fstream>
#include <filesystem>

using namespace odbc_crusher::core;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any existing log file
        if (std::filesystem::exists("test.log")) {
            std::filesystem::remove("test.log");
        }
        
        // Reset logger to default state
        Logger::instance().set_level(LogLevel::TRACE);
        Logger::instance().set_console_enabled(false); // Don't clutter test output
    }
    
    void TearDown() override {
        // Clean up
        Logger::instance().set_output(""); // Close file
        if (std::filesystem::exists("test.log")) {
            std::filesystem::remove("test.log");
        }
    }
};

TEST_F(LoggerTest, BasicLogging) {
    Logger::instance().set_output("test.log");
    
    LOG_INFO("Test info message");
    LOG_WARN("Test warning message");
    LOG_ERROR("Test error message");
    
    // Verify file was created and contains messages
    ASSERT_TRUE(std::filesystem::exists("test.log"));
    
    std::ifstream file("test.log");
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    EXPECT_NE(content.find("Test info message"), std::string::npos);
    EXPECT_NE(content.find("Test warning message"), std::string::npos);
    EXPECT_NE(content.find("Test error message"), std::string::npos);
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger::instance().set_output("test.log");
    Logger::instance().set_level(LogLevel::WARN);
    
    LOG_TRACE("Should not appear");
    LOG_DEBUG("Should not appear");
    LOG_INFO("Should not appear");
    LOG_WARN("Should appear");
    LOG_ERROR("Should appear");
    
    std::ifstream file("test.log");
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    EXPECT_EQ(content.find("Should not appear"), std::string::npos);
    EXPECT_NE(content.find("Should appear"), std::string::npos);
}

TEST_F(LoggerTest, BranchLogging) {
    Logger::instance().set_output("test.log");
    Logger::instance().set_level(LogLevel::DEBUG);
    
    bool condition_true = true;
    bool condition_false = false;
    
    LOG_IF(condition_true, "Condition was true", "Condition was false");
    LOG_IF(condition_false, "Condition was true", "Condition was false");
    
    std::ifstream file("test.log");
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    
    EXPECT_NE(content.find("BRANCH: TRUE"), std::string::npos);
    EXPECT_NE(content.find("BRANCH: FALSE"), std::string::npos);
    EXPECT_NE(content.find("Condition was true"), std::string::npos);
    EXPECT_NE(content.find("Condition was false"), std::string::npos);
}
