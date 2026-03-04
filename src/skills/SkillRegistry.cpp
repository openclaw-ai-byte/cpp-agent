#include "skills/SkillRegistry.hpp"
#include "skills/Skill.hpp"
#include <spdlog/spdlog.h>
#include <boost/filesystem.hpp>
#include <fstream>

namespace fs = boost::filesystem;

namespace agent {

SkillRegistry& SkillRegistry::instance() {
    static SkillRegistry registry;
    return registry;
}

void SkillRegistry::register_skill(std::shared_ptr<Skill> skill) {
    if (!skill) return;
    auto meta = skill->metadata();
    std::lock_guard<std::mutex> lock(mutex_);
    skills_[meta.name] = skill;
    spdlog::info("Registered skill: {} v{}", meta.name, meta.version);
}

void SkillRegistry::unregister_skill(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skills_.find(name);
    if (it != skills_.end()) {
        it->second->cleanup();
        skills_.erase(it);
        spdlog::info("Unregistered skill: {}", name);
    }
}

void SkillRegistry::load_from_directory(const fs::path& dir) {
    if (!fs::exists(dir)) {
        spdlog::debug("Skill directory not found: {}", dir.string());
        return;
    }
    
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (fs::is_directory(entry)) {
            // Look for skill definition files
            fs::path skill_file = entry.path() / "skill.yaml";
            if (!fs::exists(skill_file)) {
                skill_file = entry.path() / "skill.json";
            }
            
            if (fs::exists(skill_file)) {
                spdlog::info("Found skill definition: {}", skill_file.string());
                // TODO: Implement skill loading from YAML/JSON
            }
        }
    }
}

std::shared_ptr<Skill> SkillRegistry::get_skill(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = skills_.find(name);
    return it != skills_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Skill>> SkillRegistry::get_all_skills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Skill>> result;
    result.reserve(skills_.size());
    for (const auto& [name, skill] : skills_) {
        result.push_back(skill);
    }
    return result;
}

SkillResult SkillRegistry::execute_skill(
    const std::string& name,
    const std::string& action,
    const nlohmann::json& params,
    const SkillContext& context
) {
    auto skill = get_skill(name);
    if (!skill) {
        return {false, "Skill not found: " + name, {}};
    }
    
    // Check if action is supported
    auto actions = skill->actions();
    if (std::find(actions.begin(), actions.end(), action) == actions.end()) {
        return {false, "Unknown action: " + action + " for skill: " + name, {}};
    }
    
    try {
        return skill->execute(action, params, context);
    } catch (const std::exception& e) {
        return {false, std::string("Exception: ") + e.what(), {}};
    }
}

bool SkillRegistry::has_skill(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return skills_.find(name) != skills_.end();
}

std::vector<std::string> SkillRegistry::list_skills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(skills_.size());
    for (const auto& [name, skill] : skills_) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> SkillRegistry::find_by_tag(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& [name, skill] : skills_) {
        auto meta = skill->metadata();
        if (std::find(meta.tags.begin(), meta.tags.end(), tag) != meta.tags.end()) {
            result.push_back(name);
        }
    }
    return result;
}

} // namespace agent
