#include "core/LLMClient.hpp"
#include <spdlog/spdlog.h>
#include <curl/curl.h>

namespace agent {

class LLMClient::Impl {
public:
    Impl(const LLMConfig& config) : config_(config) {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    
    ~Impl() {
        curl_global_cleanup();
    }
    
    ChatResponse chat(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools
    ) {
        nlohmann::json body = {
            {"model", config_.model},
            {"messages", messages},
            {"max_tokens", config_.max_tokens},
            {"temperature", config_.temperature}
        };
        
        if (!tools.empty()) {
            body["tools"] = tools;
        }
        
        std::string response_str = make_request(body);
        return parse_response(response_str);
    }
    
    std::string make_request(const nlohmann::json& body, const std::string& endpoint = "/chat/completions") {
        CURL* curl = curl_easy_init();
        if (!curl) throw std::runtime_error("Failed to initialize CURL");
        
        std::string url = config_.api_base + endpoint;
        std::string body_str = body.dump();
        std::string response_str;
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth.c_str());
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* ptr, size_t size, size_t nmemb, std::string* data) {
            data->append((char*)ptr, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_str);
        
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            throw std::runtime_error(std::string("CURL error: ") + curl_easy_strerror(res));
        }
        
        return response_str;
    }
    
    ChatResponse parse_response(const std::string& response) {
        auto json = nlohmann::json::parse(response);
        ChatResponse result;
        
        auto& choice = json["choices"][0];
        result.content = choice["message"]["content"].get<std::string>();
        result.finish_reason = choice.value("finish_reason", "");
        
        if (choice["message"].contains("tool_calls")) {
            result.tool_calls = choice["message"]["tool_calls"];
        }
        
        if (json.contains("usage")) {
            result.prompt_tokens = json["usage"]["prompt_tokens"].get<int>();
            result.completion_tokens = json["usage"]["completion_tokens"].get<int>();
        }
        
        return result;
    }

private:
    LLMConfig config_;
};

LLMClient::LLMClient(const LLMConfig& config) : impl_(std::make_unique<Impl>(config)) {}
LLMClient::~LLMClient() = default;

ChatResponse LLMClient::chat(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools
) {
    return impl_->chat(messages, tools);
}

} // namespace agent
