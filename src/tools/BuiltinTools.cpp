#include "tools/Tool.hpp"
#include "tools/ToolRegistry.hpp"
#include <boost/filesystem.hpp>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <array>

namespace fs = boost::filesystem;

namespace agent {

// Web Search Tool
class WebSearchTool : public Tool {
public:
    std::string name() const override { return "web_search"; }
    
    std::string description() const override {
        return "Search the web for information. Returns relevant search results.";
    }
    
    ToolSchema schema() const override {
        nlohmann::json params;
        params["type"] = "object";
        params["properties"]["query"]["type"] = "string";
        params["properties"]["query"]["description"] = "The search query";
        params["properties"]["num_results"]["type"] = "integer";
        params["properties"]["num_results"]["description"] = "Number of results to return";
        params["properties"]["num_results"]["default"] = 5;
        params["required"] = {"query"};
        return ToolSchema{name(), description(), params};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string query = args["query"];
        int num_results = args.value("num_results", 5);
        
        // TODO: Implement actual web search API call
        return ToolResult::ok(nlohmann::json{
            {"query", query},
            {"results", {
                {{"title", "Example Result 1"}, {"url", "https://example.com/1"}, {"snippet", "This is an example search result..."}},
                {{"title", "Example Result 2"}, {"url", "https://example.com/2"}, {"snippet", "Another example result..."}}
            }}
        });
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

// Register all built-in tools
void register_builtin_tools() {
    ToolRegistry::instance().register_tool<WebSearchTool>();
    ToolRegistry::instance().register_tool<FileReadTool>();
    ToolRegistry::instance().register_tool<FileWriteTool>();
    ToolRegistry::instance().register_tool<ListDirectoryTool>();
    ToolRegistry::instance().register_tool<ExecuteTool>();
    ToolRegistry::instance().register_tool<TimeTool>();
}

} // namespace agent
