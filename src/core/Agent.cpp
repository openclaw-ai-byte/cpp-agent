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
        config.temperature
    }))
{
    conversation_.push_back({Message::Role::System, config.system_prompt});
    spdlog::info("Agent initialized with model: {}", config_.model);
}

Agent::~Agent() = default;

// ===== Coroutine-based chat =====

asio::awaitable<std::string> Agent::chat(
    const std::string& user_input,
    asio::yield_context yield
) {
    conversation_.push_back({Message::Role::User, user_input});
    
    std::vector<nlohmann::json> messages;
    for (const auto& msg : conversation_) {
        messages.push_back(msg.to_json());
    }
    
    auto tools = ToolRegistry::instance().get_tool_schemas();
    std::string final_response;
    int max_iterations = 10;
    
    for (int i = 0; i < max_iterations; ++i) {
        auto result = co_await chat_internal(messages, yield);
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
        
        co_await process_tool_calls(result.tool_calls, messages, yield);
    }
    
    conversation_.push_back({Message::Role::Assistant, final_response});
    co_return final_response;
}

asio::awaitable<std::string> Agent::chat_stream(
    const std::string& user_input,
    std::function<void(const std::string&)> on_chunk,
    asio::yield_context yield
) {
    auto response = co_await chat(user_input, yield);
    if (on_chunk) {
        on_chunk(response);
    }
    co_return response;
}

asio::awaitable<std::string> Agent::execute_task(
    const std::string& task_description,
    asio::yield_context yield
) {
    std::string prompt = 
        "You are a task executor. Break down and execute the following task step by step.\n\n"
        "Task: " + task_description + "\n\n"
        "Use available tools as needed. Report progress and results clearly.";
    co_return co_await chat(prompt, yield);
}

// ===== Synchronous wrapper =====

std::string Agent::chat_sync(const std::string& user_input) {
    return asio::co_spawn(io_, chat(user_input, asio::use_awaitable), asio::use_future).get();
}

// ===== Internal helpers =====

asio::awaitable<ChatResult> Agent::chat_internal(
    const std::vector<nlohmann::json>& messages,
    asio::yield_context yield
) {
    auto tools = ToolRegistry::instance().get_tool_schemas();
    auto result = co_await llm_client_->chat_async(messages, tools, yield);
    
    ChatResult cr;
    cr.content = result.content;
    cr.finish_reason = result.finish_reason;
    cr.tool_calls = result.tool_calls;
    cr.prompt_tokens = result.prompt_tokens;
    cr.completion_tokens = result.completion_tokens;
    co_return cr;
}

asio::awaitable<std::string> Agent::process_tool_calls(
    const std::vector<nlohmann::json>& tool_calls,
    std::vector<nlohmann::json>& messages,
    asio::yield_context yield
) {
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
        
        // Execute tool on thread pool
        auto result = co_await asio::post(
            io_,
            asio::use_awaitable(
                [=]() -> ToolResult {
                    return ToolRegistry::instance().execute_tool(tool_name, args);
                }
            )
        );
        
        std::string output = result.success ? result.output : "Error: " + result.output;
        results += tool_name + ": " + output + "\n";
        
        messages.push_back({
            {"role", "tool"},
            {"tool_call_id", tool_id},
            {"name", tool_name},
            {"content", output}
        });
    }
    
    co_return results;
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
    conversation_.push_back({Message::Role::System, config_.system_prompt});
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
