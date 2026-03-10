#pragma once

#include "Tool.hpp"
#include "mcp/MCPClient.hpp"
#include <memory>

namespace agent {

// MCP Tool wrapper - wraps an MCP server tool as a local Tool
class MCPTool : public Tool {
public:
    MCPTool(std::shared_ptr<mcp::MCPClient> client, const mcp::MCPTool& mcp_tool)
        : client_(client), mcp_tool_(mcp_tool) {}
    
    std::string name() const override {
        return "mcp_" + mcp_tool_.name;
    }
    
    std::string description() const override {
        return mcp_tool_.description;
    }
    
    ToolSchema schema() const override {
        return ToolSchema{name(), description(), mcp_tool_.input_schema};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        auto result = client_->call_tool(mcp_tool_.name, args);
        
        // Extract text content
        std::string text;
        for (const auto& c : result.content) {
            if (c.type == "text" && !c.text.empty()) {
                text += c.text;
            }
        }
        
        if (result.is_error) {
            return ToolResult::error(result.error_message.empty() ? text : result.error_message);
        }
        
        if (!text.empty()) {
            return ToolResult::ok(text);
        }
        
        // Return JSON if no text
        nlohmann::json data;
        data["content"] = nlohmann::json::array();
        for (const auto& c : result.content) {
            data["content"].push_back({
                {"type", c.type},
                {"text", c.text}
            });
        }
        return ToolResult::ok(data);
    }
    
    const mcp::MCPTool& mcp_tool() const { return mcp_tool_; }
    
private:
    std::shared_ptr<mcp::MCPClient> client_;
    mcp::MCPTool mcp_tool_;
};

} // namespace agent
