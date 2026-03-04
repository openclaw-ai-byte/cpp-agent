#pragma once

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace agent {

struct ToolSchema {
    std::string name;
    std::string description;
    nlohmann::json parameters;
    
    nlohmann::json to_json() const {
        return {
            {"type", "function"},
            {"function", {
                {"name", name},
                {"description", description},
                {"parameters", parameters}
            }}
        };
    }
};

struct ToolResult {
    bool success;
    std::string output;
    nlohmann::json data;
    
    static ToolResult ok(const std::string& output) {
        return {true, output, {}};
    }
    
    static ToolResult ok(const nlohmann::json& data) {
        return {true, data.dump(2), data};
    }
    
    static ToolResult error(const std::string& message) {
        return {false, message, {}};
    }
};

class Tool {
public:
    virtual ~Tool() = default;
    
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual ToolSchema schema() const = 0;
    virtual ToolResult execute(const nlohmann::json& arguments) = 0;
    
    virtual std::string permission_level() const { return "read"; }
    virtual bool requires_confirmation() const { return false; }
};

#define REGISTER_TOOL(ToolClass) \
    static bool _##ToolClass##_registered = []() { \
        ToolRegistry::instance().register_tool<ToolClass>(); \
        return true; \
    }();

} // namespace agent
