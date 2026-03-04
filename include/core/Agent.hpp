#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

namespace agent {

namespace asio = boost::asio;

// Forward declarations
class LLMClient;
class ToolRegistry;
class SkillRegistry;

// Message types
struct Message {
    enum class Role { System, User, Assistant, Tool } role;
    std::string content;
    std::string name;  // For tool messages
    std::string tool_call_id;
    
    // Serialization
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

// Chat result for streaming
struct ChatResult {
    std::string content;
    std::string finish_reason;
    std::vector<nlohmann::json> tool_calls;
    int prompt_tokens = 0;
    int completion_tokens = 0;
};

// Main Agent class with ASIO coroutine support
class Agent {
public:
    // Constructor with io_context for async operations
    explicit Agent(asio::io_context& io, const AgentConfig& config);
    ~Agent();
    
    // Non-copyable
    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    
    // Movable
    Agent(Agent&&) noexcept = default;
    Agent& operator=(Agent&&) noexcept = default;
    
    // ===== Core functionality (coroutine-based) =====
    
    /// Chat with the agent using ASIO coroutine (yield context)
    /// Usage: co_await agent.chat(user_input, yield);
    asio::awaitable<std::string> chat(
        const std::string& user_input,
        asio::yield_context yield
    );
    
    /// Chat with streaming callback
    /// The callback receives partial responses as they arrive
    asio::awaitable<std::string> chat_stream(
        const std::string& user_input,
        std::function<void(const std::string&)> on_chunk,
        asio::yield_context yield
    );
    
    /// Execute a multi-step task
    asio::awaitable<std::string> execute_task(
        const std::string& task_description,
        asio::yield_context yield
    );
    
    // ===== Synchronous wrappers (for backward compatibility) =====
    
    /// Synchronous chat - blocks until complete
    std::string chat_sync(const std::string& user_input);
    
    // ===== Tool management =====
    
    void register_tool(std::shared_ptr<class Tool> tool);
    void unregister_tool(const std::string& name);
    std::vector<std::string> list_tools() const;
    
    // ===== Skill management =====
    
    void register_skill(std::shared_ptr<class Skill> skill);
    void load_skill_from_directory(const std::string& path);
    std::vector<std::string> list_skills() const;
    
    // ===== MCP support =====
    
    void connect_mcp_server(const std::string& endpoint);
    
    // ===== Conversation management =====
    
    void clear_conversation();
    void set_system_prompt(const std::string& prompt);
    std::vector<Message> get_conversation_history() const;
    
    // ===== Accessors =====
    
    asio::io_context& get_io_context() { return io_; }
    const AgentConfig& get_config() const { return config_; }
    
private:
    // Internal coroutine helpers
    asio::awaitable<ChatResult> chat_internal(
        const std::vector<nlohmann::json>& messages,
        asio::yield_context yield
    );
    
    asio::awaitable<std::string> process_tool_calls(
        const std::vector<nlohmann::json>& tool_calls,
        std::vector<nlohmann::json>& messages,
        asio::yield_context yield
    );
    
    asio::io_context& io_;
    AgentConfig config_;
    std::unique_ptr<LLMClient> llm_client_;
    std::vector<Message> conversation_;
};

} // namespace agent
