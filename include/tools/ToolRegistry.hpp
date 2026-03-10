#pragma once

#include "Tool.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace agent {

class ToolRegistry {
public:
    static ToolRegistry& instance();
    
    // Add an existing tool instance
    void add_tool(std::shared_ptr<Tool> tool);
    
    // Template for creating and adding a tool
    template<typename T, typename... Args>
    void register_tool(Args&&... args) {
        auto tool = std::make_shared<T>(std::forward<Args>(args)...);
        add_tool(tool);
    }
    
    void unregister_tool(const std::string& name);
    
    // Retrieval
    std::shared_ptr<Tool> get_tool(const std::string& name) const;
    std::vector<std::shared_ptr<Tool>> get_all_tools() const;
    std::vector<nlohmann::json> get_tool_schemas() const;
    
    // Synchronous execution
    ToolResult execute_tool(
        const std::string& name,
        const nlohmann::json& arguments
    );
    
    // Asynchronous execution (C++20 coroutine)
    asio::awaitable<ToolResult> execute_tool_async(
        const std::string& name,
        const nlohmann::json& arguments
    );
    
    // Check if tool exists
    bool has_tool(const std::string& name) const;
    
    // List tool names
    std::vector<std::string> list_tools() const;
    
private:
    ToolRegistry() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Tool>> tools_;
};

} // namespace agent
