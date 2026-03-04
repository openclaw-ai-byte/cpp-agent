#pragma once

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

namespace agent::mcp {

struct MCPServerInfo {
    std::string name;
    std::string version;
    std::vector<std::string> capabilities;
};

struct MCPTool {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
};

struct MCPPrompt {
    std::string name;
    std::string description;
    std::vector<std::string> arguments;
};

struct MCPResource {
    std::string uri;
    std::string name;
    std::string mime_type;
};

class MCPClient {
public:
    virtual ~MCPClient() = default;
    
    virtual bool connect(const std::string& endpoint) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
    virtual MCPServerInfo get_server_info() const = 0;
    
    virtual std::vector<MCPTool> list_tools() = 0;
    virtual nlohmann::json call_tool(const std::string& name, const nlohmann::json& arguments) = 0;
    
    virtual std::vector<MCPPrompt> list_prompts() = 0;
    virtual std::string get_prompt(const std::string& name, const nlohmann::json& arguments) = 0;
    
    virtual std::vector<MCPResource> list_resources() = 0;
    virtual std::string read_resource(const std::string& uri) = 0;
};

class MCPClientFactory {
public:
    static std::unique_ptr<MCPClient> create(const std::string& transport);
};

} // namespace agent::mcp
