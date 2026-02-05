#include <gtest/gtest.h>
#include "core/odbc_environment.hpp"

using namespace odbc_crusher::core;

TEST(OdbcEnvironmentTest, ConstructorDoesNotThrow) {
    EXPECT_NO_THROW({
        OdbcEnvironment env;
    });
}

TEST(OdbcEnvironmentTest, GetHandleReturnsNonNull) {
    OdbcEnvironment env;
    EXPECT_NE(env.get_handle(), static_cast<SQLHENV>(SQL_NULL_HENV));
}

TEST(OdbcEnvironmentTest, MoveConstructor) {
    OdbcEnvironment env1;
    SQLHENV handle = env1.get_handle();
    
    OdbcEnvironment env2(std::move(env1));
    
    EXPECT_EQ(env2.get_handle(), handle);
    EXPECT_EQ(env1.get_handle(), static_cast<SQLHENV>(SQL_NULL_HENV));
}

TEST(OdbcEnvironmentTest, MoveAssignment) {
    OdbcEnvironment env1;
    OdbcEnvironment env2;
    
    SQLHENV handle = env1.get_handle();
    
    env2 = std::move(env1);
    
    EXPECT_EQ(env2.get_handle(), handle);
    EXPECT_EQ(env1.get_handle(), static_cast<SQLHENV>(SQL_NULL_HENV));
}
