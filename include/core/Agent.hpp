#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <future>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace agent {

// Forward declarations
class LLMClient;
class ToolRegistry;
class SkillRegistry;
class MCPClient;

// Message types
struct Message {
    enum class Role { System, User, Assistant, Tool } role;
    std::string content;
    std::string name;  // For tool messages
    std::string tool_call_id;
    
    nlohmann::json to_json() const;
    static Message from_json(const nlohmann::json& j);
};

// Tool call structure
struct ToolCall {
    std::string id;
    std::string type;
    std::string function_name;
    nlohmann::json arguments;
};

// Agent configuration
struct AgentConfig {
    std::string api_key;
    std::string api_base = "https://api.openai.com/v1";
    std::string model = "gpt-4";
    std::string system_prompt = "You are a helpful AI assistant.";
    int max_tokens = 4096;
    double temperature = 0.7;
    bool enable_tools = true;
    bool enable_skills = true;
};

// Main Agent class
class Agent {
public:
    explicit Agent(const AgentConfig& config);
    ~Agent();
    
    // Core functionality
    std::future<std::string> chat(const std::string& user_input);
    std::future<std::string> chat_stream(
        const std::string& user_input,
        std::function<void(const std::string&)> callback
    );
    
    // Task execution
    std::future<std::string> execute_task(const std::string& task_description);
    
    // Tool management
    void register_tool(std::shared_ptr<class Tool> tool);
    void unregister_tool(const std::string& name);
    std::vector<std::string> list_tools() const;
    
    // Skill management
    void register_skill(std::shared_ptr<class Skill> skill);
    void load_skill_from_directory(const std::string& path);
    std::vector<std::string> list_skills() const;
    
    // MCP support
    void connect_mcp_server(const std::string& endpoint);
    
    // Conversation management
    void clear_conversation();
    void set_system_prompt(const std::string& prompt);
    std::vector<Message> get_conversation_history() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agent
