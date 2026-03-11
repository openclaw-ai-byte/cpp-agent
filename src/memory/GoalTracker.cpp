#include "memory/GoalTracker.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <spdlog/spdlog.h>

namespace agent {

// ===== Goal Implementation =====

nlohmann::json Goal::to_json() const {
    auto to_time_t = [](const std::chrono::system_clock::time_point& tp) {
        return std::chrono::system_clock::to_time_t(tp);
    };
    
    nlohmann::json j;
    j["id"] = id;
    j["title"] = title;
    j["description"] = description;
    j["status"] = static_cast<int>(status);
    j["priority"] = static_cast<int>(priority);
    j["progress"] = progress;
    j["milestones"] = milestones;
    j["completed_milestones"] = completed_milestones;
    j["created_at"] = to_time_t(created_at);
    j["updated_at"] = to_time_t(updated_at);
    if (deadline) j["deadline"] = to_time_t(*deadline);
    if (completed_at) j["completed_at"] = to_time_t(*completed_at);
    j["check_count"] = check_count;
    j["days_stalled"] = days_stalled;
    return j;
}

Goal Goal::from_json(const nlohmann::json& j) {
    auto from_time_t = [](time_t t) {
        return std::chrono::system_clock::from_time_t(t);
    };
    
    Goal g;
    g.id = j.value("id", "");
    g.title = j.value("title", "");
    g.description = j.value("description", "");
    g.status = static_cast<GoalStatus>(j.value("status", 0));
    g.priority = static_cast<GoalPriority>(j.value("priority", 1));
    g.progress = j.value("progress", 0.0);
    g.milestones = j.value("milestones", std::vector<std::string>{});
    g.completed_milestones = j.value("completed_milestones", std::vector<std::string>{});
    g.created_at = from_time_t(j.value("created_at", 0L));
    g.updated_at = from_time_t(j.value("updated_at", 0L));
    if (j.contains("deadline")) g.deadline = from_time_t(j["deadline"].get<time_t>());
    if (j.contains("completed_at")) g.completed_at = from_time_t(j["completed_at"].get<time_t>());
    g.check_count = j.value("check_count", 0);
    g.days_stalled = j.value("days_stalled", 0);
    return g;
}

std::string Goal::status_string() const {
    switch (status) {
        case GoalStatus::NotStarted: return "not_started";
        case GoalStatus::InProgress: return "in_progress";
        case GoalStatus::Blocked: return "blocked";
        case GoalStatus::Completed: return "completed";
        case GoalStatus::Abandoned: return "abandoned";
        default: return "unknown";
    }
}

std::string Goal::priority_string() const {
    switch (priority) {
        case GoalPriority::Low: return "low";
        case GoalPriority::Medium: return "medium";
        case GoalPriority::High: return "high";
        case GoalPriority::Critical: return "critical";
        default: return "unknown";
    }
}

int Goal::days_since_created() const {
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(now - created_at);
    return duration.count() / 24;
}

int Goal::days_until_deadline() const {
    if (!deadline) return -1;
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(*deadline - now);
    return duration.count() / 24;
}

bool Goal::is_overdue() const {
    if (!deadline || status == GoalStatus::Completed || status == GoalStatus::Abandoned) {
        return false;
    }
    return std::chrono::system_clock::now() > *deadline;
}

bool Goal::is_stalled(int threshold) const {
    return days_stalled >= threshold && 
           status != GoalStatus::Completed && 
           status != GoalStatus::Abandoned;
}

// ===== GoalCheckIn Implementation =====

nlohmann::json GoalCheckIn::to_json() const {
    nlohmann::json j;
    j["goal_id"] = goal_id;
    j["timestamp"] = std::chrono::system_clock::to_time_t(timestamp);
    j["progress_before"] = progress_before;
    j["progress_after"] = progress_after;
    j["notes"] = notes;
    j["milestones_completed"] = milestones_completed;
    return j;
}

GoalCheckIn GoalCheckIn::from_json(const nlohmann::json& j) {
    GoalCheckIn c;
    c.goal_id = j.value("goal_id", "");
    c.timestamp = std::chrono::system_clock::from_time_t(j.value("timestamp", 0L));
    c.progress_before = j.value("progress_before", 0.0);
    c.progress_after = j.value("progress_after", 0.0);
    c.notes = j.value("notes", "");
    c.milestones_completed = j.value("milestones_completed", std::vector<std::string>{});
    return c;
}

// ===== GoalTracker Implementation =====

GoalTracker::GoalTracker(const std::string& data_path)
    : data_path_(data_path) {
    load();
}

std::string GoalTracker::generate_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "goal_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

Goal GoalTracker::create_goal(
    const std::string& title,
    const std::string& description,
    GoalPriority priority,
    std::optional<std::chrono::system_clock::time_point> deadline
) {
    Goal goal;
    goal.id = generate_id();
    goal.title = title;
    goal.description = description;
    goal.priority = priority;
    goal.deadline = deadline;
    goal.created_at = std::chrono::system_clock::now();
    goal.updated_at = goal.created_at;
    
    goals_.push_back(goal);
    save();
    
    spdlog::info("Created goal: {} ({})", goal.title, goal.id);
    return goal;
}

bool GoalTracker::update_goal(const Goal& goal) {
    auto it = std::find_if(goals_.begin(), goals_.end(),
        [&](const Goal& g) { return g.id == goal.id; });
    
    if (it == goals_.end()) {
        spdlog::warn("Goal not found: {}", goal.id);
        return false;
    }
    
    it->title = goal.title;
    it->description = goal.description;
    it->status = goal.status;
    it->priority = goal.priority;
    it->progress = goal.progress;
    it->milestones = goal.milestones;
    it->completed_milestones = goal.completed_milestones;
    it->deadline = goal.deadline;
    it->updated_at = std::chrono::system_clock::now();
    
    if (goal.status == GoalStatus::Completed && !it->completed_at) {
        it->completed_at = std::chrono::system_clock::now();
        it->progress = 1.0;
    }
    
    save();
    spdlog::info("Updated goal: {}", goal.id);
    return true;
}

bool GoalTracker::delete_goal(const std::string& goal_id) {
    auto it = std::find_if(goals_.begin(), goals_.end(),
        [&](const Goal& g) { return g.id == goal_id; });
    
    if (it == goals_.end()) {
        return false;
    }
    
    goals_.erase(it);
    save();
    spdlog::info("Deleted goal: {}", goal_id);
    return true;
}

std::optional<Goal> GoalTracker::get_goal(const std::string& goal_id) const {
    auto it = std::find_if(goals_.begin(), goals_.end(),
        [&](const Goal& g) { return g.id == goal_id; });
    
    if (it == goals_.end()) {
        return std::nullopt;
    }
    return *it;
}

std::vector<Goal> GoalTracker::get_all_goals() const {
    return goals_;
}

std::vector<Goal> GoalTracker::get_goals_by_status(GoalStatus status) const {
    std::vector<Goal> result;
    std::copy_if(goals_.begin(), goals_.end(), std::back_inserter(result),
        [&](const Goal& g) { return g.status == status; });
    return result;
}

std::vector<Goal> GoalTracker::get_goals_by_priority(GoalPriority priority) const {
    std::vector<Goal> result;
    std::copy_if(goals_.begin(), goals_.end(), std::back_inserter(result),
        [&](const Goal& g) { return g.priority == priority; });
    return result;
}

bool GoalTracker::check_in(
    const std::string& goal_id,
    double progress,
    const std::string& notes,
    const std::vector<std::string>& completed_milestones
) {
    auto it = std::find_if(goals_.begin(), goals_.end(),
        [&](const Goal& g) { return g.id == goal_id; });
    
    if (it == goals_.end()) {
        return false;
    }
    
    GoalCheckIn checkin;
    checkin.goal_id = goal_id;
    checkin.timestamp = std::chrono::system_clock::now();
    checkin.progress_before = it->progress;
    checkin.progress_after = progress;
    checkin.notes = notes;
    checkin.milestones_completed = completed_milestones;
    
    check_ins_.push_back(checkin);
    
    // Update goal
    it->progress = progress;
    it->check_count++;
    it->updated_at = std::chrono::system_clock::now();
    
    // Add completed milestones
    for (const auto& m : completed_milestones) {
        if (std::find(it->completed_milestones.begin(), it->completed_milestones.end(), m) 
            == it->completed_milestones.end()) {
            it->completed_milestones.push_back(m);
        }
    }
    
    // Auto-update status
    if (progress >= 1.0) {
        it->status = GoalStatus::Completed;
        it->completed_at = std::chrono::system_clock::now();
    } else if (progress > 0 && it->status == GoalStatus::NotStarted) {
        it->status = GoalStatus::InProgress;
    }
    
    // Reset stalled counter if progress made
    if (progress > checkin.progress_before) {
        it->days_stalled = 0;
    }
    
    save();
    spdlog::info("Check-in for goal {}: {} -> {}", goal_id, checkin.progress_before, progress);
    return true;
}

bool GoalTracker::set_status(const std::string& goal_id, GoalStatus status) {
    auto it = std::find_if(goals_.begin(), goals_.end(),
        [&](const Goal& g) { return g.id == goal_id; });
    
    if (it == goals_.end()) {
        return false;
    }
    
    it->status = status;
    it->updated_at = std::chrono::system_clock::now();
    
    if (status == GoalStatus::Completed) {
        it->completed_at = std::chrono::system_clock::now();
        it->progress = 1.0;
    }
    
    save();
    return true;
}

bool GoalTracker::add_milestone(const std::string& goal_id, const std::string& milestone) {
    auto it = std::find_if(goals_.begin(), goals_.end(),
        [&](const Goal& g) { return g.id == goal_id; });
    
    if (it == goals_.end()) {
        return false;
    }
    
    it->milestones.push_back(milestone);
    it->updated_at = std::chrono::system_clock::now();
    save();
    return true;
}

bool GoalTracker::complete_milestone(const std::string& goal_id, const std::string& milestone) {
    auto it = std::find_if(goals_.begin(), goals_.end(),
        [&](const Goal& g) { return g.id == goal_id; });
    
    if (it == goals_.end()) {
        return false;
    }
    
    // Check if milestone exists
    auto mit = std::find(it->milestones.begin(), it->milestones.end(), milestone);
    if (mit == it->milestones.end()) {
        // Milestone doesn't exist, add it
        it->milestones.push_back(milestone);
    }
    
    // Add to completed if not already
    if (std::find(it->completed_milestones.begin(), it->completed_milestones.end(), milestone) 
        == it->completed_milestones.end()) {
        it->completed_milestones.push_back(milestone);
    }
    
    // Update progress based on milestones
    if (!it->milestones.empty()) {
        it->progress = static_cast<double>(it->completed_milestones.size()) / it->milestones.size();
    }
    
    it->updated_at = std::chrono::system_clock::now();
    save();
    return true;
}

void GoalTracker::update_stalled_status() const {
    auto now = std::chrono::system_clock::now();
    
    for (auto& goal : goals_) {
        if (goal.status == GoalStatus::Completed || goal.status == GoalStatus::Abandoned) {
            continue;
        }
        
        // Calculate days since last update
        auto duration = std::chrono::duration_cast<std::chrono::hours>(now - goal.updated_at);
        int days_since_update = duration.count() / 24;
        
        // Update stalled counter
        if (days_since_update > 0 && goal.progress < 1.0) {
            goal.days_stalled = days_since_update;
        }
    }
}

std::vector<Goal> GoalTracker::get_stalled_goals(int days_threshold) const {
    update_stalled_status();
    
    std::vector<Goal> result;
    std::copy_if(goals_.begin(), goals_.end(), std::back_inserter(result),
        [&](const Goal& g) { return g.is_stalled(days_threshold); });
    return result;
}

std::vector<Goal> GoalTracker::get_overdue_goals() const {
    std::vector<Goal> result;
    std::copy_if(goals_.begin(), goals_.end(), std::back_inserter(result),
        [&](const Goal& g) { return g.is_overdue(); });
    return result;
}

std::vector<Goal> GoalTracker::get_goals_needing_attention() const {
    update_stalled_status();
    
    std::vector<Goal> result;
    for (const auto& goal : goals_) {
        bool needs_attention = 
            goal.is_overdue() ||
            goal.is_stalled() ||
            goal.priority == GoalPriority::Critical ||
            (goal.priority == GoalPriority::High && goal.status == GoalStatus::InProgress);
        
        if (needs_attention && goal.status != GoalStatus::Completed && 
            goal.status != GoalStatus::Abandoned) {
            result.push_back(goal);
        }
    }
    return result;
}

std::string GoalTracker::generate_report() const {
    std::stringstream ss;
    
    ss << "=== Goal Tracker Report ===\n\n";
    
    // Summary
    ss << "📊 Summary:\n";
    ss << "  Total: " << goals_.size() << "\n";
    ss << "  Completed: " << completed_goals() << "\n";
    ss << "  In Progress: " << in_progress_goals() << "\n";
    ss << "  Average Progress: " << std::fixed << std::setprecision(0) 
       << (average_progress() * 100) << "%\n\n";
    
    // Goals needing attention
    auto attention = get_goals_needing_attention();
    if (!attention.empty()) {
        ss << "⚠️  Needs Attention (" << attention.size() << "):\n";
        for (const auto& g : attention) {
            ss << "  - " << g.title << " [" << g.status_string() << "]";
            if (g.is_overdue()) ss << " [OVERDUE]";
            if (g.is_stalled()) ss << " [STALLED " << g.days_stalled << "d]";
            ss << "\n";
        }
        ss << "\n";
    }
    
    // Active goals
    auto active = get_goals_by_status(GoalStatus::InProgress);
    if (!active.empty()) {
        ss << "🔄 In Progress (" << active.size() << "):\n";
        for (const auto& g : active) {
            ss << "  - " << g.title << " [" << std::setprecision(0) 
               << (g.progress * 100) << "%]";
            if (g.deadline) {
                int days = g.days_until_deadline();
                if (days >= 0) ss << " (due in " << days << "d)";
                else ss << " (overdue " << -days << "d)";
            }
            ss << "\n";
        }
        ss << "\n";
    }
    
    return ss.str();
}

std::vector<std::string> GoalTracker::generate_reminders() const {
    std::vector<std::string> reminders;
    
    auto attention = get_goals_needing_attention();
    for (const auto& goal : attention) {
        std::string reminder;
        
        if (goal.is_overdue()) {
            reminder = "⚠️ Goal overdue: \"" + goal.title + "\" (" + 
                      std::to_string(-goal.days_until_deadline()) + " days)";
        } else if (goal.is_stalled()) {
            reminder = "⏸️ Goal stalled: \"" + goal.title + "\" (" + 
                      std::to_string(goal.days_stalled) + " days without progress)";
        } else if (goal.priority == GoalPriority::Critical) {
            reminder = "🔥 Critical goal: \"" + goal.title + "\" needs attention";
        } else {
            reminder = "📌 High priority: \"" + goal.title + "\" in progress";
        }
        
        reminders.push_back(reminder);
    }
    
    return reminders;
}

bool GoalTracker::save() const {
    nlohmann::json j;
    j["goals"] = nlohmann::json::array();
    for (const auto& g : goals_) {
        j["goals"].push_back(g.to_json());
    }
    
    j["check_ins"] = nlohmann::json::array();
    for (const auto& c : check_ins_) {
        j["check_ins"].push_back(c.to_json());
    }
    
    std::ofstream file(data_path_);
    if (!file.is_open()) {
        spdlog::error("Failed to save goals to {}", data_path_);
        return false;
    }
    
    file << j.dump(2);
    spdlog::debug("Saved {} goals to {}", goals_.size(), data_path_);
    return true;
}

bool GoalTracker::load() {
    std::ifstream file(data_path_);
    if (!file.is_open()) {
        spdlog::info("No existing goals file at {}, starting fresh", data_path_);
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        goals_.clear();
        if (j.contains("goals")) {
            for (const auto& gj : j["goals"]) {
                goals_.push_back(Goal::from_json(gj));
            }
        }
        
        check_ins_.clear();
        if (j.contains("check_ins")) {
            for (const auto& cj : j["check_ins"]) {
                check_ins_.push_back(GoalCheckIn::from_json(cj));
            }
        }
        
        spdlog::info("Loaded {} goals from {}", goals_.size(), data_path_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse goals file: {}", e.what());
        return false;
    }
}

int GoalTracker::total_goals() const {
    return goals_.size();
}

int GoalTracker::completed_goals() const {
    return get_goals_by_status(GoalStatus::Completed).size();
}

int GoalTracker::in_progress_goals() const {
    return get_goals_by_status(GoalStatus::InProgress).size();
}

double GoalTracker::average_progress() const {
    if (goals_.empty()) return 0.0;
    
    double total = 0.0;
    for (const auto& g : goals_) {
        total += g.progress;
    }
    return total / goals_.size();
}

} // namespace agent
