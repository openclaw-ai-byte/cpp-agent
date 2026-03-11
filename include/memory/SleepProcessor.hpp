#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <nlohmann/json.hpp>
#include "GoalTracker.hpp"
#include "SessionAnalyzer.hpp"

namespace agent {

/**
 * Sleep processing result
 */
struct SleepResult {
    bool success;
    std::string summary;
    
    // What was processed
    int patterns_detected;
    int goals_updated;
    int insights_generated;
    
    // Recommendations
    std::vector<std::string> recommendations;
    std::vector<std::string> reminders;
    
    // Predictions for next session
    std::vector<std::string> predicted_tasks;
    
    nlohmann::json to_json() const;
};

/**
 * Learning extracted from a session
 */
struct Learning {
    std::string id;
    std::string category;           // "decision", "mistake", "discovery", "preference"
    std::string content;
    std::string context;            // What led to this learning
    int importance;                 // 1-5 scale
    std::chrono::system_clock::time_point timestamp;
    
    nlohmann::json to_json() const;
    static Learning from_json(const nlohmann::json& j);
};

/**
 * Prediction for next session
 */
struct Prediction {
    std::string task;               // What might be asked
    double probability;             // Likelihood
    std::string reason;             // Why this prediction
    std::string preparation;        // How to prepare
    
    nlohmann::json to_json() const;
    static Prediction from_json(const nlohmann::json& j);
};

/**
 * SleepProcessor - "Sleep" processing for session-end consolidation
 * 
 * Analogous to human sleep where the brain consolidates memories.
 * Called when a session ends to:
 * - Compress learnings
 * - Update long-term memory
 * - Predict tomorrow's needs
 * - Generate wake-up report
 * 
 * Usage:
 *   SleepProcessor processor(goal_tracker, session_analyzer);
 *   auto result = processor.process(session_events);
 *   // result contains summary, recommendations, predictions
 */
class SleepProcessor {
public:
    using ProgressCallback = std::function<void(const std::string& stage, double progress)>;
    
    SleepProcessor(
        GoalTracker& goal_tracker,
        SessionAnalyzer& session_analyzer,
        const std::string& memory_path = "memory.json"
    );
    
    // ===== Main Processing =====
    
    /// Process session-end consolidation
    /// Call this when a session is about to end
    SleepResult process(const std::vector<SessionEvent>& events);
    
    /// Process with progress callback (for long-running operations)
    SleepResult process_with_callback(
        const std::vector<SessionEvent>& events,
        ProgressCallback callback
    );
    
    // ===== Processing Stages =====
    
    /// Stage 1: Extract learnings from events
    std::vector<Learning> extract_learnings(const std::vector<SessionEvent>& events);
    
    /// Stage 2: Compress learnings into compact form
    std::vector<Learning> compress_learnings(const std::vector<Learning>& learnings);
    
    /// Stage 3: Update long-term memory
    bool update_long_term_memory(const std::vector<Learning>& learnings);
    
    /// Stage 4: Check goal progress
    std::vector<std::string> check_goal_progress();
    
    /// Stage 5: Generate predictions for next session
    std::vector<Prediction> predict_next_session();
    
    /// Stage 6: Generate wake-up report
    std::string generate_wake_report(const SleepResult& result);
    
    // ===== Configuration =====
    
    void set_compression_threshold(int min_importance);
    void set_prediction_lookback_days(int days);
    void set_enable_predictions(bool enable);
    
    // ===== Persistence =====
    
    bool save_memory() const;
    bool load_memory();
    
    // ===== Accessors =====
    
    const std::vector<Learning>& get_long_term_memory() const;
    const std::vector<Prediction>& get_predictions() const;

private:
    GoalTracker& goal_tracker_;
    SessionAnalyzer& session_analyzer_;
    std::string memory_path_;
    
    std::vector<Learning> long_term_memory_;
    std::vector<Prediction> predictions_;
    
    // Configuration
    int compression_threshold_ = 2;      // Min importance to keep
    int prediction_lookback_days_ = 7;   // Days to look back for patterns
    bool enable_predictions_ = true;
    
    // Helpers
    std::string categorize_event(const SessionEvent& event);
    int calculate_importance(const SessionEvent& event);
    std::string summarize_events(const std::vector<SessionEvent>& events);
    std::string generate_id() const;
};

} // namespace agent
