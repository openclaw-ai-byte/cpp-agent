#pragma once

#include "Tool.hpp"
#include <unordered_map>
#include <mutex>

namespace agent {

class ToolRegistry {
public:
    static ToolRegistry& instance();
    
    template<typename T, typename... Args>
    void register_tool(Args&&... args) {
        auto tool = std::make_shared<T>(std::forward<Args>(args)...);
        std::lock_guard<std::mutex> lock(mutex_);
        tools_[tool->name()] = tool;
    }
    
    void unregister_tool(const std::string& name);
    std::shared_ptr<Tool> get_tool(const std::string& name) const;
    std::vector<std::shared_ptr<Tool>> get_all_tools() const;
    std::vector<nlohmann::json> get_tool_schemas() const;
    ToolResult execute_tool(const std::string& name, const nlohmann::json& arguments);
    bool has_tool(const std::string& name) const;
    std::vector<std::string> list_tools() const;
    
private:
    ToolRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Tool>> tools_;
};

} // namespace agent
