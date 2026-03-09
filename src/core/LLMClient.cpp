#include "core/LLMClient.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <cstring>
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <atomic>

namespace agent {

namespace asio = boost::asio;

// ===== Connection Pool =====

class ConnectionPool::Impl {
public:
    Impl(size_t size) : max_size_(size) {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    
    ~Impl() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!pool_.empty()) {
            CURL* curl = pool_.front();
            pool_.pop();
            curl_easy_cleanup(curl);
        }
        curl_global_cleanup();
    }
    
    CURL* acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (pool_.empty() && in_use_ < max_size_) {
            in_use_++;
            return curl_easy_init();
        }
        cv_.wait(lock, [this] { return !pool_.empty(); });
        if (pool_.empty()) {
            in_use_++;
            return curl_easy_init();
        }
        auto curl = pool_.front();
        pool_.pop();
        in_use_++;
        curl_easy_reset(curl);
        return curl;
    }
    
    void release(CURL* curl) {
        std::unique_lock<std::mutex> lock(mutex_);
        pool_.push(curl);
        in_use_--;
        cv_.notify_one();
    }
    
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }
    
    size_t in_use() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return in_use_;
    }
    
private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<CURL*> pool_;
    size_t max_size_;
    size_t in_use_ = 0;
};

ConnectionPool::ConnectionPool(size_t pool_size) 
    : impl_(std::make_unique<Impl>(pool_size)) {}
ConnectionPool::~ConnectionPool() = default;
void* ConnectionPool::acquire() { return impl_->acquire(); }
void ConnectionPool::release(void* conn) { impl_->release(static_cast<CURL*>(conn)); }
size_t ConnectionPool::available() const { return impl_->available(); }
size_t ConnectionPool::in_use() const { return impl_->in_use(); }

// ===== LLMClient Implementation =====

class LLMClient::Impl {
public:
    Impl(const LLMConfig& config) 
        : config_(config)
        , pool_(config.connection_pool_size)
        , last_error_(LLMError::None) {
        spdlog::info("LLMClient initialized: model={}, pool_size={}", 
                     config_.model, config_.connection_pool_size);
    }
    
    ~Impl() = default;
    
    // Callback for curl write
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
    // Callback for streaming
    static size_t streamCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* callback = static_cast<std::function<bool(const std::string&)>*>(userp);
        std::string chunk((char*)contents, size * nmemb);
        if (*callback && !(*callback)(chunk)) {
            return 0;  // Cancel
        }
        return size * nmemb;
    }
    
    // Parse SSE line
    std::vector<StreamChunk> parseSSE(const std::string& data) {
        std::vector<StreamChunk> chunks;
        std::istringstream ss(data);
        std::string line;
        
        while (std::getline(ss, line)) {
            if (line.empty() || line == "\r") continue;
            if (line.substr(0, 6) != "data: ") continue;
            
            std::string json_str = line.substr(6);
            if (json_str == "[DONE]") {
                chunks.push_back(StreamChunk{.done = true});
                continue;
            }
            
            try {
                auto j = nlohmann::json::parse(json_str);
                StreamChunk chunk;
                
                if (j.contains("choices") && !j["choices"].empty()) {
                    auto& choice = j["choices"][0];
                    if (choice.contains("delta")) {
                        auto& delta = choice["delta"];
                        if (delta.contains("content")) {
                            chunk.delta = delta["content"].get<std::string>();
                        }
                        if (delta.contains("tool_calls")) {
                            chunk.tool_call_delta = delta["tool_calls"];
                        }
                    }
                    chunk.finish_reason = choice.value("finish_reason", "");
                }
                
                chunks.push_back(chunk);
            } catch (const std::exception& e) {
                spdlog::debug("SSE parse error: {}", e.what());
            }
        }
        
        return chunks;
    }
    
    // Parse API response
    ChatResponse parseResponse(const std::string& response, int http_code) {
        ChatResponse cr;
        
        if (http_code == 0) {
            cr.success = false;
            cr.error_message = "Network error: no response";
            last_error_ = LLMError::NetworkError;
            return cr;
        }
        
        if (http_code == 401) {
            cr.success = false;
            cr.error_message = "Authentication error: invalid API key";
            last_error_ = LLMError::AuthError;
            return cr;
        }
        
        if (http_code == 429) {
            cr.success = false;
            cr.error_message = "Rate limit exceeded";
            last_error_ = LLMError::RateLimitError;
            return cr;
        }
        
        try {
            auto j = nlohmann::json::parse(response);
            
            if (j.contains("error")) {
                cr.success = false;
                cr.error_message = j["error"]["message"].get<std::string>();
                last_error_ = LLMError::InvalidResponse;
                return cr;
            }
            
            if (!j.contains("choices") || j["choices"].empty()) {
                cr.success = false;
                cr.error_message = "Invalid response: no choices";
                last_error_ = LLMError::InvalidResponse;
                return cr;
            }
            
            auto& choice = j["choices"][0];
            auto& msg = choice["message"];
            
            cr.content = msg.value("content", "");
            cr.finish_reason = choice.value("finish_reason", "");
            
            if (msg.contains("tool_calls")) {
                cr.tool_calls = msg["tool_calls"];
            }
            
            if (j.contains("usage")) {
                cr.prompt_tokens = j["usage"].value("prompt_tokens", 0);
                cr.completion_tokens = j["usage"].value("completion_tokens", 0);
            }
            
            cr.success = true;
            last_error_ = LLMError::None;
            
        } catch (const std::exception& e) {
            cr.success = false;
            cr.error_message = std::string("Parse error: ") + e.what();
            last_error_ = LLMError::InvalidResponse;
        }
        
        return cr;
    }
    
    // Make HTTP request with retry
    std::pair<std::string, int> makeRequest(
        const std::string& endpoint,
        const nlohmann::json& body,
        bool stream = false,
        std::function<bool(const std::string&)> stream_callback = nullptr
    ) {
        std::string url = config_.api_base + endpoint;
        std::string requestBody = body.dump();
        std::string responseBody;
        int http_code = 0;
        
        for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
            if (attempt > 0) {
                spdlog::warn("Retrying request (attempt {}/{})", attempt, config_.max_retries);
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    config_.retry_delay_ms * attempt));
            }
            
            CURL* curl = static_cast<CURL*>(pool_.acquire());
            if (!curl) {
                continue;
            }
            
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            std::string auth = "Authorization: Bearer " + config_.api_key;
            headers = curl_slist_append(headers, auth.c_str());
            
            if (stream) {
                headers = curl_slist_append(headers, "Accept: text/event-stream");
            }
            
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestBody.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeout_seconds);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            
            if (stream && stream_callback) {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_callback);
            } else {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
            }
            
            CURLcode res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            
            curl_slist_free_all(headers);
            pool_.release(curl);
            
            if (res == CURLE_OK) {
                if (http_code == 401 || http_code == 429) {
                    // Auth and rate limit errors shouldn't be retried
                    break;
                }
                if (http_code >= 200 && http_code < 300) {
                    break;  // Success
                }
            } else {
                http_code = 0;
                last_error_ = LLMError::NetworkError;
            }
        }
        
        return {responseBody, http_code};
    }
    
    // Chat (non-streaming)
    ChatResponse chat(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools
    ) {
        nlohmann::json body;
        body["model"] = config_.model;
        body["messages"] = messages;
        body["max_tokens"] = config_.max_tokens;
        body["temperature"] = config_.temperature;
        body["stream"] = false;
        
        if (!tools.empty()) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }
        
        auto [response, http_code] = makeRequest("/chat/completions", body);
        return parseResponse(response, http_code);
    }
    
    // Chat (streaming)
    ChatResponse chatStream(
        const std::vector<nlohmann::json>& messages,
        const std::vector<nlohmann::json>& tools,
        std::function<bool(const StreamChunk&)> on_chunk
    ) {
        nlohmann::json body;
        body["model"] = config_.model;
        body["messages"] = messages;
        body["max_tokens"] = config_.max_tokens;
        body["temperature"] = config_.temperature;
        body["stream"] = true;
        
        if (!tools.empty()) {
            body["tools"] = tools;
            body["tool_choice"] = "auto";
        }
        
        ChatResponse final_response;
        final_response.success = true;
        std::string accumulated_content;
        std::vector<nlohmann::json> tool_calls;
        
        auto callback = [&](const std::string& chunk) -> bool {
            auto chunks = parseSSE(chunk);
            for (const auto& c : chunks) {
                if (c.done) {
                    final_response.finish_reason = "stop";
                    return true;
                }
                
                if (!on_chunk(c)) {
                    return false;  // Cancelled
                }
                
                accumulated_content += c.delta;
                
                if (c.tool_call_delta.has_value()) {
                    // Handle tool call deltas
                    // TODO: accumulate tool call deltas
                }
                
                if (!c.finish_reason.empty()) {
                    final_response.finish_reason = c.finish_reason;
                }
            }
            return true;
        };
        
        auto [response, http_code] = makeRequest("/chat/completions", body, true, callback);
        
        if (http_code != 200) {
            return parseResponse(response, http_code);
        }
        
        final_response.content = accumulated_content;
        final_response.tool_calls = tool_calls;
        return final_response;
    }
    
    LLMError lastError() const { return last_error_; }
    
    std::pair<size_t, size_t> poolStats() const {
        return {pool_.available(), pool_.in_use()};
    }
    
private:
    LLMConfig config_;
    ConnectionPool pool_;
    std::atomic<LLMError> last_error_{LLMError::None};
};

// ===== LLMClient public API =====

LLMClient::LLMClient(const LLMConfig& config) 
    : impl_(std::make_unique<Impl>(config)) {}
LLMClient::~LLMClient() = default;

ChatResponse LLMClient::chat(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools) {
    return impl_->chat(messages, tools);
}

ChatResponse LLMClient::chat_stream(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    std::function<bool(const StreamChunk&)> on_chunk) {
    return impl_->chatStream(messages, tools, on_chunk);
}

asio::awaitable<ChatResponse> LLMClient::chat_async(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools) {
    // Run on thread pool
    auto executor = co_await asio::this_coro::executor;
    auto impl_ptr = impl_.get();
    auto msgs = messages;
    auto tls = tools;
    
    auto result = co_await asio::co_spawn(
        executor,
        [impl_ptr, msgs = std::move(msgs), tls = std::move(tls)]() mutable 
            -> asio::awaitable<ChatResponse> {
            co_return impl_ptr->chat(msgs, tls);
        },
        asio::use_awaitable
    );
    
    co_return result;
}

asio::awaitable<ChatResponse> LLMClient::chat_stream_async(
    const std::vector<nlohmann::json>& messages,
    const std::vector<nlohmann::json>& tools,
    std::function<bool(const StreamChunk&)> on_chunk) {
    auto executor = co_await asio::this_coro::executor;
    auto impl_ptr = impl_.get();
    auto msgs = messages;
    auto tls = tools;
    
    auto result = co_await asio::co_spawn(
        executor,
        [impl_ptr, msgs = std::move(msgs), tls = std::move(tls), on_chunk]() mutable
            -> asio::awaitable<ChatResponse> {
            co_return impl_ptr->chatStream(msgs, tls, on_chunk);
        },
        asio::use_awaitable
    );
    
    co_return result;
}

LLMError LLMClient::last_error() const {
    return impl_->lastError();
}

std::pair<size_t, size_t> LLMClient::pool_stats() const {
    return impl_->poolStats();
}

std::vector<float> LLMClient::embed(const std::string& text) {
    // TODO: Implement embeddings
    (void)text;
    return {};
}

} // namespace agent
