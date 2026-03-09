// Test Message serialization
#include "test_framework.hpp"
#include "core/Agent.hpp"

using namespace agent;

TEST(message_to_json_user) {
    Message msg;
    msg.role = Message::Role::User;
    msg.content = "Hello, world!";
    
    json j = msg.to_json();
    
    test::assert_eq(j["role"].get<std::string>(), std::string("user"));
    test::assert_eq(j["content"].get<std::string>(), std::string("Hello, world!"));
    test::assert_false(j.contains("name"));
    test::assert_false(j.contains("tool_call_id"));
}

TEST(message_to_json_tool) {
    Message msg;
    msg.role = Message::Role::Tool;
    msg.content = "Result: 42";
    msg.name = "calculator";
    msg.tool_call_id = "call_123";
    
    json j = msg.to_json();
    
    test::assert_eq(j["role"].get<std::string>(), std::string("tool"));
    test::assert_eq(j["name"].get<std::string>(), std::string("calculator"));
    test::assert_eq(j["tool_call_id"].get<std::string>(), std::string("call_123"));
}

TEST(message_from_json) {
    json j = {
        {"role", "assistant"},
        {"content", "Hello!"},
        {"name", "bot"}
    };
    
    Message msg = Message::from_json(j);
    
    test::assert_true(msg.role == Message::Role::Assistant);
    test::assert_eq(msg.content, std::string("Hello!"));
    test::assert_eq(msg.name, std::string("bot"));
}

TEST(message_roundtrip) {
    Message original;
    original.role = Message::Role::System;
    original.content = "You are a helpful assistant.";
    
    json j = original.to_json();
    Message restored = Message::from_json(j);
    
    test::assert_true(restored.role == original.role);
    test::assert_eq(restored.content, original.content);
}
