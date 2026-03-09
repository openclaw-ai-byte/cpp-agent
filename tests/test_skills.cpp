// Test Skill Registry and Dynamic Loading
#include "test_framework.hpp"
#include "skills/SkillRegistry.hpp"
#include <fstream>

using namespace agent;
using json = nlohmann::json;

TEST(skill_registry_singleton) {
    auto& registry = SkillRegistry::instance();
    auto& registry2 = SkillRegistry::instance();
    
    test::assert_true(&registry == &registry2, "Registry should be a singleton");
}

TEST(load_skill_json) {
    // Create a temporary skill file
    std::string skill_dir = "/tmp/test_skills/test_skill";
    std::string skill_file = skill_dir + "/skill.json";
    
    // Create directory
    system("mkdir -p /tmp/test_skills/test_skill");
    
    // Write skill JSON
    json skill_data = {
        {"name", "test_skill"},
        {"version", "1.0.0"},
        {"description", "A test skill"},
        {"author", "test"},
        {"actions", {
            {"echo", {
                {"command", "echo 'Hello from test skill'"},
                {"description", "Echo a message"}
            }},
            {"date", {
                {"command", "date"},
                {"description", "Get current date"}
            }}
        }}
    };
    
    {
        std::ofstream f(skill_file);
        f << skill_data.dump(2);
    }
    
    // Load skill
    auto& registry = SkillRegistry::instance();
    registry.load_skill_json(skill_file);
    
    // Check skill was loaded
    test::assert_true(registry.has_skill("test_skill"), "Skill should be registered");
    
    auto skills = registry.list_skills();
    bool found = false;
    for (const auto& s : skills) {
        if (s == "test_skill") found = true;
    }
    test::assert_true(found, "test_skill should be in skill list");
    
    // Cleanup
    system("rm -rf /tmp/test_skills");
}

TEST(execute_skill_action) {
    // Create skill with echo action
    std::string skill_dir = "/tmp/test_skills/echo_skill";
    std::string skill_file = skill_dir + "/skill.json";
    
    system("mkdir -p /tmp/test_skills/echo_skill");
    
    json skill_data = {
        {"name", "echo_skill"},
        {"version", "1.0.0"},
        {"actions", {
            {"say_hello", {
                {"command", "echo 'Hello, World!'"}
            }}
        }}
    };
    
    {
        std::ofstream f(skill_file);
        f << skill_data.dump(2);
    }
    
    auto& registry = SkillRegistry::instance();
    registry.load_skill_json(skill_file);
    
    // Execute skill action
    SkillContext ctx;
    ctx.agent = nullptr;
    
    auto result = registry.execute_skill("echo_skill", "say_hello", json::object(), ctx);
    
    test::assert_true(result.success, "Skill execution should succeed");
    test::assert_contains(result.data["output"].get<std::string>(), "Hello, World!");
    
    system("rm -rf /tmp/test_skills");
}

TEST(skill_metadata) {
    std::string skill_dir = "/tmp/test_skills/meta_skill";
    std::string skill_file = skill_dir + "/skill.json";
    
    system("mkdir -p /tmp/test_skills/meta_skill");
    
    json skill_data = {
        {"name", "meta_skill"},
        {"version", "2.1.0"},
        {"description", "Skill with metadata"},
        {"author", "Test Author"},
        {"tags", {"utility", "test"}},
        {"actions", {
            {"do_something", {{"command", "true"}}}
        }}
    };
    
    {
        std::ofstream f(skill_file);
        f << skill_data.dump(2);
    }
    
    auto& registry = SkillRegistry::instance();
    registry.load_skill_json(skill_file);
    
    auto skill = registry.get_skill("meta_skill");
    test::assert_true(skill != nullptr, "Skill should exist");
    
    auto meta = skill->metadata();
    test::assert_eq(meta.name, std::string("meta_skill"));
    test::assert_eq(meta.version, std::string("2.1.0"));
    test::assert_eq(meta.description, std::string("Skill with metadata"));
    test::assert_eq(meta.author, std::string("Test Author"));
    test::assert_eq(meta.tags.size(), size_t(2));
    
    system("rm -rf /tmp/test_skills");
}
