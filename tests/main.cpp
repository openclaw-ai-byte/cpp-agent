// cpp-agent unit tests main entry
// Includes test framework and runs all registered tests

#include "test_framework.hpp"

// Include all test files here
#include "test_json.cpp"
#include "test_message.cpp"
#include "test_tools.cpp"
#include "test_skills.cpp"

int main() {
    return test::TestRunner::instance().run_all();
}
