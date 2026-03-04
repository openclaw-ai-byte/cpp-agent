#include "tools/Tool.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <memory>

namespace agent::tools {

class ExecuteCommandTool : public Tool {
public:
    std::string name() const override { return "execute_command"; }
    std::string description() const override { return "Execute a shell command"; }
    
    ToolSchema schema() const override {
        return {name(), description(), {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "The command to execute"}}}
            }},
            {"required", {"command"}}
        }};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string command = args["command"];
        std::array<char, 128> buffer;
        std::string result;
        
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) return ToolResult::error("Failed to execute command");
        
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
        int exit_code = pclose(pipe);
        
        return ToolResult::ok({{"output", result}, {"exit_code", exit_code}});
    }
    
    std::string permission_level() const override { return "execute"; }
    bool requires_confirmation() const override { return true; }
};

class ReadFileTool : public Tool {
public:
    std::string name() const override { return "read_file"; }
    std::string description() const override { return "Read file contents"; }
    
    ToolSchema schema() const override {
        return {name(), description(), {
            {"type", "object"},
            {"properties", {{"path", {{"type", "string"}, {"description", "File path"}}}}},
            {"required", {"path"}}
        }};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string path = args["path"];
        if (!std::filesystem::exists(path)) return ToolResult::error("File not found");
        
        std::ifstream file(path);
        if (!file.is_open()) return ToolResult::error("Cannot open file");
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        return ToolResult::ok({{"path", path}, {"content", buffer.str()}});
    }
};

class WriteFileTool : public Tool {
public:
    std::string name() const override { return "write_file"; }
    std::string description() const override { return "Write content to file"; }
    
    ToolSchema schema() const override {
        return {name(), description(), {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}}},
                {"content", {{"type", "string"}}}
            }},
            {"required", {"path", "content"}}
        }};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string path = args["path"], content = args["content"];
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        
        std::ofstream file(path);
        if (!file.is_open()) return ToolResult::error("Cannot write to file");
        
        file << content;
        return ToolResult::ok({{"path", path}, {"bytes_written", content.size()}});
    }
    
    bool requires_confirmation() const override { return true; }
};

class ListDirectoryTool : public Tool {
public:
    std::string name() const override { return "list_directory"; }
    std::string description() const override { return "List directory contents"; }
    
    ToolSchema schema() const override {
        return {name(), description(), {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}}},
                {"recursive", {{"type", "boolean"}, {"default", false}}}
            }},
            {"required", {"path"}}
        }};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string path = args["path"];
        bool recursive = args.value("recursive", false);
        
        if (!std::filesystem::exists(path)) return ToolResult::error("Directory not found");
        
        nlohmann::json items = nlohmann::json::array();
        
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                items.push_back({
                    {"path", entry.path().string()},
                    {"type", entry.is_directory() ? "directory" : "file"},
                    {"size", entry.is_regular_file() ? entry.file_size() : 0}
                });
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                items.push_back({
                    {"path", entry.path().string()},
                    {"type", entry.is_directory() ? "directory" : "file"},
                    {"size", entry.is_regular_file() ? entry.file_size() : 0}
                });
            }
        }
        
        return ToolResult::ok({{"path", path}, {"items", items}});
    }
};

void register_builtin_tools() {
    ToolRegistry::instance().register_tool<ExecuteCommandTool>();
    ToolRegistry::instance().register_tool<ReadFileTool>();
    ToolRegistry::instance().register_tool<WriteFileTool>();
    ToolRegistry::instance().register_tool<ListDirectoryTool>();
}

} // namespace agent::tools
