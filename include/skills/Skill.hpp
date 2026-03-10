#pragma once

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>

namespace agent {

namespace asio = boost::asio;

struct SkillMetadata {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::vector<std::string> tags;
    std::vector<std::string> dependencies;
};

struct SkillContext {
    class Agent* agent;
    nlohmann::json config;
    std::unordered_map<std::string, std::string> variables;
};

struct SkillResult {
    bool success;
    std::string message;
    nlohmann::json data;
    
    static SkillResult ok(const std::string& msg = "", const nlohmann::json& data = {}) {
        return {true, msg, data};
    }
    
    static SkillResult error(const std::string& msg) {
        return {false, msg, {}};
    }
};

class Skill {
public:
    virtual ~Skill() = default;
    
    virtual SkillMetadata metadata() const = 0;
    virtual bool initialize(const SkillContext& context) { return true; }
    virtual void cleanup() {}
    
    // Synchronous execution
    virtual SkillResult execute(
        const std::string& action,
        const nlohmann::json& params,
        const SkillContext& context
    ) = 0;
    
    // Asynchronous execution (C++20 coroutine)
    // Default implementation wraps sync in thread pool
    virtual asio::awaitable<SkillResult> execute_async(
        const std::string& action,
        const nlohmann::json& params,
        const SkillContext& context
    );
    
    virtual std::vector<std::string> actions() const = 0;
    virtual std::vector<std::string> required_tools() const { return {}; }
};

} // namespace agent
