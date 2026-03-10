#include "skills/SkillRegistry.hpp"
#include "skills/Skill.hpp"
#include <spdlog/spdlog.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace fs = boost::filesystem;

namespace agent {

// Dynamic skill loaded from config file
class DynamicSkill : public Skill {
public:
    DynamicSkill(const SkillMetadata& meta, 
                 const std::vector<std::string>& actions,
                 const std::map<std::string, nlohmann::json>& action_handlers)
        : metadata_(meta)
        , actions_(actions)
        , action_handlers_(action_handlers) {}
    
    SkillMetadata metadata() const override { return metadata_; }
    
    std::vector<std::string> actions() const override { return actions_; }
    
    SkillResult execute(const std::string& action,
                        const nlohmann::json& params,
                        const SkillContext& context) override {
        auto it = action_handlers_.find(action);
        if (it == action_handlers_.end()) {
            return {false, "Unknown action: " + action, {}};
        }
        
        // For now, return the configured response or execute a command
        const auto& handler = it->second;
        
        // If handler has a "command" field, execute it
        if (handler.contains("command")) {
            std::string command = handler["command"].get<std::string>();
            
            // Simple variable substitution
            for (auto& [key, value] : params.items()) {
                std::string placeholder = "${" + key + "}";
                size_t pos;
                while ((pos = command.find(placeholder)) != std::string::npos) {
                    std::string val_str;
                    if (value.is_string()) {
                        val_str = value.get<std::string>();
                    } else {
                        val_str = value.dump();
                    }
                    command.replace(pos, placeholder.length(), val_str);
                }
            }
            
            // Execute command
            std::array<char, 128> buffer;
            std::string result;
            FILE* pipe = popen(command.c_str(), "r");
            if (!pipe) {
                return {false, "Failed to execute command", {}};
            }
            while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
                result += buffer.data();
            }
            int exit_code = pclose(pipe);
            
            return {exit_code == 0, result, {{"exit_code", exit_code}, {"output", result}}};
        }
        
        // Otherwise return static response
        return {true, "Action executed", handler};
    }

private:
    SkillMetadata metadata_;
    std::vector<std::string> actions_;
    std::map<std::string, nlohmann::json> action_handlers_;
};

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
            fs::path yaml_file = entry.path() / "skill.yaml";
            fs::path json_file = entry.path() / "skill.json";
            
            if (fs::exists(yaml_file)) {
                load_skill_yaml(yaml_file);
            } else if (fs::exists(json_file)) {
                load_skill_json(json_file);
            }
        }
    }
}

void SkillRegistry::load_skill_yaml(const fs::path& path) {
    try {
        YAML::Node config = YAML::LoadFile(path.string());
        
        // Parse metadata
        SkillMetadata meta;
        meta.name = config["name"].as<std::string>();
        meta.version = config["version"].as<std::string>("1.0.0");
        meta.description = config["description"].as<std::string>("");
        meta.author = config["author"].as<std::string>("");
        
        if (config["tags"]) {
            for (const auto& tag : config["tags"]) {
                meta.tags.push_back(tag.as<std::string>());
            }
        }
        
        if (config["dependencies"]) {
            for (const auto& dep : config["dependencies"]) {
                meta.dependencies.push_back(dep.as<std::string>());
            }
        }
        
        // Parse actions
        std::vector<std::string> actions;
        std::map<std::string, nlohmann::json> action_handlers;
        
        if (config["actions"]) {
            for (const auto& action_node : config["actions"]) {
                std::string action_name = action_node.first.as<std::string>();
                actions.push_back(action_name);
                
                const auto& handler = action_node.second;
                nlohmann::json handler_json;
                
                if (handler["command"]) {
                    handler_json["command"] = handler["command"].as<std::string>();
                }
                if (handler["description"]) {
                    handler_json["description"] = handler["description"].as<std::string>();
                }
                if (handler["params"]) {
                    handler_json["params"] = nlohmann::json::object();
                    for (const auto& param : handler["params"]) {
                        std::string param_name = param.first.as<std::string>();
                        handler_json["params"][param_name] = param.second.as<std::string>();
                    }
                }
                
                action_handlers[action_name] = handler_json;
            }
        }
        
        auto skill = std::make_shared<DynamicSkill>(meta, actions, action_handlers);
        register_skill(skill);
        
        spdlog::info("Loaded skill from YAML: {} ({} actions)", meta.name, actions.size());
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load skill from {}: {}", path.string(), e.what());
    }
}

void SkillRegistry::load_skill_json(const fs::path& path) {
    try {
        std::ifstream file(path.string());
        if (!file.is_open()) {
            spdlog::error("Cannot open skill file: {}", path.string());
            return;
        }
        
        nlohmann::json config;
        file >> config;
        
        // Parse metadata
        SkillMetadata meta;
        meta.name = config["name"].get<std::string>();
        meta.version = config.value("version", "1.0.0");
        meta.description = config.value("description", "");
        meta.author = config.value("author", "");
        
        if (config.contains("tags")) {
            for (const auto& tag : config["tags"]) {
                meta.tags.push_back(tag.get<std::string>());
            }
        }
        
        if (config.contains("dependencies")) {
            for (const auto& dep : config["dependencies"]) {
                meta.dependencies.push_back(dep.get<std::string>());
            }
        }
        
        // Parse actions
        std::vector<std::string> actions;
        std::map<std::string, nlohmann::json> action_handlers;
        
        if (config.contains("actions")) {
            for (auto& [action_name, handler] : config["actions"].items()) {
                actions.push_back(action_name);
                action_handlers[action_name] = handler;
            }
        }
        
        auto skill = std::make_shared<DynamicSkill>(meta, actions, action_handlers);
        register_skill(skill);
        
        spdlog::info("Loaded skill from JSON: {} ({} actions)", meta.name, actions.size());
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load skill from {}: {}", path.string(), e.what());
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

// Default async implementation for Skill
asio::awaitable<SkillResult> Skill::execute_async(
    const std::string& action,
    const nlohmann::json& params,
    const SkillContext& context) {
    auto executor = co_await asio::this_coro::executor;
    co_return co_await asio::co_spawn(
        executor,
        [this, action, params, context]() -> asio::awaitable<SkillResult> {
            co_return execute(action, params, context);
        },
        asio::use_awaitable
    );
}

} // namespace agent
