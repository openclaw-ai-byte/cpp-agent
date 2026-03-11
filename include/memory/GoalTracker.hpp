#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace agent {

/**
 * Goal status enumeration
 */
enum class GoalStatus {
    NotStarted,     // Goal created but no progress
    InProgress,     // Actively working on it
    Blocked,        // Stuck on something
    Completed,      // Successfully finished
    Abandoned       // No longer pursuing
};

/**
 * Goal priority levels
 */
enum class GoalPriority {
    Low,
    Medium,
    High,
    Critical
};

/**
 * A trackable goal with progress tracking
 */
struct Goal {
    std::string id;                    // Unique identifier
    std::string title;                 // Short description
    std::string description;           // Detailed description
    GoalStatus status = GoalStatus::NotStarted;
    GoalPriority priority = GoalPriority::Medium;
    
    // Progress tracking
    double progress = 0.0;             // 0.0 - 1.0
    std::vector<std::string> milestones;  // Sub-tasks
    std::vector<std::string> completed_milestones;
    
    // Timestamps
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::optional<std::chrono::system_clock::time_point> deadline;
    std::optional<std::chrono::system_clock::time_point> completed_at;
    
    // Tracking
    int check_count = 0;               // How many times this goal was checked
    int days_stalled = 0;              // Days without progress
    
    // Serialization
    nlohmann::json to_json() const;
    static Goal from_json(const nlohmann::json& j);
    
    // Helpers
    std::string status_string() const;
    std::string priority_string() const;
    int days_since_created() const;
    int days_until_deadline() const;
    bool is_overdue() const;
    bool is_stalled(int threshold = 7) const;
};

/**
 * Goal check-in record
 */
struct GoalCheckIn {
    std::string goal_id;
    std::chrono::system_clock::time_point timestamp;
    double progress_before;
    double progress_after;
    std::string notes;
    std::vector<std::string> milestones_completed;
    
    nlohmann::json to_json() const;
    static GoalCheckIn from_json(const nlohmann::json& j);
};

/**
 * GoalTracker - Long-term goal tracking system
 * 
 * Features:
 * - Create, update, delete goals
 * - Track progress over time
 * - Detect stalled goals
 * - Generate reminders
 * - Persist to file
 */
class GoalTracker {
public:
    explicit GoalTracker(const std::string& data_path = "goals.json");
    
    // ===== Goal CRUD =====
    
    /// Create a new goal
    Goal create_goal(
        const std::string& title,
        const std::string& description = "",
        GoalPriority priority = GoalPriority::Medium,
        std::optional<std::chrono::system_clock::time_point> deadline = std::nullopt
    );
    
    /// Update an existing goal
    bool update_goal(const Goal& goal);
    
    /// Delete a goal
    bool delete_goal(const std::string& goal_id);
    
    /// Get a goal by ID
    std::optional<Goal> get_goal(const std::string& goal_id) const;
    
    /// Get all goals
    std::vector<Goal> get_all_goals() const;
    
    /// Get goals by status
    std::vector<Goal> get_goals_by_status(GoalStatus status) const;
    
    /// Get goals by priority
    std::vector<Goal> get_goals_by_priority(GoalPriority priority) const;
    
    // ===== Progress Tracking =====
    
    /// Record a check-in for a goal
    bool check_in(
        const std::string& goal_id,
        double progress,
        const std::string& notes = "",
        const std::vector<std::string>& completed_milestones = {}
    );
    
    /// Update goal status
    bool set_status(const std::string& goal_id, GoalStatus status);
    
    /// Add a milestone to a goal
    bool add_milestone(const std::string& goal_id, const std::string& milestone);
    
    /// Complete a milestone
    bool complete_milestone(const std::string& goal_id, const std::string& milestone);
    
    // ===== Analysis =====
    
    /// Get goals that are stalled (no progress for N days)
    std::vector<Goal> get_stalled_goals(int days_threshold = 7) const;
    
    /// Get goals that are overdue
    std::vector<Goal> get_overdue_goals() const;
    
    /// Get goals that need attention (stalled, overdue, or high priority)
    std::vector<Goal> get_goals_needing_attention() const;
    
    /// Generate a progress report
    std::string generate_report() const;
    
    /// Generate reminders for goals needing attention
    std::vector<std::string> generate_reminders() const;
    
    // ===== Persistence =====
    
    /// Save goals to file
    bool save() const;
    
    /// Load goals from file
    bool load();
    
    // ===== Statistics =====
    
    int total_goals() const;
    int completed_goals() const;
    int in_progress_goals() const;
    double average_progress() const;
    
private:
    std::string data_path_;
    mutable std::vector<Goal> goals_;  // mutable for lazy stalled status update
    std::vector<GoalCheckIn> check_ins_;
    
    std::string generate_id() const;
    void update_stalled_status() const;
};

} // namespace agent
