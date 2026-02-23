#include "guard/guard.h"

extern "C" {
#include <tcbase/tc_log.h>
}

#include <cstring>
#include <string>
#include <vector>

// Captured log messages for testing
struct LogEntry {
    tc_log_level level;
    std::string message;
};

static std::vector<LogEntry> g_captured;

static void test_log_callback(tc_log_level level, const char* message) {
    g_captured.push_back({level, message});
}

TEST_CASE("log callback receives messages") {
    g_captured.clear();
    tc_log_set_callback(test_log_callback);
    tc_log_set_level(TC_LOG_DEBUG);

    tc_log_info("hello %d", 42);

    REQUIRE(g_captured.size() >= 1);
    CHECK_EQ(g_captured.back().level, (int)TC_LOG_INFO);
    CHECK_EQ(g_captured.back().message, std::string("hello 42"));

    tc_log_set_callback(nullptr);
}

TEST_CASE("log level filtering") {
    g_captured.clear();
    tc_log_set_callback(test_log_callback);
    tc_log_set_level(TC_LOG_WARN);

    tc_log_debug("should not appear");
    tc_log_info("should not appear");
    tc_log_warn("warning");
    tc_log_error("error");

    // Only WARN and ERROR should be captured
    CHECK_EQ(g_captured.size(), 2u);
    CHECK_EQ(g_captured[0].level, (int)TC_LOG_WARN);
    CHECK_EQ(g_captured[1].level, (int)TC_LOG_ERROR);

    // Restore defaults
    tc_log_set_level(TC_LOG_DEBUG);
    tc_log_set_callback(nullptr);
}

TEST_CASE("log shorthand functions use correct levels") {
    g_captured.clear();
    tc_log_set_callback(test_log_callback);
    tc_log_set_level(TC_LOG_DEBUG);

    tc_log_debug("d");
    tc_log_info("i");
    tc_log_warn("w");
    tc_log_error("e");

    REQUIRE(g_captured.size() >= 4);
    CHECK_EQ(g_captured[0].level, (int)TC_LOG_DEBUG);
    CHECK_EQ(g_captured[1].level, (int)TC_LOG_INFO);
    CHECK_EQ(g_captured[2].level, (int)TC_LOG_WARN);
    CHECK_EQ(g_captured[3].level, (int)TC_LOG_ERROR);

    tc_log_set_callback(nullptr);
}

TEST_CASE("log format string with multiple args") {
    g_captured.clear();
    tc_log_set_callback(test_log_callback);
    tc_log_set_level(TC_LOG_DEBUG);

    tc_log_error("x=%d y=%.1f name=%s", 10, 3.5, "test");

    REQUIRE(g_captured.size() >= 1);
    CHECK_EQ(g_captured.back().message, std::string("x=10 y=3.5 name=test"));

    tc_log_set_callback(nullptr);
}

TEST_CASE("log works without callback (no crash)") {
    tc_log_set_callback(nullptr);
    tc_log_set_level(TC_LOG_DEBUG);

    // Should not crash, just prints to stderr
    tc_log_info("no callback test");
    tc_log_error("no callback error");
}

TEST_CASE("log generic function with explicit level") {
    g_captured.clear();
    tc_log_set_callback(test_log_callback);
    tc_log_set_level(TC_LOG_DEBUG);

    tc_log(TC_LOG_WARN, "explicit warn %s", "msg");

    REQUIRE(g_captured.size() >= 1);
    CHECK_EQ(g_captured.back().level, (int)TC_LOG_WARN);
    CHECK_EQ(g_captured.back().message, std::string("explicit warn msg"));

    tc_log_set_callback(nullptr);
}
