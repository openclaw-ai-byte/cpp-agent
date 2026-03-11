#pragma once

#include "tools/Tool.hpp"
#include "memory/GoalTracker.hpp"
#include "memory/SessionAnalyzer.hpp"
#include "memory/SleepProcessor.hpp"
#include <memory>

namespace agent {

/**
 * SelfEvolutionTool - Tool for self-improvement and goal tracking
 * 
 * Exposes the following capabilities to the agent:
 * 
 * Goal Management:
 *   - goal_create: Create a new goal
 *   - goal_list: List all goals
 *   - goal_update: Update goal progress/status
 *   - goal_checkin: Record a check-in for a goal
 *   - goal_report: Generate progress report
 * 
 * Pattern Analysis:
 *   - pattern_detect: Detect patterns from sessions
 *   - pattern_list: List detected patterns
 *   - insights: Generate insights
 *   - improvements: Get improvement suggestions
 * 
 * Sleep Processing:
 *   - sleep_process: Run sleep consolidation
 *   - memory_list: List long-term memories
 *   - predictions: Get predictions for next session
 *   - wake_report: Generate wake report
 */
class SelfEvolutionTool : public Tool {
public:
    explicit SelfEvolutionTool(
        std::shared_ptr<GoalTracker> goal_tracker,
        std::shared_ptr<SessionAnalyzer> session_analyzer,
        std::shared_ptr<SleepProcessor> sleep_processor
    );
    
    ~SelfEvolutionTool() override = default;
    
    std::string name() const override { return "self_evolution"; }
    
    std::string description() const override {
        return "Self-improvement and goal tracking system. "
               "Manage long-term goals, detect patterns from interactions, "
               "and process learnings for continuous improvement.";
    }
    
    ToolSchema schema() const override;
    
    ToolResult execute(const nlohmann::json& arguments) override;
    
    std::string permission_level() const override { return "read"; }
    bool requires_confirmation() const override { return false; }

private:
    std::shared_ptr<GoalTracker> goal_tracker_;
    std::shared_ptr<SessionAnalyzer> session_analyzer_;
    std::shared_ptr<SleepProcessor> sleep_processor_;
    
    // Goal operations
    ToolResult handle_goal_create(const nlohmann::json& args);
    ToolResult handle_goal_list(const nlohmann::json& args);
    ToolResult handle_goal_update(const nlohmann::json& args);
    ToolResult handle_goal_checkin(const nlohmann::json& args);
    ToolResult handle_goal_report(const nlohmann::json& args);
    ToolResult handle_goal_attention(const nlohmann::json& args);
    
    // Pattern operations
    ToolResult handle_pattern_detect(const nlohmann::json& args);
    ToolResult handle_pattern_list(const nlohmann::json& args);
    ToolResult handle_insights(const nlohmann::json& args);
    ToolResult handle_improvements(const nlohmann::json& args);
    
    // Sleep operations
    ToolResult handle_sleep_process(const nlohmann::json& args);
    ToolResult handle_memory_list(const nlohmann::json& args);
    ToolResult handle_predictions(const nlohmann::json& args);
    ToolResult handle_wake_report(const nlohmann::json& args);
    
    // Session operations
    ToolResult handle_session_start(const nlohmann::json& args);
    ToolResult handle_session_event(const nlohmann::json& args);
    ToolResult handle_session_end(const nlohmann::json& args);
};

} // namespace agent
