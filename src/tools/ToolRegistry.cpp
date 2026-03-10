#include "tools/ToolRegistry.hpp"
#include <spdlog/spdlog.h>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace agent {

// Default async implementation for Tool (wraps sync in thread pool)
asio::awaitable<ToolResult> Tool::execute_async(const nlohmann::json& arguments) {
    auto executor = co_await asio::this_coro::executor;
    co_return co_await asio::co_spawn(
        executor,
        [this, arguments]() -> asio::awaitable<ToolResult> {
            co_return execute(arguments);
        },
        asio::use_awaitable
    );
}

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

void ToolRegistry::add_tool(std::shared_ptr<Tool> tool) {
    if (!tool) return;
    std::lock_guard<std::mutex> lock(mutex_);
    tools_[tool->name()] = tool;
    spdlog::info("Registered tool: {}", tool->name());
}

void ToolRegistry::unregister_tool(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    tools_.erase(name);
    spdlog::info("Unregistered tool: {}", name);
}

std::shared_ptr<Tool> ToolRegistry::get_tool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Tool>> ToolRegistry::get_all_tools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Tool>> result;
    result.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        result.push_back(tool);
    }
    return result;
}

std::vector<nlohmann::json> ToolRegistry::get_tool_schemas() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> schemas;
    schemas.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        schemas.push_back(tool->schema().to_json());
    }
    return schemas;
}

ToolResult ToolRegistry::execute_tool(
    const std::string& name,
    const nlohmann::json& arguments
) {
    auto tool = get_tool(name);
    if (!tool) {
        spdlog::error("Tool not found: {}", name);
        return ToolResult::error("Tool not found: " + name);
    }
    
    spdlog::info("Executing tool '{}' with args: {}", name, arguments.dump());
    
    try {
        auto result = tool->execute(arguments);
        spdlog::info("Tool '{}' completed: success={}", name, result.success);
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Tool '{}' failed: {}", name, e.what());
        return ToolResult::error(std::string("Exception: ") + e.what());
    }
}

asio::awaitable<ToolResult> ToolRegistry::execute_tool_async(
    const std::string& name,
    const nlohmann::json& arguments
) {
    auto tool = get_tool(name);
    if (!tool) {
        spdlog::error("Tool not found: {}", name);
        co_return ToolResult::error("Tool not found: " + name);
    }
    
    spdlog::info("Executing tool '{}' async with args: {}", name, arguments.dump());
    
    try {
        auto result = co_await tool->execute_async(arguments);
        spdlog::info("Tool '{}' completed: success={}", name, result.success);
        co_return result;
    } catch (const std::exception& e) {
        spdlog::error("Tool '{}' failed: {}", name, e.what());
        co_return ToolResult::error(std::string("Exception: ") + e.what());
    }
}

bool ToolRegistry::has_tool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tools_.find(name) != tools_.end();
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        names.push_back(name);
    }
    return names;
}

} // namespace agent
