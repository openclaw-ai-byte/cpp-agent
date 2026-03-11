#include "tools/SelfEvolutionTool.hpp"
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace agent {

SelfEvolutionTool::SelfEvolutionTool(
    std::shared_ptr<GoalTracker> goal_tracker,
    std::shared_ptr<SessionAnalyzer> session_analyzer,
    std::shared_ptr<SleepProcessor> sleep_processor
) : goal_tracker_(goal_tracker),
    session_analyzer_(session_analyzer),
    sleep_processor_(sleep_processor) {
}

ToolSchema SelfEvolutionTool::schema() const {
    return ToolSchema{
        .name = "self_evolution",
        .description = description(),
        .parameters = {
            {"type", "object"},
            {"properties", {
                {"action", {
                    {"type", "string"},
                    {"enum", {
                        // Goal operations
                        "goal_create", "goal_list", "goal_update",
                        "goal_checkin", "goal_report", "goal_attention",
                        // Pattern operations
                        "pattern_detect", "pattern_list", "insights", "improvements",
                        // Sleep operations
                        "sleep_process", "memory_list", "predictions", "wake_report",
                        // Session operations
                        "session_start", "session_event", "session_end"
                    }},
                    {"description", "The action to perform"}
                }},
                // Goal parameters
                {"title", {
                    {"type", "string"},
                    {"description", "Goal title (for goal_create)"}
                }},
                {"description", {
                    {"type", "string"},
                    {"description", "Goal description (for goal_create)"}
                }},
                {"priority", {
                    {"type", "string"},
                    {"enum", {"low", "medium", "high", "critical"}},
                    {"description", "Goal priority (for goal_create)"}
                }},
                {"deadline", {
                    {"type", "string"},
                    {"description", "Goal deadline in ISO format (for goal_create)"}
                }},
                {"goal_id", {
                    {"type", "string"},
                    {"description", "Goal ID (for goal_update, goal_checkin)"}
                }},
                {"status", {
                    {"type", "string"},
                    {"enum", {"not_started", "in_progress", "blocked", "completed", "abandoned"}},
                    {"description", "Goal status (for goal_update)"}
                }},
                {"progress", {
                    {"type", "number"},
                    {"minimum", 0},
                    {"maximum", 1},
                    {"description", "Progress value 0.0-1.0 (for goal_checkin, goal_update)"}
                }},
                {"notes", {
                    {"type", "string"},
                    {"description", "Notes for check-in (for goal_checkin)"}
                }},
                {"milestones", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "Completed milestones (for goal_checkin)"}
                }},
                // Pattern parameters
                {"pattern_type", {
                    {"type", "string"},
                    {"enum", {"time_based", "error_based", "preference_based", "sequence_based", "frequency_based"}},
                    {"description", "Filter by pattern type (for pattern_list)"}
                }},
                {"keyword", {
                    {"type", "string"},
                    {"description", "Search keyword (for pattern_list)"}
                }},
                // Session parameters
                {"event_type", {
                    {"type", "string"},
                    {"description", "Event type for session_event"}
                }},
                {"content", {
                    {"type", "string"},
                    {"description", "Event content for session_event"}
                }},
                {"summary", {
                    {"type", "string"},
                    {"description", "Session summary for session_end"}
                }}
            }},
            {"required", {"action"}}
        }
    };
}

ToolResult SelfEvolutionTool::execute(const nlohmann::json& arguments) {
    if (!arguments.contains("action")) {
        return ToolResult::error("Missing required parameter: action");
    }
    
    std::string action = arguments["action"].get<std::string>();
    
    // Goal operations
    if (action == "goal_create") return handle_goal_create(arguments);
    if (action == "goal_list") return handle_goal_list(arguments);
    if (action == "goal_update") return handle_goal_update(arguments);
    if (action == "goal_checkin") return handle_goal_checkin(arguments);
    if (action == "goal_report") return handle_goal_report(arguments);
    if (action == "goal_attention") return handle_goal_attention(arguments);
    
    // Pattern operations
    if (action == "pattern_detect") return handle_pattern_detect(arguments);
    if (action == "pattern_list") return handle_pattern_list(arguments);
    if (action == "insights") return handle_insights(arguments);
    if (action == "improvements") return handle_improvements(arguments);
    
    // Sleep operations
    if (action == "sleep_process") return handle_sleep_process(arguments);
    if (action == "memory_list") return handle_memory_list(arguments);
    if (action == "predictions") return handle_predictions(arguments);
    if (action == "wake_report") return handle_wake_report(arguments);
    
    // Session operations
    if (action == "session_start") return handle_session_start(arguments);
    if (action == "session_event") return handle_session_event(arguments);
    if (action == "session_end") return handle_session_end(arguments);
    
    return ToolResult::error("Unknown action: " + action);
}

// ===== Goal Operations =====

ToolResult SelfEvolutionTool::handle_goal_create(const nlohmann::json& args) {
    if (!args.contains("title")) {
        return ToolResult::error("Missing required parameter: title");
    }
    
    std::string title = args["title"].get<std::string>();
    std::string description = args.value("description", "");
    
    GoalPriority priority = GoalPriority::Medium;
    if (args.contains("priority")) {
        std::string p = args["priority"].get<std::string>();
        if (p == "low") priority = GoalPriority::Low;
        else if (p == "high") priority = GoalPriority::High;
        else if (p == "critical") priority = GoalPriority::Critical;
    }
    
    std::optional<std::chrono::system_clock::time_point> deadline;
    if (args.contains("deadline")) {
        // Parse ISO format deadline
        std::tm tm = {};
        std::istringstream ss(args["deadline"].get<std::string>());
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (!ss.fail()) {
            deadline = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
    }
    
    Goal goal = goal_tracker_->create_goal(title, description, priority, deadline);
    
    nlohmann::json result = goal.to_json();
    result["message"] = "Created goal: " + title;
    return ToolResult::ok(result);
}

ToolResult SelfEvolutionTool::handle_goal_list(const nlohmann::json& args) {
    std::vector<Goal> goals;
    
    if (args.contains("status")) {
        std::string status = args["status"].get<std::string>();
        GoalStatus gs = GoalStatus::NotStarted;
        if (status == "in_progress") gs = GoalStatus::InProgress;
        else if (status == "blocked") gs = GoalStatus::Blocked;
        else if (status == "completed") gs = GoalStatus::Completed;
        else if (status == "abandoned") gs = GoalStatus::Abandoned;
        goals = goal_tracker_->get_goals_by_status(gs);
    } else {
        goals = goal_tracker_->get_all_goals();
    }
    
    nlohmann::json result = {
        {"count", goals.size()},
        {"goals", nlohmann::json::array()}
    };
    
    for (const auto& g : goals) {
        result["goals"].push_back(g.to_json());
    }
    
    return ToolResult::ok(result);
}

ToolResult SelfEvolutionTool::handle_goal_update(const nlohmann::json& args) {
    if (!args.contains("goal_id")) {
        return ToolResult::error("Missing required parameter: goal_id");
    }
    
    auto goal_opt = goal_tracker_->get_goal(args["goal_id"].get<std::string>());
    if (!goal_opt) {
        return ToolResult::error("Goal not found: " + args["goal_id"].get<std::string>());
    }
    
    Goal goal = *goal_opt;
    
    if (args.contains("status")) {
        std::string status = args["status"].get<std::string>();
        if (status == "not_started") goal.status = GoalStatus::NotStarted;
        else if (status == "in_progress") goal.status = GoalStatus::InProgress;
        else if (status == "blocked") goal.status = GoalStatus::Blocked;
        else if (status == "completed") goal.status = GoalStatus::Completed;
        else if (status == "abandoned") goal.status = GoalStatus::Abandoned;
    }
    
    if (args.contains("progress")) {
        goal.progress = args["progress"].get<double>();
    }
    
    if (goal_tracker_->update_goal(goal)) {
        return ToolResult::ok(goal.to_json());
    }
    return ToolResult::error("Failed to update goal");
}

ToolResult SelfEvolutionTool::handle_goal_checkin(const nlohmann::json& args) {
    if (!args.contains("goal_id") || !args.contains("progress")) {
        return ToolResult::error("Missing required parameters: goal_id, progress");
    }
    
    std::string goal_id = args["goal_id"].get<std::string>();
    double progress = args["progress"].get<double>();
    std::string notes = args.value("notes", "");
    std::vector<std::string> milestones;
    if (args.contains("milestones")) {
        milestones = args["milestones"].get<std::vector<std::string>>();
    }
    
    if (goal_tracker_->check_in(goal_id, progress, notes, milestones)) {
        auto goal = goal_tracker_->get_goal(goal_id);
        return ToolResult::ok({
            {"message", "Check-in recorded"},
            {"goal", goal ? goal->to_json() : nlohmann::json()}
        });
    }
    return ToolResult::error("Failed to record check-in");
}

ToolResult SelfEvolutionTool::handle_goal_report(const nlohmann::json& args) {
    std::string report = goal_tracker_->generate_report();
    return ToolResult::ok({
        {"report", report},
        {"stats", {
            {"total", goal_tracker_->total_goals()},
            {"completed", goal_tracker_->completed_goals()},
            {"in_progress", goal_tracker_->in_progress_goals()},
            {"average_progress", goal_tracker_->average_progress()}
        }}
    });
}

ToolResult SelfEvolutionTool::handle_goal_attention(const nlohmann::json& args) {
    auto goals = goal_tracker_->get_goals_needing_attention();
    auto reminders = goal_tracker_->generate_reminders();
    
    nlohmann::json result = {
        {"count", goals.size()},
        {"goals", nlohmann::json::array()},
        {"reminders", reminders}
    };
    
    for (const auto& g : goals) {
        result["goals"].push_back(g.to_json());
    }
    
    return ToolResult::ok(result);
}

// ===== Pattern Operations =====

ToolResult SelfEvolutionTool::handle_pattern_detect(const nlohmann::json& args) {
    auto patterns = session_analyzer_->detect_patterns();
    
    nlohmann::json result = {
        {"message", "Pattern detection complete"},
        {"patterns_found", patterns.size()},
        {"patterns", nlohmann::json::array()}
    };
    
    for (const auto& p : patterns) {
        result["patterns"].push_back(p.to_json());
    }
    
    return ToolResult::ok(result);
}

ToolResult SelfEvolutionTool::handle_pattern_list(const nlohmann::json& args) {
    std::vector<Pattern> patterns;
    
    if (args.contains("pattern_type")) {
        std::string type = args["pattern_type"].get<std::string>();
        PatternType pt = PatternType::TimeBased;
        if (type == "error_based") pt = PatternType::ErrorBased;
        else if (type == "preference_based") pt = PatternType::PreferenceBased;
        else if (type == "sequence_based") pt = PatternType::SequenceBased;
        else if (type == "frequency_based") pt = PatternType::FrequencyBased;
        patterns = session_analyzer_->get_patterns_by_type(pt);
    } else if (args.contains("keyword")) {
        patterns = session_analyzer_->search_patterns(args["keyword"].get<std::string>());
    } else {
        patterns = session_analyzer_->get_patterns();
    }
    
    nlohmann::json result = {
        {"count", patterns.size()},
        {"patterns", nlohmann::json::array()}
    };
    
    for (const auto& p : patterns) {
        result["patterns"].push_back(p.to_json());
    }
    
    return ToolResult::ok(result);
}

ToolResult SelfEvolutionTool::handle_insights(const nlohmann::json& args) {
    auto insights = session_analyzer_->generate_insights();
    
    return ToolResult::ok({
        {"count", insights.size()},
        {"insights", insights}
    });
}

ToolResult SelfEvolutionTool::handle_improvements(const nlohmann::json& args) {
    auto suggestions = session_analyzer_->get_improvement_suggestions();
    
    return ToolResult::ok({
        {"count", suggestions.size()},
        {"suggestions", suggestions}
    });
}

// ===== Sleep Operations =====

ToolResult SelfEvolutionTool::handle_sleep_process(const nlohmann::json& args) {
    SleepResult result = sleep_processor_->process({});
    
    return ToolResult::ok(result.to_json());
}

ToolResult SelfEvolutionTool::handle_memory_list(const nlohmann::json& args) {
    auto memories = sleep_processor_->get_long_term_memory();
    
    nlohmann::json result = {
        {"count", memories.size()},
        {"memories", nlohmann::json::array()}
    };
    
    for (const auto& m : memories) {
        result["memories"].push_back(m.to_json());
    }
    
    return ToolResult::ok(result);
}

ToolResult SelfEvolutionTool::handle_predictions(const nlohmann::json& args) {
    auto predictions = sleep_processor_->get_predictions();
    
    nlohmann::json result = {
        {"count", predictions.size()},
        {"predictions", nlohmann::json::array()}
    };
    
    for (const auto& p : predictions) {
        result["predictions"].push_back(p.to_json());
    }
    
    return ToolResult::ok(result);
}

ToolResult SelfEvolutionTool::handle_wake_report(const nlohmann::json& args) {
    SleepResult sr;
    sr.patterns_detected = session_analyzer_->get_patterns().size();
    sr.reminders = goal_tracker_->generate_reminders();
    sr.recommendations = session_analyzer_->get_improvement_suggestions();
    
    auto predictions = sleep_processor_->get_predictions();
    for (const auto& p : predictions) {
        sr.predicted_tasks.push_back(p.task);
    }
    
    std::string report = sleep_processor_->generate_wake_report(sr);
    
    return ToolResult::ok({
        {"report", report}
    });
}

// ===== Session Operations =====

ToolResult SelfEvolutionTool::handle_session_start(const nlohmann::json& args) {
    std::string session_id = session_analyzer_->start_session();
    
    return ToolResult::ok({
        {"message", "Session started"},
        {"session_id", session_id}
    });
}

ToolResult SelfEvolutionTool::handle_session_event(const nlohmann::json& args) {
    if (!args.contains("event_type") || !args.contains("content")) {
        return ToolResult::error("Missing required parameters: event_type, content");
    }
    
    SessionEvent event;
    event.type = args["event_type"].get<std::string>();
    event.content = args["content"].get<std::string>();
    event.timestamp = std::chrono::system_clock::now();
    
    if (args.contains("metadata")) {
        for (auto it = args["metadata"].begin(); it != args["metadata"].end(); ++it) {
            event.metadata[it.key()] = it.value().get<std::string>();
        }
    }
    
    session_analyzer_->record_event(event);
    
    return ToolResult::ok({
        {"message", "Event recorded"},
        {"event_type", event.type}
    });
}

ToolResult SelfEvolutionTool::handle_session_end(const nlohmann::json& args) {
    std::string summary = args.value("summary", "");
    SessionSummary s = session_analyzer_->end_session(summary);
    
    return ToolResult::ok({
        {"message", "Session ended"},
        {"session_id", s.session_id},
        {"message_count", s.message_count},
        {"tool_call_count", s.tool_call_count},
        {"error_count", s.error_count}
    });
}

} // namespace agent
