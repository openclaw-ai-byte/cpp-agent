#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>

namespace agent::mcp {

// MCP Server information
struct MCPServerInfo {
    std::string name;
    std::string version;
    nlohmann::json capabilities;
    
    bool has_tools() const {
        return capabilities.value("tools", false);
    }
    bool has_prompts() const {
        return capabilities.value("prompts", false);
    }
    bool has_resources() const {
        return capabilities.value("resources", false);
    }
};

// MCP Tool definition
struct MCPTool {
    std::string name;
    std::string description;
    nlohmann::json input_schema;  // JSON Schema for input
};

// MCP Prompt definition
struct MCPPrompt {
    std::string name;
    std::string description;
    nlohmann::json arguments;  // Array of argument definitions
};

// MCP Resource definition
struct MCPResource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

// MCP Content types
struct MCPContent {
    std::string type;  // "text", "image", "resource"
    std::string text;
    std::string data;  // For base64 data
    std::string mime_type;
    std::string uri;   // For resource references
};

// MCP Tool result
struct MCPToolResult {
    bool is_error = false;
    std::vector<MCPContent> content;
    std::string error_message;
};

// Progress callback for long-running operations
using ProgressCallback = std::function<void(double progress, const std::string& message)>;

// MCP Client interface
class MCPClient {
public:
    virtual ~MCPClient() = default;
    
    // Connection management
    virtual bool connect(const std::string& endpoint) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    
    // Server info
    virtual MCPServerInfo get_server_info() const = 0;
    
    // Tools API
    virtual std::vector<MCPTool> list_tools() = 0;
    virtual MCPToolResult call_tool(
        const std::string& name,
        const nlohmann::json& arguments,
        ProgressCallback progress = nullptr
    ) = 0;
    
    // Prompts API
    virtual std::vector<MCPPrompt> list_prompts() = 0;
    virtual std::string get_prompt(
        const std::string& name,
        const nlohmann::json& arguments = {}
    ) = 0;
    
    // Resources API
    virtual std::vector<MCPResource> list_resources() = 0;
    virtual std::string read_resource(const std::string& uri) = 0;
    
    // Logging
    virtual void set_logging_level(const std::string& level) = 0;
};

// Factory for creating MCP clients
class MCPClientFactory {
public:
    // Transport types: "stdio", "http", "sse"
    static std::unique_ptr<MCPClient> create(const std::string& transport);
};

} // namespace agent::mcp
