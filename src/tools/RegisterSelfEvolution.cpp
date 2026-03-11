#include "tools/SelfEvolutionTool.hpp"
#include "tools/ToolRegistry.hpp"
#include "memory/GoalTracker.hpp"
#include "memory/SessionAnalyzer.hpp"
#include "memory/SleepProcessor.hpp"
#include <memory>
#include <spdlog/spdlog.h>

namespace agent {

// Global instances for self-evolution system
static std::shared_ptr<GoalTracker> g_goal_tracker;
static std::shared_ptr<SessionAnalyzer> g_session_analyzer;
static std::shared_ptr<SleepProcessor> g_sleep_processor;

void init_self_evolution(const std::string& data_dir = ".") {
    // Initialize components with data files in the specified directory
    g_goal_tracker = std::make_shared<GoalTracker>(data_dir + "/goals.json");
    g_session_analyzer = std::make_shared<SessionAnalyzer>(data_dir + "/sessions.json");
    g_sleep_processor = std::make_shared<SleepProcessor>(
        *g_goal_tracker, 
        *g_session_analyzer,
        data_dir + "/memory.json"
    );
    
    // Register the self-evolution tool
    auto tool = std::make_shared<SelfEvolutionTool>(
        g_goal_tracker,
        g_session_analyzer,
        g_sleep_processor
    );
    
    ToolRegistry::instance().add_tool(tool);
    
    spdlog::info("Self-evolution system initialized");
    spdlog::info("  Goals: {}", g_goal_tracker->total_goals());
    spdlog::info("  Sessions: {}", g_session_analyzer->total_sessions());
    spdlog::info("  Memories: {}", g_sleep_processor->get_long_term_memory().size());
}

// Accessors for API handlers
std::shared_ptr<GoalTracker> get_goal_tracker() { return g_goal_tracker; }
std::shared_ptr<SessionAnalyzer> get_session_analyzer() { return g_session_analyzer; }
std::shared_ptr<SleepProcessor> get_sleep_processor() { return g_sleep_processor; }

} // namespace agent
