// Test Tool Registry and Builtin Tools
#include "test_framework.hpp"
#include "tools/ToolRegistry.hpp"

using namespace agent;

// Declare builtin tools registration
namespace agent { void register_builtin_tools(); }

TEST(tool_registry_singleton) {
    auto& registry = ToolRegistry::instance();
    auto& registry2 = ToolRegistry::instance();
    
    test::assert_true(&registry == &registry2, "Registry should be a singleton");
}

TEST(list_builtin_tools) {
    // Register built-in tools
    register_builtin_tools();
    
    auto tools = ToolRegistry::instance().list_tools();
    
    test::assert_true(!tools.empty(), "Should have built-in tools");
    
    // Check for expected tools
    bool has_file_read = false, has_file_write = false, has_execute = false, has_web_search = false;
    for (const auto& t : tools) {
        if (t == "file_read") has_file_read = true;
        if (t == "file_write") has_file_write = true;
        if (t == "execute") has_execute = true;
        if (t == "web_search") has_web_search = true;
    }
    
    test::assert_true(has_file_read, "Should have file_read tool");
    test::assert_true(has_file_write, "Should have file_write tool");
    test::assert_true(has_execute, "Should have execute tool");
    test::assert_true(has_web_search, "Should have web_search tool");
}

TEST(execute_get_time) {
    register_builtin_tools();
    
    json args = json::object();
    auto result = ToolRegistry::instance().execute_tool("get_time", args);
    
    test::assert_true(result.success, "get_time should succeed");
    test::assert_true(result.data.contains("datetime"), "Result should have datetime");
    test::assert_true(result.data.contains("timestamp"), "Result should have timestamp");
}

TEST(execute_list_directory) {
    register_builtin_tools();
    
    json args = {{"path", "."}};
    auto result = ToolRegistry::instance().execute_tool("list_directory", args);
    
    test::assert_true(result.success, "list_directory should succeed on current dir");
    test::assert_true(result.data.contains("entries"), "Result should have entries");
    test::assert_true(result.data["entries"].is_array(), "Entries should be an array");
}

TEST(execute_file_read_not_found) {
    register_builtin_tools();
    
    json args = {{"path", "/nonexistent/file/that/does/not/exist.txt"}};
    auto result = ToolRegistry::instance().execute_tool("file_read", args);
    
    test::assert_false(result.success, "file_read should fail for nonexistent file");
    test::assert_contains(result.output, "not found");
}

TEST(tool_schema_format) {
    register_builtin_tools();
    
    auto schemas = ToolRegistry::instance().get_tool_schemas();
    
    test::assert_true(!schemas.empty(), "Should have tool schemas");
    
    // Check schema format (OpenAI compatible)
    for (const auto& schema : schemas) {
        test::assert_true(schema.contains("type"), "Schema should have type");
        test::assert_true(schema["type"] == "function", "Schema type should be function");
        test::assert_true(schema.contains("function"), "Schema should have function");
        
        auto func = schema["function"];
        test::assert_true(func.contains("name"), "Function should have name");
        test::assert_true(func.contains("description"), "Function should have description");
        test::assert_true(func.contains("parameters"), "Function should have parameters");
    }
}
