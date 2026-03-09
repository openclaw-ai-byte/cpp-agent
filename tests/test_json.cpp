// Test JSON parsing and manipulation
#include "test_framework.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST(json_parse_string) {
    std::string s = R"({"name": "test", "value": 42})";
    json j = json::parse(s);
    
    test::assert_eq(j["name"].get<std::string>(), std::string("test"));
    test::assert_eq(j["value"].get<int>(), 42);
}

TEST(json_create_object) {
    json j;
    j["key"] = "value";
    j["number"] = 123;
    j["array"] = {1, 2, 3};
    
    test::assert_true(j.contains("key"));
    test::assert_eq(j["number"].get<int>(), 123);
    test::assert_eq(j["array"].size(), size_t(3));
}

TEST(json_nested_access) {
    std::string s = R"({
        "tool": {
            "function": {
                "name": "web_search",
                "arguments": "{\"query\": \"test\"}"
            }
        }
    })";
    
    json j = json::parse(s);
    std::string name = j["tool"]["function"]["name"];
    
    test::assert_eq(name, std::string("web_search"));
    
    // Parse nested JSON string
    json args = json::parse(j["tool"]["function"]["arguments"].get<std::string>());
    test::assert_eq(args["query"].get<std::string>(), std::string("test"));
}

TEST(json_optional_values) {
    json j = {{"existing", "value"}};
    
    test::assert_eq(j.value("existing", "default"), std::string("value"));
    test::assert_eq(j.value("missing", "default"), std::string("default"));
}

TEST(json_array_operations) {
    json arr = json::array();
    arr.push_back("first");
    arr.push_back("second");
    arr.push_back({"nested", "array"});
    
    test::assert_eq(arr.size(), size_t(3));
    test::assert_eq(arr[0].get<std::string>(), std::string("first"));
    test::assert_true(arr[2].is_array());
}
