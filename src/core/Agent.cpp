#include "core/Agent.hpp"
#include "core/LLMClient.hpp"
#include "tools/ToolRegistry.hpp"
#include "skills/SkillRegistry.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

namespace asio = boost::asio;

// ===== Message serialization =====

nlohmann::json Message::to_json() const {
    nlohmann::json j;
    j["role"] = [](Role r) -> std::string {
        switch(r) {
            case Role::System: return "system";
            case Role::User: return "user";
            case Role::Assistant: return "assistant";
            case Role::Tool: return "tool";
        }
        return "user";
    }(role);
    j["content"] = content;
    if (!name.empty()) j["name"] = name;
    if (!tool_call_id.empty()) j["tool_call_id"] = tool_call_id;
    return j;
}

Message Message::from_json(const nlohmann::json& j) {
    Message msg;
    std::string role_str = j["role"];
    msg.role = [](const std::string& s) -> Role {
        if (s == "system") return Role::System;
        if (s == "assistant") return Role::Assistant;
        if (s == "tool") return Role::Tool;
        return Role::User;
    }(role_str);
    msg.content = j.value("content", "");
    msg.name = j.value("name", "");
    msg.tool_call_id = j.value("tool_call_id", "");
    return msg;
}

// ===== Agent implementation =====

Agent::Agent(asio::io_context& io, const AgentConfig& config)
    : io_(io)
    , config_(config)
    , llm_client_(std::make_unique<LLMClient>(LLMConfig{
        config.api_key,
        config.api_base,
        config.model,
        config.max_tokens,
        config.temperature,
        3,      // max_retries
        1000,   // retry_delay_ms
        120,    // timeout_seconds
        5,      // connection_pool_size
        true    // enable_streaming
    }))
{
    conversation_.push_back({Message::Role::System, config.system_prompt, "", ""});
    spdlog::info("Agent initialized with model: {}", config_.model);
}

Agent::~Agent() = default;

// ===== C++20 Coroutine-based chat =====

asio::awaitable<std::string> Agent::chat(const std::string& user_input) {
    conversation_.push_back({Message::Role::User, user_input, "", ""});
    
    std::vector<nlohmann::json> messages;
    for (const auto& msg : conversation_) {
        messages.push_back(msg.to_json());
    }
    
    auto tools = ToolRegistry::instance().get_tool_schemas();
    std::string final_response;
    int max_iterations = 10;
    
    for (int i = 0; i < max_iterations; ++i) {
        auto executor = co_await asio::this_coro::executor;
        auto tools_copy = tools;
        auto messages_copy = messages;
        
        ChatResult result = co_await asio::co_spawn(
            executor,
            [this, messages_copy, tools_copy]() -> asio::awaitable<ChatResult> {
                co_return chat_internal(messages_copy);
            },
            asio::use_awaitable
        );
        
        if (!result.success) {
            final_response = "Error: " + result.error_message;
            break;
        }
        
        final_response = result.content;
        
        if (result.tool_calls.empty()) {
            break;
        }
        
        auto assistant_msg = nlohmann::json{
            {"role", "assistant"},
            {"content", result.content}
        };
        if (!result.tool_calls.empty()) {
            assistant_msg["tool_calls"] = result.tool_calls;
        }
        messages.push_back(assistant_msg);
        
        process_tool_calls(result.tool_calls, messages);
    }
    
    conversation_.push_back({Message::Role::Assistant, final_response, "", ""});
    co_return final_response;
}

asio::awaitable<std::string> Agent::chat_stream(
    const std::string& user_input,
    std::function<void(const std::string&)> on_chunk) {
    
    conversation_.push_back({Message::Role::User, user_input, "", ""});
    
    std::vector<nlohmann::json> messages;
    for (const auto& msg : conversation_) {
        messages.push_back(msg.to_json());
    }
    
    auto tools = ToolRegistry::instance().get_tool_schemas();
    std::string final_response;
    
    auto executor = co_await asio::this_coro::executor;
    auto tools_copy = tools;
    auto messages_copy = messages;
    
    ChatResponse result = co_await asio::co_spawn(
        executor,
        [this, messages_copy, tools_copy, on_chunk]() -> asio::awaitable<ChatResponse> {
            co_return llm_client_->chat_stream(
                messages_copy, 
                tools_copy,
                [on_chunk](const StreamChunk& chunk) -> bool {
                    if (on_chunk && !chunk.delta.empty()) {
                        on_chunk(chunk.delta);
                    }
                    return true;  // Continue streaming
                }
            );
        },
        asio::use_awaitable
    );
    
    if (!result.success) {
        final_response = "Error: " + result.error_message;
    } else {
        final_response = result.content;
    }
    
    conversation_.push_back({Message::Role::Assistant, final_response, "", ""});
    co_return final_response;
}

asio::awaitable<std::string> Agent::execute_task(const std::string& task_description) {
    std::string prompt = 
        "You are a task executor. Break down and execute the following task step by step.\n\n"
        "Task: " + task_description + "\n\n"
        "Use available tools as needed. Report progress and results clearly.";
    co_return co_await chat(prompt);
}

// ===== Synchronous wrapper =====

std::string Agent::chat_sync(const std::string& user_input) {
    conversation_.push_back({Message::Role::User, user_input, "", ""});
    
    std::vector<nlohmann::json> messages;
    for (const auto& msg : conversation_) {
        messages.push_back(msg.to_json());
    }
    
    auto tools = ToolRegistry::instance().get_tool_schemas();
    std::string final_response;
    int max_iterations = 10;
    
    for (int i = 0; i < max_iterations; ++i) {
        ChatResult result = chat_internal(messages);
        
        if (!result.success) {
            final_response = "Error: " + result.error_message;
            break;
        }
        
        final_response = result.content;
        
        if (result.tool_calls.empty()) {
            break;
        }
        
        auto assistant_msg = nlohmann::json{
            {"role", "assistant"},
            {"content", result.content}
        };
        if (!result.tool_calls.empty()) {
            assistant_msg["tool_calls"] = result.tool_calls;
        }
        messages.push_back(assistant_msg);
        
        process_tool_calls(result.tool_calls, messages);
    }
    
    conversation_.push_back({Message::Role::Assistant, final_response, "", ""});
    return final_response;
}

// ===== Internal helpers =====

ChatResult Agent::chat_internal(const std::vector<nlohmann::json>& messages) {
    auto tools = ToolRegistry::instance().get_tool_schemas();
    ChatResponse response = llm_client_->chat(messages, tools);
    
    ChatResult result;
    result.content = response.content;
    result.finish_reason = response.finish_reason;
    result.tool_calls = response.tool_calls;
    result.prompt_tokens = response.prompt_tokens;
    result.completion_tokens = response.completion_tokens;
    result.success = response.success;
    result.error_message = response.error_message;
    return result;
}

std::string Agent::process_tool_calls(
    const std::vector<nlohmann::json>& tool_calls,
    std::vector<nlohmann::json>& messages) {
    std::string results;
    
    for (const auto& tc : tool_calls) {
        std::string tool_name = tc["function"]["name"];
        nlohmann::json args;
        try {
            args = nlohmann::json::parse(tc["function"]["arguments"].get<std::string>());
        } catch (...) {
            args = nlohmann::json::object();
        }
        std::string tool_id = tc["id"];
        
        spdlog::info("Executing tool: {}", tool_name);
        
        ToolResult result = ToolRegistry::instance().execute_tool(tool_name, args);
        std::string output = result.success ? result.output : "Error: " + result.output;
        results += tool_name + ": " + output + "\n";
        
        messages.push_back({
            {"role", "tool"},
            {"tool_call_id", tool_id},
            {"name", tool_name},
            {"content", output}
        });
    }
    
    return results;
}

// ===== Tool/Skill management =====

void Agent::register_tool(std::shared_ptr<Tool> tool) {
    ToolRegistry::instance().add_tool(tool);
}

void Agent::unregister_tool(const std::string& name) {
    ToolRegistry::instance().unregister_tool(name);
}

std::vector<std::string> Agent::list_tools() const {
    return ToolRegistry::instance().list_tools();
}

void Agent::register_skill(std::shared_ptr<Skill> skill) {
    SkillRegistry::instance().register_skill(skill);
}

void Agent::load_skill_from_directory(const std::string& path) {
    SkillRegistry::instance().load_from_directory(path);
}

std::vector<std::string> Agent::list_skills() const {
    return SkillRegistry::instance().list_skills();
}

// ===== Conversation management =====

void Agent::clear_conversation() {
    conversation_.clear();
    conversation_.push_back({Message::Role::System, config_.system_prompt, "", ""});
}

void Agent::set_system_prompt(const std::string& prompt) {
    config_.system_prompt = prompt;
    if (!conversation_.empty() && conversation_[0].role == Message::Role::System) {
        conversation_[0].content = prompt;
    }
}

std::vector<Message> Agent::get_conversation_history() const {
    return conversation_;
}

} // namespace agent
