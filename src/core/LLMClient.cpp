#include "core/LLMClient.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <cstring>
#include <curl/curl.h>
#include <boost/asio.hpp>

namespace agent {

namespace asio = boost::asio;

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
    
    std::string makeRequest(const std::string& endpoint, const nlohmann::json& body) {
        std::string url = config_.api_base + endpoint;
        std::string requestBody = body.dump();
        std::string responseBody;
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            return R"({"error": {"message": "curl init failed"}})";
        }
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth.c_str());
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            return R"({"error": {"message": ")" + std::string(curl_easy_strerror(res)) + R"("}})";
        }
        return responseBody;
    }
    
    ChatResponse parseResponse(const std::string& response) {
        try {
            auto json = nlohmann::json::parse(response);
            ChatResult r;
            if (json.contains("error")) {
                r.content = "Error: " + json["error"]["message"].get<std::string>();
                r.finish_reason = "error";
                return r;
            }
            auto& choice = json["choices"][0];
            auto& msg = choice["message"];
            r.content = msg.value("content", "");
            r.finish_reason = choice.value("finish_reason", "stop");
            if (msg.contains("tool_calls")) r.tool_calls = msg["tool_calls"];
            if (json.contains("usage")) {
                r.prompt_tokens = json["usage"].value("prompt_tokens", 0);
                r.completion_tokens = json["usage"].value("completion_tokens", 0);
            }
            return r;
        } catch (const std::exception& e) {
            return ChatResponse{"Parse error: " + std::string(e.what()), {}, "error", 0, 0};
        }
    }
    
    ChatResponse chat(const std::vector<nlohmann::json>& messages,
                      const std::vector<nlohmann::json>& tools) {
        nlohmann::json body;
        body["model"] = config_.model;
        body["messages"] = messages;
        body["max_tokens"] = config_.max_tokens;
        body["temperature"] = config_.temperature;
        if (!tools.empty()) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }
        return parseResponse(makeRequest("/chat/completions", body));
    }
    
    asio::awaitable<ChatResponse> chatAsync(const std::vector<nlohmann::json>& messages,
                                            const std::vector<nlohmann::json>& tools,
                                            asio::yield_context yield) {
        nlohmann::json body;
        body["model"] = config_.model;
        body["messages"] = messages;
        body["max_tokens"] = config_.max_tokens;
        body["temperature"] = config_.temperature;
        if (!tools.empty()) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }
        
        // Post to thread pool for blocking curl call
        auto self = shared_from_this();
        auto response = co_await asio::post(
            co_await asio::this_coro::executor,
            asio::use_awaitable(
                [this, body]() { return makeRequest("/chat/completions", body); }
            )
        );
        co_return parseResponse(response);
    }
    
private:
    LLMConfig config_;
};

LLMClient::LLMClient(const LLMConfig& config) : impl_(std::make_unique<Impl>(config)) {}
LLMClient::~LLMClient() = default;

ChatResponse LLMClient::chat(const std::vector<nlohmann::json>& messages,
                             const std::vector<nlohmann::json>& tools) {
    return impl_->chat(messages, tools);
}

asio::awaitable<ChatResponse> LLMClient::chat_async(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    asio::yield_context yield) {
    co_return co_await impl_->chatAsync(messages, tools, yield);
}

asio::awaitable<void> LLMClient::chat_stream_async(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    std::function<void(const std::string&)> on_chunk,
    std::function<void(const ChatResponse&)> on_complete,
    asio::yield_context yield) {
    auto r = co_await chat_async(messages, tools, yield);
    if (on_chunk) on_chunk(r.content);
    if (on_complete) on_complete(r);
}

std::vector<float> LLMClient::embed(const std::string& text) {
    // TODO
    return {};
}

asio::awaitable<std::vector<float>> LLMClient::embed_async(const std::string&, asio::yield_context) {
    co_return {};
}

} // namespace agent
