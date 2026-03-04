#pragma once

#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

namespace agent {

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
};

class Skill {
public:
    virtual ~Skill() = default;
    
    virtual SkillMetadata metadata() const = 0;
    virtual bool initialize(const SkillContext& context) { return true; }
    virtual void cleanup() {}
    virtual SkillResult execute(
        const std::string& action,
        const nlohmann::json& params,
        const SkillContext& context
    ) = 0;
    virtual std::vector<std::string> actions() const = 0;
    virtual std::vector<std::string> required_tools() const { return {}; }
};

} // namespace agent
