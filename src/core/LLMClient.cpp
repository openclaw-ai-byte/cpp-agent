#include "core/LLMClient.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <cstring>

// libcurl for HTTP requests
#include <curl/curl.h>

namespace agent {

// Callback for curl write
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class LLMClient::Impl {
public:
    Impl(const LLMConfig& config) : config_(config) {
        curl_global_init(CURL_GLOBAL_ALL);
        spdlog::info("LLMClient initialized with model: {}", config_.model);
    }
    
    ~Impl() {
        curl_global_cleanup();
    }
    
    ChatResponse chat(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools
    ) {
        nlohmann::json body;
        body["model"] = config_.model;
        body["messages"] = messages;
        body["max_tokens"] = config_.max_tokens;
        body["temperature"] = config_.temperature;
        
        if (!tools.empty()) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }
        
        std::string response = makeRequest("/chat/completions", body);
        return parseResponse(response);
    }
    
    void chatStream(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        std::function<void(const std::string&)> onChunk,
        std::function<void(const ChatResponse&)> onComplete
    ) {
        // For now, fall back to non-streaming
        auto response = chat(messages, tools);
        onChunk(response.content);
        onComplete(response);
    }
    
    std::vector<float> embed(const std::string& text) {
        nlohmann::json body;
        body["model"] = config_.model;
        body["input"] = text;
        
        std::string response = makeRequest("/embeddings", body);
        auto json = nlohmann::json::parse(response);
        
        std::vector<float> embedding;
        for (const auto& val : json["data"][0]["embedding"]) {
            embedding.push_back(val.get<float>());
        }
        return embedding;
    }
    
private:
    LLMConfig config_;
    
    std::string makeRequest(const std::string& endpoint, const nlohmann::json& body) {
        std::string url = config_.api_base + endpoint;
        std::string requestBody = body.dump();
        std::string responseBody;
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            spdlog::error("Failed to initialize curl");
            return R"({"error": "curl init failed"})";
        }
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string authHeader = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, authHeader.c_str());
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
        
        spdlog::debug("Request to {}: {}", url, requestBody);
        
        CURLcode res = curl_easy_perform(curl);
        
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            spdlog::error("curl request failed: {}", curl_easy_strerror(res));
            return R"({"error": ")" + std::string(curl_easy_strerror(res)) + R"("})";
        }
        
        spdlog::debug("Response: {}", responseBody);
        return responseBody;
    }
    
    ChatResponse parseResponse(const std::string& response) {
        try {
            auto json = nlohmann::json::parse(response);
            ChatResponse result;
            
            if (json.contains("error")) {
                result.content = "Error: " + json["error"]["message"].get<std::string>();
                result.finish_reason = "error";
                return result;
            }
            
            auto& choice = json["choices"][0];
            auto& message = choice["message"];
            
            result.content = message.value("content", "");
            result.finish_reason = choice.value("finish_reason", "stop");
            
            if (message.contains("tool_calls")) {
                result.tool_calls = message["tool_calls"];
            }
            
            if (json.contains("usage")) {
                result.prompt_tokens = json["usage"].value("prompt_tokens", 0);
                result.completion_tokens = json["usage"].value("completion_tokens", 0);
            }
            
            return result;
        } catch (const std::exception& e) {
            spdlog::error("Failed to parse response: {}", e.what());
            return ChatResponse{"Error parsing response: " + std::string(e.what()), {}, "error", 0, 0};
        }
    }
};

LLMClient::LLMClient(const LLMConfig& config) 
    : impl_(std::make_unique<Impl>(config)) {}

LLMClient::~LLMClient() = default;

ChatResponse LLMClient::chat(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools
) {
    return impl_->chat(messages, tools);
}

void LLMClient::chatStream(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    std::function<void(const std::string&)> onChunk,
    std::function<void(const ChatResponse&)> onComplete
) {
    impl_->chatStream(messages, tools, onChunk, onComplete);
}

std::vector<float> LLMClient::embed(const std::string& text) {
    return impl_->embed(text);
}

} // namespace agent
