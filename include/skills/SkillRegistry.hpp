#pragma once

#include "Skill.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <mutex>

namespace agent {

class SkillRegistry {
public:
    static SkillRegistry& instance();
    
    // Registration
    void register_skill(std::shared_ptr<Skill> skill);
    void unregister_skill(const std::string& name);
    
    // Loading skills from directories
    void load_from_directory(const std::filesystem::path& dir);
    
    // Retrieval
    std::shared_ptr<Skill> get_skill(const std::string& name) const;
    std::vector<std::shared_ptr<Skill>> get_all_skills() const;
    
    // Execution
    SkillResult execute_skill(
        const std::string& name,
        const std::string& action,
        const nlohmann::json& params,
        const SkillContext& context
    );
    
    // Query
    bool has_skill(const std::string& name) const;
    std::vector<std::string> list_skills() const;
    std::vector<std::string> find_by_tag(const std::string& tag) const;
    
private:
    SkillRegistry() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Skill>> skills_;
};

} // namespace agent
