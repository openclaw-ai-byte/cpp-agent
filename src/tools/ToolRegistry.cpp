#include "tools/ToolRegistry.hpp"
#include <spdlog/spdlog.h>

namespace agent {

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

void ToolRegistry::unregister_tool(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    tools_.erase(name);
}

std::shared_ptr<Tool> ToolRegistry::get_tool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tools_.find(name);
    return it != tools_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Tool>> ToolRegistry::get_all_tools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Tool>> result;
    for (const auto& [_, tool] : tools_) result.push_back(tool);
    return result;
}

std::vector<nlohmann::json> ToolRegistry::get_tool_schemas() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> schemas;
    for (const auto& [_, tool] : tools_) schemas.push_back(tool->schema().to_json());
    return schemas;
}

ToolResult ToolRegistry::execute_tool(const std::string& name, const nlohmann::json& arguments) {
    auto tool = get_tool(name);
    if (!tool) return ToolResult::error("Tool not found: " + name);
    
    try {
        spdlog::debug("Executing tool '{}' with args: {}", name, arguments.dump());
        return tool->execute(arguments);
    } catch (const std::exception& e) {
        return ToolResult::error(std::string("Execution error: ") + e.what());
    }
}

bool ToolRegistry::has_tool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tools_.find(name) != tools_.end();
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : tools_) names.push_back(name);
    return names;
}

} // namespace agent
