#include "core/Agent.hpp"
#include "core/LLMClient.hpp"
#include "tools/ToolRegistry.hpp"
#include "skills/SkillRegistry.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

// Message serialization
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

// Agent implementation
class Agent::Impl {
public:
    Impl(const AgentConfig& cfg) 
        : config_(cfg),
          llm_client_(std::make_unique<LLMClient>(LLMConfig{
              cfg.api_key, cfg.api_base, cfg.model,
              cfg.max_tokens, cfg.temperature
          }))
    {
        conversation_.push_back({Message::Role::System, cfg.system_prompt});
    }
    
    std::future<std::string> chat(const std::string& user_input) {
        return std::async(std::launch::async, [this, user_input]() {
            conversation_.push_back({Message::Role::User, user_input});
            
            auto tools = ToolRegistry::instance().get_tool_schemas();
            
            std::vector<nlohmann::json> messages;
            for (const auto& msg : conversation_) {
                messages.push_back(msg.to_json());
            }
            
            int max_iterations = 10;
            std::string final_response;
            
            for (int i = 0; i < max_iterations; ++i) {
                auto response = llm_client_->chat(messages, tools);
                final_response = response.content;
                
                if (response.tool_calls.empty()) {
                    break;
                }
                
                // Process tool calls
                auto assistant_msg = nlohmann::json{
                    {"role", "assistant"},
                    {"content", response.content}
                };
                if (!response.tool_calls.empty()) {
                    assistant_msg["tool_calls"] = response.tool_calls;
                }
                messages.push_back(assistant_msg);
                
                for (const auto& tc : response.tool_calls) {
                    std::string tool_name = tc["function"]["name"];
                    nlohmann::json args = nlohmann::json::parse(tc["function"]["arguments"].get<std::string>());
                    std::string tool_id = tc["id"];
                    
                    spdlog::info("Executing tool: {}", tool_name);
                    auto result = ToolRegistry::instance().execute_tool(tool_name, args);
                    
                    messages.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_id},
                        {"name", tool_name},
                        {"content", result.success ? result.output : "Error: " + result.output}
                    });
                }
            }
            
            conversation_.push_back({Message::Role::Assistant, final_response});
            return final_response;
        });
    }
    
    std::future<std::string> execute_task(const std::string& task_description) {
        return std::async(std::launch::async, [this, task_description]() {
            std::string enhanced_prompt = 
                "You are a task executor. Break down and execute the following task step by step.\n\n"
                "Task: " + task_description + "\n\n"
                "Use available tools as needed. Report progress and results clearly.";
            return chat(enhanced_prompt).get();
        });
    }
    
    void clear_conversation() {
        conversation_.clear();
        conversation_.push_back({Message::Role::System, config_.system_prompt});
    }
    
    void set_system_prompt(const std::string& prompt) {
        config_.system_prompt = prompt;
        if (!conversation_.empty() && conversation_[0].role == Message::Role::System) {
            conversation_[0].content = prompt;
        }
    }
    
    std::vector<Message> get_conversation_history() const {
        return conversation_;
    }
    
    AgentConfig config_;
    std::unique_ptr<LLMClient> llm_client_;
    std::vector<Message> conversation_;
};

Agent::Agent(const AgentConfig& config) 
    : impl_(std::make_unique<Impl>(config)) {}

Agent::~Agent() = default;

std::future<std::string> Agent::chat(const std::string& user_input) {
    return impl_->chat(user_input);
}

std::future<std::string> Agent::execute_task(const std::string& task_description) {
    return impl_->execute_task(task_description);
}

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

void Agent::clear_conversation() {
    impl_->clear_conversation();
}

void Agent::set_system_prompt(const std::string& prompt) {
    impl_->set_system_prompt(prompt);
}

std::vector<Message> Agent::get_conversation_history() const {
    return impl_->get_conversation_history();
}

} // namespace agent
