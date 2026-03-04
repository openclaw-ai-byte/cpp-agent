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

struct LLMConfig {
    std::string api_key;
    std::string api_base;
    std::string model;
    int max_tokens;
    double temperature;
};

struct ChatResponse {
    std::string content;
    std::vector<nlohmann::json> tool_calls;
    std::string finish_reason;
    int prompt_tokens;
    int completion_tokens;
};

class LLMClient {
public:
    explicit LLMClient(const LLMConfig& config);
    ~LLMClient();
    
    // ===== Synchronous API (blocking) =====
    
    /// Blocking chat call - use only in simple cases
    ChatResponse chat(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools = {}
    );
    
    // ===== Asynchronous API (coroutine-based) =====
    
    /// Async chat using ASIO coroutine
    /// Usage: auto result = co_await client.chat_async(messages, tools, yield);
    asio::awaitable<ChatResponse> chat_async(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        asio::yield_context yield
    );
    
    /// Async chat with streaming callback
    asio::awaitable<void> chat_stream_async(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        std::function<void(const std::string&)> on_chunk,
        std::function<void(const ChatResponse&)> on_complete,
        asio::yield_context yield
    );
    
    // ===== Embeddings =====
    
    std::vector<float> embed(const std::string& text);
    asio::awaitable<std::vector<float>> embed_async(
        const std::string& text,
        asio::yield_context yield
    );
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agent
