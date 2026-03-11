#include "memory/SleepProcessor.hpp"
#include <fstream>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace agent {

// ===== SleepResult Implementation =====

nlohmann::json SleepResult::to_json() const {
    nlohmann::json j;
    j["success"] = success;
    j["summary"] = summary;
    j["patterns_detected"] = patterns_detected;
    j["goals_updated"] = goals_updated;
    j["insights_generated"] = insights_generated;
    j["recommendations"] = recommendations;
    j["reminders"] = reminders;
    j["predicted_tasks"] = predicted_tasks;
    return j;
}

// ===== Learning Implementation =====

nlohmann::json Learning::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["category"] = category;
    j["content"] = content;
    j["context"] = context;
    j["importance"] = importance;
    j["timestamp"] = std::chrono::system_clock::to_time_t(timestamp);
    return j;
}

Learning Learning::from_json(const nlohmann::json& j) {
    Learning l;
    l.id = j.value("id", "");
    l.category = j.value("category", "");
    l.content = j.value("content", "");
    l.context = j.value("context", "");
    l.importance = j.value("importance", 1);
    l.timestamp = std::chrono::system_clock::from_time_t(j.value("timestamp", 0L));
    return l;
}

// ===== Prediction Implementation =====

nlohmann::json Prediction::to_json() const {
    nlohmann::json j;
    j["task"] = task;
    j["probability"] = probability;
    j["reason"] = reason;
    j["preparation"] = preparation;
    return j;
}

Prediction Prediction::from_json(const nlohmann::json& j) {
    Prediction p;
    p.task = j.value("task", "");
    p.probability = j.value("probability", 0.0);
    p.reason = j.value("reason", "");
    p.preparation = j.value("preparation", "");
    return p;
}

// ===== SleepProcessor Implementation =====

SleepProcessor::SleepProcessor(
    GoalTracker& goal_tracker,
    SessionAnalyzer& session_analyzer,
    const std::string& memory_path
) : goal_tracker_(goal_tracker),
    session_analyzer_(session_analyzer),
    memory_path_(memory_path) {
    load_memory();
}

std::string SleepProcessor::generate_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    return "learn_" + std::to_string(dis(gen));
}

SleepResult SleepProcessor::process(const std::vector<SessionEvent>& events) {
    SleepResult result;
    result.success = true;
    
    spdlog::info("Starting sleep processing for {} events", events.size());
    
    // Stage 1: Extract learnings
    auto learnings = extract_learnings(events);
    spdlog::info("Extracted {} learnings", learnings.size());
    
    // Stage 2: Compress learnings
    auto compressed = compress_learnings(learnings);
    spdlog::info("Compressed to {} significant learnings", compressed.size());
    
    // Stage 3: Update long-term memory
    if (update_long_term_memory(compressed)) {
        result.goals_updated = compressed.size();
    }
    
    // Stage 4: Check goal progress
    auto goal_reminders = check_goal_progress();
    result.reminders = goal_reminders;
    result.goals_updated += goal_reminders.size();
    
    // Stage 5: Generate predictions
    if (enable_predictions_) {
        predictions_ = predict_next_session();
        for (const auto& pred : predictions_) {
            result.predicted_tasks.push_back(pred.task);
        }
    }
    
    // Stage 6: Generate summary
    result.patterns_detected = session_analyzer_.get_patterns().size();
    auto insights = session_analyzer_.generate_insights();
    result.insights_generated = insights.size();
    result.recommendations = session_analyzer_.get_improvement_suggestions();
    
    // Generate summary text
    std::stringstream ss;
    ss << "🧠 Sleep Processing Complete\n\n";
    ss << "📊 Session Summary:\n";
    ss << "  - Events processed: " << events.size() << "\n";
    ss << "  - Learnings extracted: " << learnings.size() << "\n";
    ss << "  - Significant learnings: " << compressed.size() << "\n";
    ss << "  - Patterns detected: " << result.patterns_detected << "\n\n";
    
    if (!compressed.empty()) {
        ss << "💡 Key Learnings:\n";
        for (const auto& l : compressed) {
            ss << "  - [" << l.category << "] " << l.content << "\n";
        }
        ss << "\n";
    }
    
    if (!result.reminders.empty()) {
        ss << "⏰ Reminders:\n";
        for (const auto& r : result.reminders) {
            ss << "  - " << r << "\n";
        }
        ss << "\n";
    }
    
    if (!result.predicted_tasks.empty()) {
        ss << "🔮 Predicted for next session:\n";
        for (const auto& t : result.predicted_tasks) {
            ss << "  - " << t << "\n";
        }
        ss << "\n";
    }
    
    result.summary = ss.str();
    
    save_memory();
    spdlog::info("Sleep processing complete");
    
    return result;
}

SleepResult SleepProcessor::process_with_callback(
    const std::vector<SessionEvent>& events,
    ProgressCallback callback
) {
    if (callback) callback("extracting", 0.0);
    auto learnings = extract_learnings(events);
    
    if (callback) callback("compressing", 0.2);
    auto compressed = compress_learnings(learnings);
    
    if (callback) callback("updating_memory", 0.4);
    update_long_term_memory(compressed);
    
    if (callback) callback("checking_goals", 0.6);
    auto reminders = check_goal_progress();
    
    if (callback) callback("predicting", 0.8);
    if (enable_predictions_) {
        predictions_ = predict_next_session();
    }
    
    if (callback) callback("complete", 1.0);
    
    return process(events);
}

std::vector<Learning> SleepProcessor::extract_learnings(const std::vector<SessionEvent>& events) {
    std::vector<Learning> learnings;
    
    for (const auto& event : events) {
        Learning l;
        l.id = generate_id();
        l.timestamp = event.timestamp;
        l.context = event.session_id;
        
        if (event.type == "error") {
            l.category = "mistake";
            l.content = event.content;
            l.importance = 3;
            learnings.push_back(l);
        }
        else if (event.type == "tool_call") {
            std::string tool = event.metadata.count("tool") ? event.metadata.at("tool") : "";
            std::string status = event.metadata.count("status") ? event.metadata.at("status") : "";
            
            if (status == "success") {
                l.category = "discovery";
                l.content = "Successfully used tool: " + tool;
                l.importance = 2;
                learnings.push_back(l);
            }
        }
        else if (event.type == "assistant_response") {
            // Look for decision indicators
            if (event.content.find("决定") != std::string::npos ||
                event.content.find("decided") != std::string::npos ||
                event.content.find("选择") != std::string::npos) {
                l.category = "decision";
                l.content = event.content.substr(0, 200);
                l.importance = 3;
                learnings.push_back(l);
            }
        }
        else if (event.type == "user_message") {
            // Look for preference indicators
            if (event.content.find("喜欢") != std::string::npos ||
                event.content.find("prefer") != std::string::npos ||
                event.content.find("always") != std::string::npos) {
                l.category = "preference";
                l.content = event.content.substr(0, 200);
                l.importance = 4;
                learnings.push_back(l);
            }
        }
    }
    
    return learnings;
}

std::vector<Learning> SleepProcessor::compress_learnings(const std::vector<Learning>& learnings) {
    // Filter by importance threshold
    std::vector<Learning> compressed;
    std::copy_if(learnings.begin(), learnings.end(), std::back_inserter(compressed),
        [&](const Learning& l) { return l.importance >= compression_threshold_; });
    
    // Deduplicate similar content
    std::vector<Learning> unique;
    for (const auto& l : compressed) {
        bool is_duplicate = false;
        for (const auto& u : unique) {
            if (l.category == u.category && 
                (l.content.find(u.content.substr(0, 50)) != std::string::npos ||
                 u.content.find(l.content.substr(0, 50)) != std::string::npos)) {
                is_duplicate = true;
                break;
            }
        }
        if (!is_duplicate) {
            unique.push_back(l);
        }
    }
    
    // Sort by importance (descending)
    std::sort(unique.begin(), unique.end(), 
        [](const Learning& a, const Learning& b) { return a.importance > b.importance; });
    
    // Limit to top learnings
    if (unique.size() > 20) {
        unique.resize(20);
    }
    
    return unique;
}

bool SleepProcessor::update_long_term_memory(const std::vector<Learning>& learnings) {
    // Add new learnings
    for (const auto& l : learnings) {
        // Check for duplicates in existing memory
        auto existing = std::find_if(long_term_memory_.begin(), long_term_memory_.end(),
            [&](const Learning& m) { 
                return m.category == l.category && m.content == l.content; 
            });
        
        if (existing == long_term_memory_.end()) {
            long_term_memory_.push_back(l);
        } else {
            // Update existing learning (increase importance if repeated)
            existing->importance = std::min(5, existing->importance + 1);
            existing->timestamp = std::chrono::system_clock::now();
        }
    }
    
    // Prune old, low-importance learnings
    auto now = std::chrono::system_clock::now();
    long_term_memory_.erase(
        std::remove_if(long_term_memory_.begin(), long_term_memory_.end(),
            [&](const Learning& l) {
                auto age = std::chrono::duration_cast<std::chrono::hours>(now - l.timestamp);
                return l.importance < 2 && age.count() > 168;  // 1 week
            }),
        long_term_memory_.end()
    );
    
    // Sort by importance
    std::sort(long_term_memory_.begin(), long_term_memory_.end(),
        [](const Learning& a, const Learning& b) { return a.importance > b.importance; });
    
    return true;
}

std::vector<std::string> SleepProcessor::check_goal_progress() {
    std::vector<std::string> reminders;
    
    // Get goals needing attention
    auto attention_goals = goal_tracker_.get_goals_needing_attention();
    
    for (const auto& goal : attention_goals) {
        std::string reminder;
        
        if (goal.is_overdue()) {
            reminder = "⚠️ Goal overdue: \"" + goal.title + "\" (overdue by " + 
                      std::to_string(-goal.days_until_deadline()) + " days)";
        } else if (goal.is_stalled()) {
            reminder = "⏸️ Goal stalled: \"" + goal.title + "\" (no progress for " + 
                      std::to_string(goal.days_stalled) + " days)";
        } else if (goal.priority == GoalPriority::Critical) {
            reminder = "🔥 Critical goal: \"" + goal.title + "\" needs immediate attention";
        } else if (goal.priority == GoalPriority::High) {
            reminder = "📌 High priority: \"" + goal.title + "\" - current progress: " + 
                      std::to_string(static_cast<int>(goal.progress * 100)) + "%";
        }
        
        if (!reminder.empty()) {
            reminders.push_back(reminder);
        }
    }
    
    return reminders;
}

std::vector<Prediction> SleepProcessor::predict_next_session() {
    std::vector<Prediction> predictions;
    
    // Get patterns from session analyzer
    auto patterns = session_analyzer_.get_patterns();
    
    // Get recent sessions
    auto recent_sessions = session_analyzer_.get_recent_sessions(prediction_lookback_days_);
    
    // Time-based predictions
    auto time_patterns = session_analyzer_.get_patterns_by_type(PatternType::TimeBased);
    for (const auto& p : time_patterns) {
        if (p.confidence >= 0.6) {
            Prediction pred;
            pred.task = "Check for tasks matching pattern: " + p.description;
            pred.probability = p.confidence;
            pred.reason = "Time pattern detected: " + p.trigger;
            pred.preparation = "Prepare for: " + p.action;
            predictions.push_back(pred);
        }
    }
    
    // Frequency-based predictions
    auto freq_patterns = session_analyzer_.get_patterns_by_type(PatternType::FrequencyBased);
    for (const auto& p : freq_patterns) {
        if (p.confidence >= 0.5 && p.occurrence_count >= 3) {
            Prediction pred;
            pred.task = "Likely topic: " + p.trigger.substr(6);  // Remove "topic:" prefix
            pred.probability = p.confidence;
            pred.reason = "Frequently discussed (" + std::to_string(p.occurrence_count) + " times)";
            pred.preparation = "Have relevant resources ready";
            predictions.push_back(pred);
        }
    }
    
    // Goal-based predictions
    auto active_goals = goal_tracker_.get_goals_by_status(GoalStatus::InProgress);
    for (const auto& goal : active_goals) {
        if (goal.days_stalled > 0) {
            Prediction pred;
            pred.task = "Resume work on: " + goal.title;
            pred.probability = 0.7;
            pred.reason = "Goal has been stalled for " + std::to_string(goal.days_stalled) + " days";
            pred.preparation = "Review goal context and next steps";
            predictions.push_back(pred);
        }
    }
    
    // Sort by probability
    std::sort(predictions.begin(), predictions.end(),
        [](const Prediction& a, const Prediction& b) { return a.probability > b.probability; });
    
    // Limit predictions
    if (predictions.size() > 5) {
        predictions.resize(5);
    }
    
    return predictions;
}

std::string SleepProcessor::generate_wake_report(const SleepResult& result) {
    std::stringstream ss;
    
    ss << "🌅 Wake Report\n";
    ss << "═══════════════════════════════════════\n\n";
    
    // Summary
    ss << "📊 Processing Summary:\n";
    ss << "  Patterns detected: " << result.patterns_detected << "\n";
    ss << "  Learnings stored: " << result.goals_updated << "\n";
    ss << "  Insights generated: " << result.insights_generated << "\n\n";
    
    // Reminders
    if (!result.reminders.empty()) {
        ss << "⏰ Reminders:\n";
        for (const auto& r : result.reminders) {
            ss << "  " << r << "\n";
        }
        ss << "\n";
    }
    
    // Predictions
    if (!result.predicted_tasks.empty()) {
        ss << "🔮 Predicted for today:\n";
        for (const auto& t : result.predicted_tasks) {
            ss << "  • " << t << "\n";
        }
        ss << "\n";
    }
    
    // Recommendations
    if (!result.recommendations.empty()) {
        ss << "💡 Recommendations:\n";
        for (const auto& r : result.recommendations) {
            ss << "  " << r << "\n";
        }
        ss << "\n";
    }
    
    ss << "═══════════════════════════════════════\n";
    ss << "Ready to start the day! 🚀\n";
    
    return ss.str();
}

void SleepProcessor::set_compression_threshold(int min_importance) {
    compression_threshold_ = min_importance;
}

void SleepProcessor::set_prediction_lookback_days(int days) {
    prediction_lookback_days_ = days;
}

void SleepProcessor::set_enable_predictions(bool enable) {
    enable_predictions_ = enable;
}

bool SleepProcessor::save_memory() const {
    nlohmann::json j;
    
    j["learnings"] = nlohmann::json::array();
    for (const auto& l : long_term_memory_) {
        j["learnings"].push_back(l.to_json());
    }
    
    j["predictions"] = nlohmann::json::array();
    for (const auto& p : predictions_) {
        j["predictions"].push_back(p.to_json());
    }
    
    j["config"] = {
        {"compression_threshold", compression_threshold_},
        {"prediction_lookback_days", prediction_lookback_days_},
        {"enable_predictions", enable_predictions_}
    };
    
    std::ofstream file(memory_path_);
    if (!file.is_open()) {
        spdlog::error("Failed to save memory to {}", memory_path_);
        return false;
    }
    
    file << j.dump(2);
    spdlog::debug("Saved {} learnings to {}", long_term_memory_.size(), memory_path_);
    return true;
}

bool SleepProcessor::load_memory() {
    std::ifstream file(memory_path_);
    if (!file.is_open()) {
        spdlog::info("No existing memory at {}, starting fresh", memory_path_);
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        long_term_memory_.clear();
        if (j.contains("learnings")) {
            for (const auto& lj : j["learnings"]) {
                long_term_memory_.push_back(Learning::from_json(lj));
            }
        }
        
        predictions_.clear();
        if (j.contains("predictions")) {
            for (const auto& pj : j["predictions"]) {
                predictions_.push_back(Prediction::from_json(pj));
            }
        }
        
        if (j.contains("config")) {
            compression_threshold_ = j["config"].value("compression_threshold", 2);
            prediction_lookback_days_ = j["config"].value("prediction_lookback_days", 7);
            enable_predictions_ = j["config"].value("enable_predictions", true);
        }
        
        spdlog::info("Loaded {} learnings from {}", long_term_memory_.size(), memory_path_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse memory file: {}", e.what());
        return false;
    }
}

const std::vector<Learning>& SleepProcessor::get_long_term_memory() const {
    return long_term_memory_;
}

const std::vector<Prediction>& SleepProcessor::get_predictions() const {
    return predictions_;
}

} // namespace agent
