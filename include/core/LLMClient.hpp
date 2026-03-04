#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace agent {

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
    
    ChatResponse chat(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools = {}
    );
    
    void chat_stream(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        std::function<void(const std::string&)> on_chunk,
        std::function<void(const ChatResponse&)> on_complete
    );
    
    std::vector<float> embed(const std::string& text);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace agent
