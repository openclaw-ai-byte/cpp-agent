#include "tools/Tool.hpp"
#include "tools/ToolRegistry.hpp"
#include "cron/CronManager.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <array>
#include <curl/curl.h>

namespace fs = boost::filesystem;

namespace agent {

// Web Search Tool - Uses DuckDuckGo Instant Answer API
class WebSearchTool : public Tool {
public:
    std::string name() const override { return "web_search"; }
    
    std::string description() const override {
        return "Search the web for information using DuckDuckGo. Returns relevant search results.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["query"]["type"] = "string";
        params["properties"]["query"]["description"] = "The search query";
        params["properties"]["num_results"]["type"] = "integer";
        params["properties"]["num_results"]["description"] = "Number of results to return (max 10)";
        params["properties"]["num_results"]["default"] = 5;
        params["required"] = {"query"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string query = args["query"];
        int num_results = args.value("num_results", 5);
        num_results = std::min(num_results, 10);  // Cap at 10
        
        // DuckDuckGo HTML search fallback
        std::string url = "https://html.duckduckgo.com/html/?q=" + urlEncode(query);
        
        std::string response = httpGet(url);
        if (response.empty()) {
            return ToolResult::error("Failed to perform web search");
        }
        
        // Parse HTML results
        std::vector<nlohmann::json> results;
        size_t pos = 0;
        int count = 0;
        
        // DuckDuckGo HTML uses class="result__a" for titles and class="result__url" for URLs
        while ((pos = response.find("class=\"result__a\"", pos)) != std::string::npos && count < num_results) {
            // Extract title
            size_t title_start = response.find('>', pos);
            size_t title_end = response.find("</a>", title_start);
            if (title_start == std::string::npos || title_end == std::string::npos) {
                pos++;
                continue;
            }
            
            std::string title = response.substr(title_start + 1, title_end - title_start - 1);
            title = stripHtml(title);
            
            // Extract URL (from href)
            size_t href_start = response.rfind("href=\"", pos);
            size_t href_end = response.find('"', href_start + 6);
            if (href_start != std::string::npos && href_end != std::string::npos) {
                std::string result_url = response.substr(href_start + 6, href_end - href_start - 6);
                
                // DuckDuckGo uses redirect URLs, extract actual URL
                if (result_url.find("uddg=") != std::string::npos) {
                    size_t uddg_pos = result_url.find("uddg=");
                    result_url = result_url.substr(uddg_pos + 5);
                    result_url = urlDecode(result_url);
                }
                
                // Extract snippet (text after the link)
                size_t snippet_start = title_end + 4;
                size_t snippet_end = response.find("<a class=\"result__snippet\"", snippet_start);
                if (snippet_end == std::string::npos) {
                    snippet_end = response.find("</div>", snippet_start);
                }
                std::string snippet;
                if (snippet_start < snippet_end && snippet_end != std::string::npos) {
                    snippet = response.substr(snippet_start, snippet_end - snippet_start);
                    snippet = stripHtml(snippet);
                    // Clean up snippet
                    size_t first_char = snippet.find_first_not_of(" \t\n\r");
                    if (first_char != std::string::npos) {
                        snippet = snippet.substr(first_char);
                    }
                    if (snippet.length() > 200) {
                        snippet = snippet.substr(0, 197) + "...";
                    }
                }
                
                if (!title.empty() && !result_url.empty()) {
                    results.push_back({
                        {"title", title},
                        {"url", result_url},
                        {"snippet", snippet}
                    });
                    count++;
                }
            }
            
            pos = title_end;
        }
        
        return ToolResult::ok(nlohmann::json{
            {"query", query},
            {"results", results},
            {"count", results.size()}
        });
    }
    
private:
    static std::string urlEncode(const std::string& s) {
        std::string result;
        for (char c : s) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                result += c;
            } else {
                result += '%';
                char hex[3];
                snprintf(hex, sizeof(hex), "%02X", (unsigned char)c);
                result += hex;
            }
        }
        return result;
    }
    
    static std::string urlDecode(const std::string& s) {
        std::string result;
        for (size_t i = 0; i < s.length(); ++i) {
            if (s[i] == '%' && i + 2 < s.length()) {
                int val;
                sscanf(s.substr(i + 1, 2).c_str(), "%x", &val);
                result += (char)val;
                i += 2;
            } else if (s[i] == '+') {
                result += ' ';
            } else {
                result += s[i];
            }
        }
        return result;
    }
    
    static std::string stripHtml(const std::string& html) {
        std::string result;
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') {
                in_tag = true;
            } else if (c == '>') {
                in_tag = false;
            } else if (!in_tag) {
                result += c;
            }
        }
        // Decode common entities
        size_t pos;
        while ((pos = result.find("&amp;")) != std::string::npos) {
            result.replace(pos, 5, "&");
        }
        while ((pos = result.find("&lt;")) != std::string::npos) {
            result.replace(pos, 4, "<");
        }
        while ((pos = result.find("&gt;")) != std::string::npos) {
            result.replace(pos, 4, ">");
        }
        while ((pos = result.find("&quot;")) != std::string::npos) {
            result.replace(pos, 6, "\"");
        }
        while ((pos = result.find("&#39;")) != std::string::npos) {
            result.replace(pos, 5, "'");
        }
        return result;
    }
    
    static std::string httpGet(const std::string& url) {
        std::string response;
        CURL* curl = curl_easy_init();
        if (!curl) return "";
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void* contents, size_t size, size_t nmemb, void* userp) {
            ((std::string*)userp)->append((char*)contents, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; CppAgent/1.0)");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        return res == CURLE_OK ? response : "";
    }
};

// File Read Tool
class FileReadTool : public Tool {
public:
    std::string name() const override { return "file_read"; }
    
    std::string description() const override {
        return "Read the contents of a file from the filesystem.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["path"]["type"] = "string";
        params["properties"]["path"]["description"] = "The file path to read";
        params["properties"]["offset"]["type"] = "integer";
        params["properties"]["offset"]["description"] = "Line number to start reading from (1-indexed)";
        params["properties"]["offset"]["default"] = 1;
        params["properties"]["limit"]["type"] = "integer";
        params["properties"]["limit"]["description"] = "Maximum number of lines to read";
        params["properties"]["limit"]["default"] = 100;
        params["required"] = {"path"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string path = args["path"];
        int offset = args.value("offset", 1);
        int limit = args.value("limit", 100);
        
        if (!fs::exists(path)) {
            return ToolResult::error("File not found: " + path);
        }
        
        try {
            std::ifstream file(path);
            if (!file.is_open()) {
                return ToolResult::error("Cannot open file: " + path);
            }
            
            std::string line;
            std::vector<std::string> lines;
            int current_line = 0;
            
            while (std::getline(file, line)) {
                current_line++;
                if (current_line >= offset && current_line < offset + limit) {
                    lines.push_back(line);
                }
                if (current_line >= offset + limit) break;
            }
            
            return ToolResult::ok(nlohmann::json{
                {"path", path},
                {"lines", lines},
                {"total_lines_shown", lines.size()},
                {"start_line", offset}
            });
        } catch (const std::exception& e) {
            return ToolResult::error(std::string("Error reading file: ") + e.what());
        }
    }
    
    std::string permission_level() const override { return "read"; }
};

// File Write Tool
class FileWriteTool : public Tool {
public:
    std::string name() const override { return "file_write"; }
    
    std::string description() const override {
        return "Write content to a file. Creates the file if it doesn't exist, overwrites if it does.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["path"]["type"] = "string";
        params["properties"]["path"]["description"] = "The file path to write";
        params["properties"]["content"]["type"] = "string";
        params["properties"]["content"]["description"] = "The content to write to the file";
        params["required"] = {"path", "content"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string path = args["path"];
        std::string content = args["content"];
        
        try {
            // Create parent directories if needed
            fs::path p(path);
            if (p.has_parent_path() && !p.parent_path().empty()) {
                fs::create_directories(p.parent_path());
            }
            
            std::ofstream file(path);
            if (!file.is_open()) {
                return ToolResult::error("Cannot create file: " + path);
            }
            
            file << content;
            return ToolResult::ok("Successfully wrote " + std::to_string(content.size()) + " bytes to " + path);
        } catch (const std::exception& e) {
            return ToolResult::error(std::string("Error writing file: ") + e.what());
        }
    }
    
    std::string permission_level() const override { return "write"; }
    bool requires_confirmation() const override { return true; }
};

// List Directory Tool
class ListDirectoryTool : public Tool {
public:
    std::string name() const override { return "list_directory"; }
    
    std::string description() const override {
        return "List files and directories in a path.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["path"]["type"] = "string";
        params["properties"]["path"]["description"] = "The directory path to list";
        params["properties"]["path"]["default"] = ".";
        params["properties"]["recursive"]["type"] = "boolean";
        params["properties"]["recursive"]["description"] = "List recursively";
        params["properties"]["recursive"]["default"] = false;
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string path = args.value("path", ".");
        bool recursive = args.value("recursive", false);
        
        if (!fs::exists(path)) {
            return ToolResult::error("Directory not found: " + path);
        }
        
        if (!fs::is_directory(path)) {
            return ToolResult::error("Not a directory: " + path);
        }
        
        try {
            std::vector<nlohmann::json> entries;
            
            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(path)) {
                    entries.push_back({
                        {"path", entry.path().string()},
                        {"type", fs::is_directory(entry) ? "directory" : "file"},
                        {"size", fs::is_regular_file(entry) ? fs::file_size(entry) : 0}
                    });
                }
            } else {
                for (const auto& entry : fs::directory_iterator(path)) {
                    entries.push_back({
                        {"path", entry.path().string()},
                        {"type", fs::is_directory(entry) ? "directory" : "file"},
                        {"size", fs::is_regular_file(entry) ? fs::file_size(entry) : 0}
                    });
                }
            }
            
            return ToolResult::ok(nlohmann::json{
                {"path", path},
                {"entries", entries},
                {"count", entries.size()}
            });
        } catch (const std::exception& e) {
            return ToolResult::error(std::string("Error listing directory: ") + e.what());
        }
    }
    
    std::string permission_level() const override { return "read"; }
};

// Execute Command Tool
class ExecuteTool : public Tool {
public:
    std::string name() const override { return "execute"; }
    
    std::string description() const override {
        return "Execute a shell command. Use with caution.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["command"]["type"] = "string";
        params["properties"]["command"]["description"] = "The command to execute";
        params["properties"]["timeout"]["type"] = "integer";
        params["properties"]["timeout"]["description"] = "Timeout in seconds";
        params["properties"]["timeout"]["default"] = 30;
        params["required"] = {"command"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string command = args["command"];
        int timeout = args.value("timeout", 30);
        
        // Execute with popen
        std::array<char, 128> buffer;
        std::string result;
        
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            return ToolResult::error("Failed to execute command");
        }
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        
        int exit_code = pclose(pipe);
        
        return ToolResult::ok(nlohmann::json{
            {"stdout", result},
            {"exit_code", exit_code},
            {"command", command}
        });
    }
    
    std::string permission_level() const override { return "execute"; }
    bool requires_confirmation() const override { return true; }
};

// Time Tool
class TimeTool : public Tool {
public:
    std::string name() const override { return "get_time"; }
    
    std::string description() const override {
        return "Get the current date and time.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"] = nlohmann::json::object();
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        
        return ToolResult::ok(nlohmann::json{
            {"datetime", ss.str()},
            {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()
            ).count()}
        });
    }
};

// Cron Tools - need CronManager pointer
static CronManager* g_cron_manager = nullptr;

// Set cron manager for tool access
void set_cron_manager(CronManager* mgr) { 
    g_cron_manager = mgr; 
}

// List Cron Tasks Tool
class CronListTool : public Tool {
public:
    std::string name() const override { return "cron_list"; }
    
    std::string description() const override {
        return "List all scheduled cron tasks.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"] = nlohmann::json::object();
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        if (!g_cron_manager) {
            return ToolResult::error("Cron manager not available");
        }
        
        auto tasks = g_cron_manager->list_tasks();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : tasks) {
            arr.push_back(t.to_json());
        }
        
        return ToolResult::ok(nlohmann::json{
            {"tasks", arr},
            {"count", tasks.size()}
        });
    }
};

// Add Cron Task Tool
class CronAddTool : public Tool {
public:
    std::string name() const override { return "cron_add"; }
    
    std::string description() const override {
        return "Add a new cron task. Cron format: minute hour day month weekday. "
               "Example: '0 8 * * *' means every day at 8:00 AM.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["name"]["type"] = "string";
        params["properties"]["name"]["description"] = "Task name";
        params["properties"]["cron"]["type"] = "string";
        params["properties"]["cron"]["description"] = "Cron expression (minute hour day month weekday)";
        params["properties"]["command"]["type"] = "string";
        params["properties"]["command"]["description"] = "Command or message to execute";
        params["required"] = {"name", "cron", "command"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        if (!g_cron_manager) {
            return ToolResult::error("Cron manager not available");
        }
        
        std::string name = args["name"];
        std::string cron = args["cron"];
        std::string command = args["command"];
        
        try {
            std::string id = g_cron_manager->add_task(name, cron, command);
            return ToolResult::ok(nlohmann::json{
                {"success", true},
                {"id", id},
                {"message", "Cron task added: " + name + " (" + cron + ")"}
            });
        } catch (const std::exception& e) {
            return ToolResult::error(std::string("Failed to add cron task: ") + e.what());
        }
    }
    
    std::string permission_level() const override { return "write"; }
    bool requires_confirmation() const override { return true; }
};

// Remove Cron Task Tool
class CronRemoveTool : public Tool {
public:
    std::string name() const override { return "cron_remove"; }
    
    std::string description() const override {
        return "Remove a cron task by its ID.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["id"]["type"] = "string";
        params["properties"]["id"]["description"] = "The task ID to remove";
        params["required"] = {"id"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        if (!g_cron_manager) {
            return ToolResult::error("Cron manager not available");
        }
        
        std::string id = args["id"];
        bool removed = g_cron_manager->remove_task(id);
        
        if (removed) {
            return ToolResult::ok(nlohmann::json{
                {"success", true},
                {"message", "Cron task removed: " + id}
            });
        } else {
            return ToolResult::error("Task not found: " + id);
        }
    }
    
    std::string permission_level() const override { return "write"; }
    bool requires_confirmation() const override { return true; }
};

// Toggle Cron Task Tool
class CronToggleTool : public Tool {
public:
    std::string name() const override { return "cron_toggle"; }
    
    std::string description() const override {
        return "Enable or disable a cron task.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["id"]["type"] = "string";
        params["properties"]["id"]["description"] = "The task ID";
        params["properties"]["enabled"]["type"] = "boolean";
        params["properties"]["enabled"]["description"] = "Enable (true) or disable (false) the task";
        params["required"] = {"id", "enabled"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        if (!g_cron_manager) {
            return ToolResult::error("Cron manager not available");
        }
        
        std::string id = args["id"];
        bool enabled = args["enabled"];
        
        bool toggled = g_cron_manager->toggle_task(id, enabled);
        
        if (toggled) {
            return ToolResult::ok(nlohmann::json{
                {"success", true},
                {"message", std::string("Task ") + id + (enabled ? " enabled" : " disabled")}
            });
        } else {
            return ToolResult::error("Task not found: " + id);
        }
    }
    
    std::string permission_level() const override { return "write"; }
};

// Register all built-in tools
void register_builtin_tools() {
    ToolRegistry::instance().register_tool<WebSearchTool>();
    ToolRegistry::instance().register_tool<FileReadTool>();
    ToolRegistry::instance().register_tool<FileWriteTool>();
    ToolRegistry::instance().register_tool<ListDirectoryTool>();
    ToolRegistry::instance().register_tool<ExecuteTool>();
    ToolRegistry::instance().register_tool<TimeTool>();
    // Cron tools
    ToolRegistry::instance().register_tool<CronListTool>();
    ToolRegistry::instance().register_tool<CronAddTool>();
    ToolRegistry::instance().register_tool<CronRemoveTool>();
    ToolRegistry::instance().register_tool<CronToggleTool>();
}

} // namespace agent
