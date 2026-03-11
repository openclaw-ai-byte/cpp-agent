#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <nlohmann/json.hpp>

namespace agent {

/**
 * Pattern types that can be detected
 */
enum class PatternType {
    TimeBased,      // "User asks about X at specific times"
    ErrorBased,     // "I made mistakes when doing X"
    PreferenceBased, // "User prefers X over Y"
    SequenceBased,  // "After X, user usually asks Y"
    FrequencyBased  // "Topic X comes up frequently"
};

/**
 * A detected pattern from session analysis
 */
struct Pattern {
    std::string id;
    PatternType type;
    std::string description;        // Human-readable description
    std::string trigger;            // What triggers this pattern
    std::string action;             // What typically happens
    double confidence;              // 0.0 - 1.0
    int occurrence_count;           // How many times observed
    std::chrono::system_clock::time_point first_seen;
    std::chrono::system_clock::time_point last_seen;
    
    // Context
    std::vector<std::string> examples;  // Example instances
    
    nlohmann::json to_json() const;
    static Pattern from_json(const nlohmann::json& j);
    
    std::string type_string() const;
};

/**
 * Session event for analysis
 */
struct SessionEvent {
    std::string id;
    std::string type;               // "user_message", "assistant_response", "tool_call", "error"
    std::string content;            // The actual content
    std::chrono::system_clock::time_point timestamp;
    
    // Context
    std::string session_id;
    std::unordered_map<std::string, std::string> metadata;
    
    nlohmann::json to_json() const;
    static SessionEvent from_json(const nlohmann::json& j);
};

/**
 * Session summary
 */
struct SessionSummary {
    std::string session_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    
    // Content analysis
    int message_count;
    int tool_call_count;
    int error_count;
    std::vector<std::string> topics;
    std::vector<std::string> tools_used;
    
    // Key learnings
    std::vector<std::string> decisions_made;
    std::vector<std::string> mistakes_made;
    std::vector<std::string> discoveries;
    
    // Compressed representation
    std::string summary_text;       // LLM-generated summary
    
    nlohmann::json to_json() const;
    static SessionSummary from_json(const nlohmann::json& j);
};

/**
 * SessionAnalyzer - Analyzes sessions to extract patterns and insights
 * 
 * Features:
 * - Record session events
 * - Detect patterns (time-based, error-based, preference-based)
 * - Generate session summaries
 * - Track topic frequency
 * - Identify improvement opportunities
 */
class SessionAnalyzer {
public:
    explicit SessionAnalyzer(const std::string& data_path = "sessions.json");
    
    // ===== Event Recording =====
    
    /// Record a session event
    void record_event(const SessionEvent& event);
    
    /// Start a new session
    std::string start_session();
    
    /// End current session
    SessionSummary end_session(const std::string& summary_text = "");
    
    /// Get current session ID
    std::string current_session_id() const;
    
    // ===== Pattern Detection =====
    
    /// Detect patterns from recorded events
    std::vector<Pattern> detect_patterns();
    
    /// Get all detected patterns
    std::vector<Pattern> get_patterns() const;
    
    /// Get patterns by type
    std::vector<Pattern> get_patterns_by_type(PatternType type) const;
    
    /// Get patterns matching a keyword
    std::vector<Pattern> search_patterns(const std::string& keyword) const;
    
    // ===== Session Analysis =====
    
    /// Get session summaries
    std::vector<SessionSummary> get_session_summaries() const;
    
    /// Get recent sessions (last N)
    std::vector<SessionSummary> get_recent_sessions(int count = 10) const;
    
    /// Analyze a specific session
    nlohmann::json analyze_session(const std::string& session_id) const;
    
    // ===== Insights Generation =====
    
    /// Generate insights from patterns
    std::vector<std::string> generate_insights() const;
    
    /// Get improvement suggestions
    std::vector<std::string> get_improvement_suggestions() const;
    
    /// Get frequent topics (topics that come up often)
    std::vector<std::pair<std::string, int>> get_frequent_topics(int min_count = 3) const;
    
    /// Get common errors
    std::vector<std::pair<std::string, int>> get_common_errors(int min_count = 2) const;
    
    // ===== Statistics =====
    
    int total_sessions() const;
    int total_events() const;
    std::chrono::hours total_session_time() const;
    
    // ===== Persistence =====
    
    bool save() const;
    bool load();
    
private:
    std::string data_path_;
    std::string current_session_id_;
    std::vector<SessionEvent> current_session_events_;
    
    std::vector<Pattern> patterns_;
    std::vector<SessionSummary> session_summaries_;
    std::unordered_map<std::string, int> topic_frequency_;
    std::unordered_map<std::string, int> error_frequency_;
    
    // Pattern detection helpers
    void detect_time_patterns();
    void detect_error_patterns();
    void detect_preference_patterns();
    void detect_sequence_patterns();
    void detect_frequency_patterns();
    
    std::string generate_id() const;
};

} // namespace agent
