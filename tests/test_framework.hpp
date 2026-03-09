// cpp-agent unit test framework
// Lightweight test framework - no external dependencies

#pragma once

#include <iostream>
#include <vector>
#include <functional>
#include <string>
#include <sstream>
#include <chrono>

namespace test {

struct TestResult {
    std::string name;
    bool passed;
    std::string message;
    double duration_ms;
};

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner runner;
        return runner;
    }
    
    void add_test(const std::string& name, std::function<void()> test) {
        tests_.push_back({name, test});
    }
    
    int run_all() {
        std::cout << "\n========================================\n";
        std::cout << "  Running " << tests_.size() << " tests\n";
        std::cout << "========================================\n\n";
        
        int passed = 0;
        int failed = 0;
        
        for (const auto& [name, test] : tests_) {
            std::cout << "  " << name << "... " << std::flush;
            
            auto start = std::chrono::high_resolution_clock::now();
            bool success = false;
            std::string error;
            
            try {
                test();
                success = true;
                passed++;
                std::cout << "\033[32mPASSED\033[0m\n";
            } catch (const std::exception& e) {
                error = e.what();
                failed++;
                std::cout << "\033[31mFAILED\033[0m\n";
                std::cout << "    Error: " << error << "\n";
            } catch (...) {
                failed++;
                std::cout << "\033[31mFAILED\033[0m\n";
                std::cout << "    Error: Unknown exception\n";
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
        }
        
        std::cout << "\n========================================\n";
        std::cout << "  Results: " << passed << " passed, " << failed << " failed\n";
        std::cout << "========================================\n\n";
        
        return failed;
    }
    
private:
    std::vector<std::pair<std::string, std::function<void()>>> tests_;
};

// Assertion helpers
inline void assert_true(bool cond, const std::string& msg = "") {
    if (!cond) {
        throw std::runtime_error("Assertion failed: " + (msg.empty() ? "expected true" : msg));
    }
}

inline void assert_false(bool cond, const std::string& msg = "") {
    if (cond) {
        throw std::runtime_error("Assertion failed: " + (msg.empty() ? "expected false" : msg));
    }
}

template<typename T>
void assert_eq(const T& a, const T& b, const std::string& msg = "") {
    if (a != b) {
        std::stringstream ss;
        ss << "Assertion failed: " << (msg.empty() ? "values not equal" : msg);
        throw std::runtime_error(ss.str());
    }
}

template<typename T>
void assert_ne(const T& a, const T& b, const std::string& msg = "") {
    if (a == b) {
        std::stringstream ss;
        ss << "Assertion failed: " << (msg.empty() ? "values are equal" : msg);
        throw std::runtime_error(ss.str());
    }
}

inline void assert_contains(const std::string& haystack, const std::string& needle, const std::string& msg = "") {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error("Assertion failed: string does not contain '" + needle + "'");
    }
}

inline void assert_no_throw(std::function<void()> fn, const std::string& msg = "") {
    try {
        fn();
    } catch (const std::exception& e) {
        throw std::runtime_error("Unexpected exception: " + std::string(e.what()));
    }
}

} // namespace test

// Test registration macro
#define TEST(name) \
    void test_##name(); \
    struct TestRegistrar_##name { \
        TestRegistrar_##name() { \
            test::TestRunner::instance().add_test(#name, test_##name); \
        } \
    } test_registrar_##name; \
    void test_##name()
