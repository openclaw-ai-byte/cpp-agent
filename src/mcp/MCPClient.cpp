#include "mcp/MCPClient.hpp"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <memory>
#include <array>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <sstream>
#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#endif

namespace agent::mcp {

// Helper: Generate unique request ID
static std::atomic<int> g_request_id{1};
static int next_request_id() { return g_request_id++; }

// Helper: Create JSON-RPC request
static std::string make_request(int id, const std::string& method, const nlohmann::json& params = {}) {
    nlohmann::json req = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method}
    };
    if (!params.is_null() && !params.empty()) {
        req["params"] = params;
    }
    return req.dump() + "\n";
}

// Helper: Parse JSON-RPC response
static bool parse_response(const std::string& data, int expected_id, nlohmann::json& result, std::string& error) {
    try {
        auto j = nlohmann::json::parse(data);
        if (j.value("jsonrpc", "") != "2.0") {
            error = "Invalid JSON-RPC version";
            return false;
        }
        if (j.value("id", 0) != expected_id) {
            error = "Response ID mismatch";
            return false;
        }
        if (j.contains("error")) {
            error = j["error"].value("message", "Unknown error");
            return false;
        }
        if (j.contains("result")) {
            result = j["result"];
            return true;
        }
        error = "No result in response";
        return false;
    } catch (const std::exception& e) {
        error = std::string("JSON parse error: ") + e.what();
        return false;
    }
}

// ============================================================================
// Stdio MCP Client - Communicates via subprocess stdin/stdout
// ============================================================================
class StdioMCPClient : public MCPClient {
public:
    StdioMCPClient() = default;
    
    ~StdioMCPClient() override {
        disconnect();
    }
    
    bool connect(const std::string& endpoint) override {
        // Endpoint format: "command args..." e.g., "mcp-server --port 8080"
        endpoint_ = endpoint;
        
        spdlog::info("MCP: Starting server: {}", endpoint);
        
#ifdef _WIN32
        // Windows implementation
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;
        
        // Create pipes for stdin/stdout
        HANDLE hStdinRead, hStdinWrite, hStdoutRead, hStdoutWrite;
        CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0);
        CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0);
        
        // Set handle inheritance
        SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
        
        // Create process
        STARTUPINFOA si = {sizeof(STARTUPINFOA)};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hStdinRead;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        
        PROCESS_INFORMATION pi;
        if (!CreateProcessA(
            nullptr,
            const_cast<char*>(endpoint.c_str()),
            nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW,
            nullptr, nullptr,
            &si, &pi
        )) {
            spdlog::error("MCP: Failed to create process: {}", GetLastError());
            CloseHandle(hStdinRead);
            CloseHandle(hStdinWrite);
            CloseHandle(hStdoutRead);
            CloseHandle(hStdoutWrite);
            return false;
        }
        
        process_handle_ = pi.hProcess;
        thread_handle_ = pi.hThread;
        stdin_write_ = hStdinWrite;
        stdout_read_ = hStdoutRead;
        
        CloseHandle(hStdinRead);
        CloseHandle(hStdoutWrite);
#else
        // Unix implementation
        int stdin_pipe[2], stdout_pipe[2];
        if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
            spdlog::error("MCP: Failed to create pipes: {}", strerror(errno));
            return false;
        }
        
        pid_t pid = fork();
        if (pid < 0) {
            spdlog::error("MCP: Fork failed: {}", strerror(errno));
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            return false;
        }
        
        if (pid == 0) {
            // Child process
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);
            close(stdin_pipe[0]);
            close(stdout_pipe[1]);
            
            // Parse command and exec
            execl("/bin/sh", "sh", "-c", endpoint.c_str(), nullptr);
            _exit(127);
        }
        
        // Parent process
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        stdin_write_ = fdopen(stdin_pipe[1], "w");
        stdout_read_ = fdopen(stdout_pipe[0], "r");
        child_pid_ = pid;
#endif
        
        connected_ = true;
        
        // Initialize MCP connection
        if (!initialize()) {
            spdlog::error("MCP: Failed to initialize connection");
            disconnect();
            return false;
        }
        
        spdlog::info("MCP: Connected successfully");
        return true;
    }
    
    void disconnect() override {
        if (!connected_) return;
        
        connected_ = false;
        
#ifdef _WIN32
        if (stdin_write_) {
            CloseHandle((HANDLE)stdin_write_);
            stdin_write_ = nullptr;
        }
        if (stdout_read_) {
            CloseHandle((HANDLE)stdout_read_);
            stdout_read_ = nullptr;
        }
        if (process_handle_) {
            TerminateProcess((HANDLE)process_handle_, 0);
            CloseHandle((HANDLE)process_handle_);
            process_handle_ = nullptr;
        }
        if (thread_handle_) {
            CloseHandle((HANDLE)thread_handle_);
            thread_handle_ = nullptr;
        }
#else
        if (stdin_write_) {
            fclose((FILE*)stdin_write_);
            stdin_write_ = nullptr;
        }
        if (stdout_read_) {
            fclose((FILE*)stdout_read_);
            stdout_read_ = nullptr;
        }
        if (child_pid_ > 0) {
            kill(child_pid_, SIGTERM);
            int status;
            waitpid(child_pid_, &status, 0);
            child_pid_ = 0;
        }
#endif
        
        spdlog::info("MCP: Disconnected");
    }
    
    bool is_connected() const override {
        return connected_;
    }
    
    MCPServerInfo get_server_info() const override {
        return server_info_;
    }
    
    // ========== Tools API ==========
    
    std::vector<MCPTool> list_tools() override {
        std::vector<MCPTool> tools;
        std::string error;
        
        int id = next_request_id();
        std::string req = make_request(id, "tools/list");
        nlohmann::json result;
        
        if (!send_request(req, id, result, error)) {
            spdlog::error("MCP: tools/list failed: {}", error);
            return tools;
        }
        
        if (result.contains("tools") && result["tools"].is_array()) {
            for (const auto& t : result["tools"]) {
                MCPTool tool;
                tool.name = t.value("name", "");
                tool.description = t.value("description", "");
                tool.input_schema = t.value("inputSchema", nlohmann::json::object());
                tools.push_back(tool);
            }
        }
        
        spdlog::debug("MCP: Found {} tools", tools.size());
        return tools;
    }
    
    MCPToolResult call_tool(
        const std::string& name,
        const nlohmann::json& arguments,
        ProgressCallback progress = nullptr
    ) override {
        MCPToolResult result;
        std::string error;
        
        nlohmann::json params = {
            {"name", name},
            {"arguments", arguments}
        };
        
        int id = next_request_id();
        std::string req = make_request(id, "tools/call", params);
        nlohmann::json resp;
        
        if (!send_request(req, id, resp, error)) {
            result.is_error = true;
            result.error_message = error;
            return result;
        }
        
        result.is_error = resp.value("isError", false);
        
        if (resp.contains("content") && resp["content"].is_array()) {
            for (const auto& c : resp["content"]) {
                MCPContent content;
                content.type = c.value("type", "text");
                content.text = c.value("text", "");
                content.data = c.value("data", "");
                content.mime_type = c.value("mimeType", "");
                content.uri = c.value("uri", "");
                result.content.push_back(content);
            }
        }
        
        return result;
    }
    
    // ========== Prompts API ==========
    
    std::vector<MCPPrompt> list_prompts() override {
        std::vector<MCPPrompt> prompts;
        std::string error;
        
        int id = next_request_id();
        std::string req = make_request(id, "prompts/list");
        nlohmann::json result;
        
        if (!send_request(req, id, result, error)) {
            spdlog::error("MCP: prompts/list failed: {}", error);
            return prompts;
        }
        
        if (result.contains("prompts") && result["prompts"].is_array()) {
            for (const auto& p : result["prompts"]) {
                MCPPrompt prompt;
                prompt.name = p.value("name", "");
                prompt.description = p.value("description", "");
                prompt.arguments = p.value("arguments", nlohmann::json::array());
                prompts.push_back(prompt);
            }
        }
        
        return prompts;
    }
    
    std::string get_prompt(
        const std::string& name,
        const nlohmann::json& arguments = {}
    ) override {
        std::string error;
        
        nlohmann::json params = {{"name", name}};
        if (!arguments.empty()) {
            params["arguments"] = arguments;
        }
        
        int id = next_request_id();
        std::string req = make_request(id, "prompts/get", params);
        nlohmann::json result;
        
        if (!send_request(req, id, result, error)) {
            spdlog::error("MCP: prompts/get failed: {}", error);
            return "";
        }
        
        // Prompt result contains "description" and "messages"
        // Return the concatenated text from messages
        std::string text;
        if (result.contains("messages") && result["messages"].is_array()) {
            for (const auto& msg : result["messages"]) {
                if (msg.contains("content")) {
                    auto content = msg["content"];
                    if (content.is_string()) {
                        text += content.get<std::string>() + "\n";
                    } else if (content.is_object() && content.contains("text")) {
                        text += content["text"].get<std::string>() + "\n";
                    }
                }
            }
        }
        
        return text;
    }
    
    // ========== Resources API ==========
    
    std::vector<MCPResource> list_resources() override {
        std::vector<MCPResource> resources;
        std::string error;
        
        int id = next_request_id();
        std::string req = make_request(id, "resources/list");
        nlohmann::json result;
        
        if (!send_request(req, id, result, error)) {
            spdlog::error("MCP: resources/list failed: {}", error);
            return resources;
        }
        
        if (result.contains("resources") && result["resources"].is_array()) {
            for (const auto& r : result["resources"]) {
                MCPResource res;
                res.uri = r.value("uri", "");
                res.name = r.value("name", "");
                res.description = r.value("description", "");
                res.mime_type = r.value("mimeType", "");
                resources.push_back(res);
            }
        }
        
        return resources;
    }
    
    std::string read_resource(const std::string& uri) override {
        std::string error;
        
        nlohmann::json params = {{"uri", uri}};
        int id = next_request_id();
        std::string req = make_request(id, "resources/read", params);
        nlohmann::json result;
        
        if (!send_request(req, id, result, error)) {
            spdlog::error("MCP: resources/read failed: {}", error);
            return "";
        }
        
        // Resource result contains "contents" array
        std::string text;
        if (result.contains("contents") && result["contents"].is_array()) {
            for (const auto& c : result["contents"]) {
                text += c.value("text", "");
            }
        }
        
        return text;
    }
    
    void set_logging_level(const std::string& level) override {
        nlohmann::json params = {{"level", level}};
        int id = next_request_id();
        std::string req = make_request(id, "logging/setLevel", params);
        
        nlohmann::json result;
        std::string error;
        send_request(req, id, result, error);
    }
    
private:
    std::string endpoint_;
    std::atomic<bool> connected_{false};
    MCPServerInfo server_info_;
    std::mutex mutex_;
    
#ifdef _WIN32
    void* process_handle_ = nullptr;
    void* thread_handle_ = nullptr;
    void* stdin_write_ = nullptr;
    void* stdout_read_ = nullptr;
#else
    pid_t child_pid_ = 0;
    FILE* stdin_write_ = nullptr;
    FILE* stdout_read_ = nullptr;
#endif
    
    bool initialize() {
        // Send initialize request
        nlohmann::json init_params = {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", {
                {"tools", true},
                {"prompts", true},
                {"resources", true}
            }},
            {"clientInfo", {
                {"name", "cpp-agent"},
                {"version", "0.1.0"}
            }}
        };
        
        int id = next_request_id();
        std::string req = make_request(id, "initialize", init_params);
        nlohmann::json result;
        std::string error;
        
        if (!send_request(req, id, result, error)) {
            spdlog::error("MCP: Initialize failed: {}", error);
            return false;
        }
        
        // Parse server info
        server_info_.name = result.value("serverInfo", nlohmann::json::object()).value("name", "unknown");
        server_info_.version = result.value("serverInfo", nlohmann::json::object()).value("version", "0.0.0");
        server_info_.capabilities = result.value("capabilities", nlohmann::json::object());
        
        spdlog::info("MCP: Connected to {} v{}", server_info_.name, server_info_.version);
        
        // Send initialized notification
        std::string notif = R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n";
        send_raw(notif);
        
        return true;
    }
    
    bool send_request(const std::string& request, int expected_id, nlohmann::json& result, std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!connected_) {
            error = "Not connected";
            return false;
        }
        
        // Send request
        if (!send_raw(request)) {
            error = "Failed to send request";
            return false;
        }
        
        // Read response
        std::string response;
        if (!recv_line(response)) {
            error = "Failed to receive response";
            return false;
        }
        
        return parse_response(response, expected_id, result, error);
    }
    
    bool send_raw(const std::string& data) {
#ifdef _WIN32
        DWORD written;
        return WriteFile((HANDLE)stdin_write_, data.c_str(), (DWORD)data.size(), &written, nullptr);
#else
        if (!stdin_write_) return false;
        fputs(data.c_str(), (FILE*)stdin_write_);
        fflush((FILE*)stdin_write_);
        return true;
#endif
    }
    
    bool recv_line(std::string& line) {
#ifdef _WIN32
        char buf[4096];
        DWORD read;
        line.clear();
        
        while (true) {
            char c;
            if (!ReadFile((HANDLE)stdout_read_, &c, 1, &read, nullptr) || read == 0) {
                return false;
            }
            if (c == '\n') break;
            line += c;
            if (line.size() > 1000000) return false;  // Safety limit
        }
        return true;
#else
        if (!stdout_read_) return false;
        line.clear();
        char buf[4096];
        if (!fgets(buf, sizeof(buf), (FILE*)stdout_read_)) {
            return false;
        }
        line = buf;
        // Remove trailing newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        return true;
#endif
    }
};

// ============================================================================
// HTTP MCP Client - Communicates via HTTP/SSE
// ============================================================================
class HTTPMCPClient : public MCPClient {
public:
    HTTPMCPClient() {
        // Initialize curl once
        static bool curl_initialized = false;
        if (!curl_initialized) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_initialized = true;
        }
    }
    
    ~HTTPMCPClient() override { disconnect(); }
    
    bool connect(const std::string& endpoint) override {
        endpoint_ = endpoint;
        
        // Remove trailing slash
        while (!endpoint_.empty() && endpoint_.back() == '/') {
            endpoint_.pop_back();
        }
        
        // Initialize MCP connection
        nlohmann::json init_params = {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", {
                {"tools", true},
                {"prompts", true},
                {"resources", true}
            }},
            {"clientInfo", {
                {"name", "cpp-agent"},
                {"version", "0.1.0"}
            }}
        };
        
        nlohmann::json result;
        std::string error;
        if (!send_jsonrpc("initialize", init_params, result, error)) {
            spdlog::error("MCP HTTP: Initialize failed: {}", error);
            return false;
        }
        
        // Parse server info
        server_info_.name = result.value("serverInfo", nlohmann::json::object()).value("name", "unknown");
        server_info_.version = result.value("serverInfo", nlohmann::json::object()).value("version", "0.0.0");
        server_info_.capabilities = result.value("capabilities", nlohmann::json::object());
        
        // Send initialized notification
        send_notification("notifications/initialized");
        
        connected_ = true;
        spdlog::info("MCP HTTP: Connected to {} v{}", server_info_.name, server_info_.version);
        return true;
    }
    
    void disconnect() override {
        connected_ = false;
    }
    
    bool is_connected() const override { return connected_; }
    MCPServerInfo get_server_info() const override { return server_info_; }
    
    // ========== Tools API ==========
    
    std::vector<MCPTool> list_tools() override {
        std::vector<MCPTool> tools;
        if (!connected_) return tools;
        
        nlohmann::json result;
        std::string error;
        if (!send_jsonrpc("tools/list", {}, result, error)) {
            spdlog::error("MCP HTTP: tools/list failed: {}", error);
            return tools;
        }
        
        if (result.contains("tools") && result["tools"].is_array()) {
            for (const auto& t : result["tools"]) {
                MCPTool tool;
                tool.name = t.value("name", "");
                tool.description = t.value("description", "");
                tool.input_schema = t.value("inputSchema", nlohmann::json::object());
                tools.push_back(tool);
            }
        }
        
        return tools;
    }
    
    MCPToolResult call_tool(
        const std::string& name,
        const nlohmann::json& arguments,
        ProgressCallback progress = nullptr
    ) override {
        MCPToolResult result;
        if (!connected_) {
            result.is_error = true;
            result.error_message = "Not connected";
            return result;
        }
        
        nlohmann::json params = {
            {"name", name},
            {"arguments", arguments}
        };
        
        nlohmann::json resp;
        std::string error;
        if (!send_jsonrpc("tools/call", params, resp, error)) {
            result.is_error = true;
            result.error_message = error;
            return result;
        }
        
        result.is_error = resp.value("isError", false);
        if (resp.contains("content") && resp["content"].is_array()) {
            for (const auto& c : resp["content"]) {
                MCPContent content;
                content.type = c.value("type", "text");
                content.text = c.value("text", "");
                content.data = c.value("data", "");
                content.mime_type = c.value("mimeType", "");
                content.uri = c.value("uri", "");
                result.content.push_back(content);
            }
        }
        
        return result;
    }
    
    // ========== Prompts API ==========
    
    std::vector<MCPPrompt> list_prompts() override {
        std::vector<MCPPrompt> prompts;
        if (!connected_) return prompts;
        
        nlohmann::json result;
        std::string error;
        if (!send_jsonrpc("prompts/list", {}, result, error)) {
            return prompts;
        }
        
        if (result.contains("prompts") && result["prompts"].is_array()) {
            for (const auto& p : result["prompts"]) {
                MCPPrompt prompt;
                prompt.name = p.value("name", "");
                prompt.description = p.value("description", "");
                prompt.arguments = p.value("arguments", nlohmann::json::array());
                prompts.push_back(prompt);
            }
        }
        
        return prompts;
    }
    
    std::string get_prompt(
        const std::string& name,
        const nlohmann::json& arguments = {}
    ) override {
        if (!connected_) return "";
        
        nlohmann::json params = {{"name", name}};
        if (!arguments.empty()) {
            params["arguments"] = arguments;
        }
        
        nlohmann::json result;
        std::string error;
        if (!send_jsonrpc("prompts/get", params, result, error)) {
            return "";
        }
        
        std::string text;
        if (result.contains("messages") && result["messages"].is_array()) {
            for (const auto& msg : result["messages"]) {
                if (msg.contains("content")) {
                    auto content = msg["content"];
                    if (content.is_string()) {
                        text += content.get<std::string>() + "\n";
                    } else if (content.is_object() && content.contains("text")) {
                        text += content["text"].get<std::string>() + "\n";
                    }
                }
            }
        }
        
        return text;
    }
    
    // ========== Resources API ==========
    
    std::vector<MCPResource> list_resources() override {
        std::vector<MCPResource> resources;
        if (!connected_) return resources;
        
        nlohmann::json result;
        std::string error;
        if (!send_jsonrpc("resources/list", {}, result, error)) {
            return resources;
        }
        
        if (result.contains("resources") && result["resources"].is_array()) {
            for (const auto& r : result["resources"]) {
                MCPResource res;
                res.uri = r.value("uri", "");
                res.name = r.value("name", "");
                res.description = r.value("description", "");
                res.mime_type = r.value("mimeType", "");
                resources.push_back(res);
            }
        }
        
        return resources;
    }
    
    std::string read_resource(const std::string& uri) override {
        if (!connected_) return "";
        
        nlohmann::json params = {{"uri", uri}};
        nlohmann::json result;
        std::string error;
        if (!send_jsonrpc("resources/read", params, result, error)) {
            return "";
        }
        
        std::string text;
        if (result.contains("contents") && result["contents"].is_array()) {
            for (const auto& c : result["contents"]) {
                text += c.value("text", "");
            }
        }
        
        return text;
    }
    
    void set_logging_level(const std::string& level) override {
        if (!connected_) return;
        nlohmann::json params = {{"level", level}};
        send_jsonrpc("logging/setLevel", params);
    }
    
private:
    std::string endpoint_;
    std::atomic<bool> connected_{false};
    MCPServerInfo server_info_;
    std::mutex mutex_;
    int request_id_{1};
    
    int next_id() { return request_id_++; }
    
    bool send_jsonrpc(const std::string& method, const nlohmann::json& params, nlohmann::json& result, std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        nlohmann::json req = {
            {"jsonrpc", "2.0"},
            {"id", next_id()},
            {"method", method}
        };
        if (!params.is_null() && !params.empty()) {
            req["params"] = params;
        }
        
        std::string response;
        if (!http_post(endpoint_ + "/mcp", req.dump(), response, error)) {
            return false;
        }
        
        return parse_jsonrpc_response(response, req["id"].get<int>(), result, error);
    }
    
    // Overload for notifications (no response expected)
    void send_jsonrpc(const std::string& method, const nlohmann::json& params) {
        nlohmann::json req = {
            {"jsonrpc", "2.0"},
            {"method", method}
        };
        if (!params.is_null() && !params.empty()) {
            req["params"] = params;
        }
        
        std::string response, error;
        http_post(endpoint_ + "/mcp", req.dump(), response, error);
    }
    
    void send_notification(const std::string& method) {
        nlohmann::json req = {
            {"jsonrpc", "2.0"},
            {"method", method}
        };
        std::string response, error;
        http_post(endpoint_ + "/mcp", req.dump(), response, error);
    }
    
    bool http_post(const std::string& url, const std::string& body, std::string& response, std::string& error) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "Failed to initialize curl";
            return false;
        }
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, std::string* s) {
            s->append((char*)contents, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            error = curl_easy_strerror(res);
            return false;
        }
        
        return true;
    }
    
    bool parse_jsonrpc_response(const std::string& data, int expected_id, nlohmann::json& result, std::string& error) {
        try {
            auto j = nlohmann::json::parse(data);
            if (j.value("jsonrpc", "") != "2.0") {
                error = "Invalid JSON-RPC version";
                return false;
            }
            if (j.value("id", 0) != expected_id) {
                error = "Response ID mismatch";
                return false;
            }
            if (j.contains("error")) {
                error = j["error"].value("message", "Unknown error");
                return false;
            }
            if (j.contains("result")) {
                result = j["result"];
                return true;
            }
            error = "No result in response";
            return false;
        } catch (const std::exception& e) {
            error = std::string("JSON parse error: ") + e.what();
            return false;
        }
    }
};

// ============================================================================
// Factory
// ============================================================================
std::unique_ptr<MCPClient> MCPClientFactory::create(const std::string& transport) {
    if (transport == "stdio") {
        return std::make_unique<StdioMCPClient>();
    } else if (transport == "http" || transport == "sse") {
        return std::make_unique<HTTPMCPClient>();
    }
    spdlog::error("MCP: Unknown transport type: {}", transport);
    return nullptr;
}

} // namespace agent::mcp
