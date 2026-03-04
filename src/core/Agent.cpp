#include "core/Agent.hpp"
#include "core/LLMClient.hpp"
#include "tools/ToolRegistry.hpp"
#include <spdlog/spdlog.h>
#include <future>

namespace agent {

class Agent::Impl {
public:
    Impl(const AgentConfig& config)
        : config_(config)
        , llm_client_(std::make_unique<LLMClient>(LLMConfig{
            config.api_key, config.api_base, config.model,
            config.max_tokens, config.temperature
        }))
    {
        messages_.push_back(Message{Message::Role::System, config.system_prompt, "", ""});
    }
    
    std::future<std::string> chat(const std::string& user_input) {
        return std::async(std::launch::async, [this, user_input]() {
            messages_.push_back(Message{Message::Role::User, user_input, "", ""});
            
            std::vector<nlohmann::json> tools;
            if (config_.enable_tools) {
                tools = ToolRegistry::instance().get_tool_schemas();
            }
            
            std::vector<nlohmann::json> messages_json;
            for (const auto& msg : messages_) {
                messages_json.push_back(msg.to_json());
            }
            
            auto response = llm_client_->chat(messages_json, tools);
            
            if (!response.tool_calls.empty()) {
                nlohmann::json assistant_msg = {
                    {"role", "assistant"},
                    {"content", response.content},
                    {"tool_calls", response.tool_calls}
                };
                messages_json.push_back(assistant_msg);
                
                for (const auto& tc : response.tool_calls) {
                    std::string tool_name = tc["function"]["name"];
                    nlohmann::json args = nlohmann::json::parse(tc["function"]["arguments"].get<std::string>());
                    std::string tool_id = tc["id"];
                    
                    spdlog::info("Executing tool: {}", tool_name);
                    auto result = ToolRegistry::instance().execute_tool(tool_name, args);
                    
                    messages_json.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_id},
                        {"name", tool_name},
                        {"content", result.output}
                    });
                }
                
                response = llm_client_->chat(messages_json, tools);
            }
            
            messages_.push_back(Message{Message::Role::Assistant, response.content, "", ""});
            return response.content;
        });
    }
    
    void clear_conversation() {
        messages_.clear();
        messages_.push_back(Message{Message::Role::System, config_.system_prompt, "", ""});
    }
    
    std::vector<Message> get_history() const { return messages_; }

private:
    AgentConfig config_;
    std::unique_ptr<LLMClient> llm_client_;
    std::vector<Message> messages_;
};

Agent::Agent(const AgentConfig& config) : impl_(std::make_unique<Impl>(config)) {}
Agent::~Agent() = default;

std::future<std::string> Agent::chat(const std::string& user_input) {
    return impl_->chat(user_input);
}

void Agent::clear_conversation() { impl_->clear_conversation(); }
std::vector<Message> Agent::get_conversation_history() const { return impl_->get_history(); }

nlohmann::json Message::to_json() const {
    nlohmann::json j = {{"role", []() {
        switch (role) {
            case Role::System: return "system";
            case Role::User: return "user";
            case Role::Assistant: return "assistant";
            case Role::Tool: return "tool";
        }
    }()}, {"content", content}};
    if (!name.empty()) j["name"] = name;
    if (!tool_call_id.empty()) j["tool_call_id"] = tool_call_id;
    return j;
}

Message Message::from_json(const nlohmann::json& j) {
    Message msg;
    std::string role_str = j["role"];
    if (role_str == "system") msg.role = Role::System;
    else if (role_str == "user") msg.role = Role::User;
    else if (role_str == "assistant") msg.role = Role::Assistant;
    else if (role_str == "tool") msg.role = Role::Tool;
    msg.content = j.value("content", "");
    msg.name = j.value("name", "");
    msg.tool_call_id = j.value("tool_call_id", "");
    return msg;
}

} // namespace agent
