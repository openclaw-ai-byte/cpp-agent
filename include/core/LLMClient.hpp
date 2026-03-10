#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <optional>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

namespace agent {

namespace asio = boost::asio;

// Configuration
struct LLMConfig {
    std::string api_key;
    std::string api_base = "https://api.openai.com/v1";
    std::string model = "gpt-4";
    int max_tokens = 4096;
    double temperature = 0.7;
    
    // Retry settings
    int max_retries = 3;
    int retry_delay_ms = 1000;
    int timeout_seconds = 120;
    
    // Connection pool
    int connection_pool_size = 5;
    bool enable_streaming = true;
};

// Response types
struct ChatResponse {
    std::string content;
    std::vector<nlohmann::json> tool_calls;
    std::string finish_reason;
    int prompt_tokens = 0;
    int completion_tokens = 0;
    bool success = true;
    std::string error_message;
    
    static ChatResponse ok(const std::string& c) { return ChatResponse{.content = c, .success = true}; }
    static ChatResponse err(const std::string& msg) { return ChatResponse{.success = false, .error_message = msg}; }
};

// Streaming chunk
struct StreamChunk {
    std::string delta;           // Content delta
    std::string finish_reason;   // "stop", "tool_calls", etc.
    std::optional<nlohmann::json> tool_call_delta;  // Tool call delta if any
    bool done = false;           // End of stream
};

// Error types
enum class LLMError {
    None = 0,
    NetworkError,
    AuthError,
    RateLimitError,
    InvalidResponse,
    TimeoutError,
    Cancelled
};

struct LLMException : std::runtime_error {
    LLMError error_type;
    int http_code;
    
    LLMException(LLMError type, const std::string& msg, int http = 0)
        : std::runtime_error(msg), error_type(type), http_code(http) {}
};

// Connection pool for HTTP connections
class ConnectionPool {
public:
    explicit ConnectionPool(size_t pool_size = 5);
    ~ConnectionPool();
    
    // Get a connection (blocking)
    void* acquire();
    
    // Return a connection to the pool
    void release(void* conn);
    
    // Get pool stats
    size_t available() const;
    size_t in_use() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Main LLM Client
class LLMClient {
public:
    explicit LLMClient(const LLMConfig& config);
    ~LLMClient();
    
    // ===== Synchronous API =====
    
    /// Blocking chat call with retry
    ChatResponse chat(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools = {}
    );
    
    /// Blocking streaming chat - calls on_chunk for each chunk
    ChatResponse chat_stream(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        std::function<bool(const StreamChunk&)> on_chunk  // return false to cancel
    );
    
    // ===== C++20 Coroutine API =====
    
    /// Async chat with automatic retry
    asio::awaitable<ChatResponse> chat_async(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools = {}
    );
    
    /// Async streaming chat
    asio::awaitable<ChatResponse> chat_stream_async(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        std::function<bool(const StreamChunk&)> on_chunk
    );
    
    // ===== Utility =====
    
    /// Get last error
    LLMError last_error() const;
    
    /// Get connection pool stats
    std::pair<size_t, size_t> pool_stats() const;
    
    /// Embeddings
    std::vector<float> embed(const std::string& text);
    
    /// Async embeddings (C++20 coroutine)
    asio::awaitable<std::vector<float>> embed_async(const std::string& text);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agent
