#include "memory/SessionAnalyzer.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <regex>
#include <spdlog/spdlog.h>

namespace agent {

// ===== Pattern Implementation =====

nlohmann::json Pattern::to_json() const {
    auto to_time_t = [](const std::chrono::system_clock::time_point& tp) {
        return std::chrono::system_clock::to_time_t(tp);
    };
    
    nlohmann::json j;
    j["id"] = id;
    j["type"] = static_cast<int>(type);
    j["description"] = description;
    j["trigger"] = trigger;
    j["action"] = action;
    j["confidence"] = confidence;
    j["occurrence_count"] = occurrence_count;
    j["first_seen"] = to_time_t(first_seen);
    j["last_seen"] = to_time_t(last_seen);
    j["examples"] = examples;
    return j;
}

Pattern Pattern::from_json(const nlohmann::json& j) {
    auto from_time_t = [](time_t t) {
        return std::chrono::system_clock::from_time_t(t);
    };
    
    Pattern p;
    p.id = j.value("id", "");
    p.type = static_cast<PatternType>(j.value("type", 0));
    p.description = j.value("description", "");
    p.trigger = j.value("trigger", "");
    p.action = j.value("action", "");
    p.confidence = j.value("confidence", 0.0);
    p.occurrence_count = j.value("occurrence_count", 0);
    p.first_seen = from_time_t(j.value("first_seen", 0L));
    p.last_seen = from_time_t(j.value("last_seen", 0L));
    p.examples = j.value("examples", std::vector<std::string>{});
    return p;
}

std::string Pattern::type_string() const {
    switch (type) {
        case PatternType::TimeBased: return "time_based";
        case PatternType::ErrorBased: return "error_based";
        case PatternType::PreferenceBased: return "preference_based";
        case PatternType::SequenceBased: return "sequence_based";
        case PatternType::FrequencyBased: return "frequency_based";
        default: return "unknown";
    }
}

// ===== SessionEvent Implementation =====

nlohmann::json SessionEvent::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["type"] = type;
    j["content"] = content;
    j["timestamp"] = std::chrono::system_clock::to_time_t(timestamp);
    j["session_id"] = session_id;
    j["metadata"] = metadata;
    return j;
}

SessionEvent SessionEvent::from_json(const nlohmann::json& j) {
    SessionEvent e;
    e.id = j.value("id", "");
    e.type = j.value("type", "");
    e.content = j.value("content", "");
    e.timestamp = std::chrono::system_clock::from_time_t(j.value("timestamp", 0L));
    e.session_id = j.value("session_id", "");
    if (j.contains("metadata")) {
        for (auto it = j["metadata"].begin(); it != j["metadata"].end(); ++it) {
            e.metadata[it.key()] = it.value().get<std::string>();
        }
    }
    return e;
}

// ===== SessionSummary Implementation =====

nlohmann::json SessionSummary::to_json() const {
    auto to_time_t = [](const std::chrono::system_clock::time_point& tp) {
        return std::chrono::system_clock::to_time_t(tp);
    };
    
    nlohmann::json j;
    j["session_id"] = session_id;
    j["start_time"] = to_time_t(start_time);
    j["end_time"] = to_time_t(end_time);
    j["message_count"] = message_count;
    j["tool_call_count"] = tool_call_count;
    j["error_count"] = error_count;
    j["topics"] = topics;
    j["tools_used"] = tools_used;
    j["decisions_made"] = decisions_made;
    j["mistakes_made"] = mistakes_made;
    j["discoveries"] = discoveries;
    j["summary_text"] = summary_text;
    return j;
}

SessionSummary SessionSummary::from_json(const nlohmann::json& j) {
    auto from_time_t = [](time_t t) {
        return std::chrono::system_clock::from_time_t(t);
    };
    
    SessionSummary s;
    s.session_id = j.value("session_id", "");
    s.start_time = from_time_t(j.value("start_time", 0L));
    s.end_time = from_time_t(j.value("end_time", 0L));
    s.message_count = j.value("message_count", 0);
    s.tool_call_count = j.value("tool_call_count", 0);
    s.error_count = j.value("error_count", 0);
    s.topics = j.value("topics", std::vector<std::string>{});
    s.tools_used = j.value("tools_used", std::vector<std::string>{});
    s.decisions_made = j.value("decisions_made", std::vector<std::string>{});
    s.mistakes_made = j.value("mistakes_made", std::vector<std::string>{});
    s.discoveries = j.value("discoveries", std::vector<std::string>{});
    s.summary_text = j.value("summary_text", "");
    return s;
}

// ===== SessionAnalyzer Implementation =====

SessionAnalyzer::SessionAnalyzer(const std::string& data_path)
    : data_path_(data_path) {
    load();
}

std::string SessionAnalyzer::generate_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << "sess_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

std::string SessionAnalyzer::start_session() {
    current_session_id_ = generate_id();
    current_session_events_.clear();
    spdlog::info("Started session: {}", current_session_id_);
    return current_session_id_;
}

std::string SessionAnalyzer::current_session_id() const {
    return current_session_id_;
}

void SessionAnalyzer::record_event(const SessionEvent& event) {
    SessionEvent e = event;
    if (e.session_id.empty()) {
        e.session_id = current_session_id_;
    }
    if (e.id.empty()) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        e.id = "evt_" + std::to_string(dis(gen));
    }
    
    current_session_events_.push_back(e);
    
    // Track topic frequency
    if (e.type == "user_message" || e.type == "assistant_response") {
        // Simple keyword extraction (could be enhanced with NLP)
        std::regex word_regex("\\b[a-zA-Z]{4,}\\b");
        std::sregex_iterator it(e.content.begin(), e.content.end(), word_regex);
        std::sregex_iterator end;
        
        while (it != end) {
            std::string word = it->str();
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            topic_frequency_[word]++;
            ++it;
        }
    }
    
    // Track error frequency
    if (e.type == "error") {
        error_frequency_[e.content]++;
    }
    
    spdlog::debug("Recorded event: {} ({})", e.id, e.type);
}

SessionSummary SessionAnalyzer::end_session(const std::string& summary_text) {
    SessionSummary summary;
    summary.session_id = current_session_id_;
    
    if (!current_session_events_.empty()) {
        summary.start_time = current_session_events_.front().timestamp;
        summary.end_time = current_session_events_.back().timestamp;
    } else {
        auto now = std::chrono::system_clock::now();
        summary.start_time = now;
        summary.end_time = now;
    }
    
    // Count events
    summary.message_count = std::count_if(current_session_events_.begin(), 
        current_session_events_.end(), [](const SessionEvent& e) {
            return e.type == "user_message" || e.type == "assistant_response";
        });
    
    summary.tool_call_count = std::count_if(current_session_events_.begin(),
        current_session_events_.end(), [](const SessionEvent& e) {
            return e.type == "tool_call";
        });
    
    summary.error_count = std::count_if(current_session_events_.begin(),
        current_session_events_.end(), [](const SessionEvent& e) {
            return e.type == "error";
        });
    
    // Extract tools used
    for (const auto& e : current_session_events_) {
        if (e.type == "tool_call") {
            std::string tool_name = e.metadata.count("tool") ? e.metadata.at("tool") : "unknown";
            if (std::find(summary.tools_used.begin(), summary.tools_used.end(), tool_name) 
                == summary.tools_used.end()) {
                summary.tools_used.push_back(tool_name);
            }
        }
    }
    
    // Extract topics (top 5 most frequent words)
    std::vector<std::pair<std::string, int>> topics(topic_frequency_.begin(), 
        topic_frequency_.end());
    std::sort(topics.begin(), topics.end(), 
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    for (size_t i = 0; i < std::min(topics.size(), size_t(5)); ++i) {
        summary.topics.push_back(topics[i].first);
    }
    
    summary.summary_text = summary_text;
    
    session_summaries_.push_back(summary);
    
    // Detect patterns
    detect_patterns();
    
    save();
    
    spdlog::info("Ended session: {} ({} messages, {} tool calls, {} errors)",
        summary.session_id, summary.message_count, summary.tool_call_count, 
        summary.error_count);
    
    current_session_id_.clear();
    current_session_events_.clear();
    
    return summary;
}

std::vector<Pattern> SessionAnalyzer::detect_patterns() {
    // Run all pattern detection
    detect_time_patterns();
    detect_error_patterns();
    detect_preference_patterns();
    detect_sequence_patterns();
    detect_frequency_patterns();
    
    return patterns_;
}

void SessionAnalyzer::detect_time_patterns() {
    // Group events by hour of day
    std::unordered_map<int, std::vector<SessionEvent>> events_by_hour;
    
    for (const auto& event : current_session_events_) {
        auto time_t = std::chrono::system_clock::to_time_t(event.timestamp);
        std::tm* tm = std::localtime(&time_t);
        int hour = tm->tm_hour;
        events_by_hour[hour].push_back(event);
    }
    
    // Find hours with significant activity
    for (const auto& [hour, events] : events_by_hour) {
        if (events.size() >= 3) {
            // Check if this pattern already exists
            std::string trigger = "hour_" + std::to_string(hour);
            auto existing = std::find_if(patterns_.begin(), patterns_.end(),
                [&](const Pattern& p) { return p.trigger == trigger && 
                                              p.type == PatternType::TimeBased; });
            
            if (existing != patterns_.end()) {
                existing->occurrence_count++;
                existing->last_seen = std::chrono::system_clock::now();
                existing->confidence = std::min(1.0, existing->confidence + 0.1);
            } else {
                Pattern p;
                p.id = "pattern_" + std::to_string(patterns_.size());
                p.type = PatternType::TimeBased;
                p.trigger = trigger;
                p.description = "User active at hour " + std::to_string(hour);
                p.action = "Consider proactive check-ins at this time";
                p.confidence = 0.5;
                p.occurrence_count = 1;
                p.first_seen = std::chrono::system_clock::now();
                p.last_seen = p.first_seen;
                patterns_.push_back(p);
            }
        }
    }
}

void SessionAnalyzer::detect_error_patterns() {
    // Group similar errors
    for (const auto& [error, count] : error_frequency_) {
        if (count >= 2) {
            std::string trigger = "error:" + error.substr(0, 50);
            auto existing = std::find_if(patterns_.begin(), patterns_.end(),
                [&](const Pattern& p) { return p.trigger == trigger && 
                                              p.type == PatternType::ErrorBased; });
            
            if (existing != patterns_.end()) {
                existing->occurrence_count += count;
                existing->last_seen = std::chrono::system_clock::now();
            } else {
                Pattern p;
                p.id = "pattern_" + std::to_string(patterns_.size());
                p.type = PatternType::ErrorBased;
                p.trigger = trigger;
                p.description = "Recurring error: " + error.substr(0, 100);
                p.action = "Investigate and fix root cause";
                p.confidence = std::min(0.9, 0.5 + count * 0.1);
                p.occurrence_count = count;
                p.first_seen = std::chrono::system_clock::now();
                p.last_seen = p.first_seen;
                p.examples.push_back(error);
                patterns_.push_back(p);
            }
        }
    }
}

void SessionAnalyzer::detect_preference_patterns() {
    // Analyze user messages for preference indicators
    static const std::vector<std::pair<std::regex, std::string>> preference_patterns = {
        {std::regex("(?:i prefer|i like|i want|i need)\\s+(.+)"), "preference"},
        {std::regex("(?:don't|do not)\\s+(.+)"), "avoid"},
        {std::regex("(?:always|every time)\\s+(.+)"), "habit"},
    };
    
    for (const auto& event : current_session_events_) {
        if (event.type != "user_message") continue;
        
        for (const auto& [regex, pref_type] : preference_patterns) {
            std::smatch match;
            if (std::regex_search(event.content, match, regex)) {
                std::string preference = match[1].str();
                
                Pattern p;
                p.id = "pattern_" + std::to_string(patterns_.size());
                p.type = PatternType::PreferenceBased;
                p.trigger = pref_type + ":" + preference.substr(0, 30);
                p.description = "User " + pref_type + ": " + preference;
                p.action = "Remember this preference for future interactions";
                p.confidence = 0.6;
                p.occurrence_count = 1;
                p.first_seen = std::chrono::system_clock::now();
                p.last_seen = p.first_seen;
                p.examples.push_back(event.content);
                patterns_.push_back(p);
            }
        }
    }
}

void SessionAnalyzer::detect_sequence_patterns() {
    // Detect common sequences of actions
    if (current_session_events_.size() < 3) return;
    
    // Look for patterns like: user asks X -> tool Y -> response Z
    for (size_t i = 0; i + 2 < current_session_events_.size(); ++i) {
        const auto& e1 = current_session_events_[i];
        const auto& e2 = current_session_events_[i + 1];
        const auto& e3 = current_session_events_[i + 2];
        
        if (e1.type == "user_message" && e2.type == "tool_call" && 
            e3.type == "assistant_response") {
            
            std::string tool = e2.metadata.count("tool") ? e2.metadata.at("tool") : "unknown";
            std::string trigger = "seq:" + tool;
            
            auto existing = std::find_if(patterns_.begin(), patterns_.end(),
                [&](const Pattern& p) { return p.trigger == trigger && 
                                              p.type == PatternType::SequenceBased; });
            
            if (existing != patterns_.end()) {
                existing->occurrence_count++;
                existing->last_seen = std::chrono::system_clock::now();
            } else {
                Pattern p;
                p.id = "pattern_" + std::to_string(patterns_.size());
                p.type = PatternType::SequenceBased;
                p.trigger = trigger;
                p.description = "Tool '" + tool + "' commonly used for user queries";
                p.action = "Consider suggesting this tool for similar queries";
                p.confidence = 0.5;
                p.occurrence_count = 1;
                p.first_seen = std::chrono::system_clock::now();
                p.last_seen = p.first_seen;
                patterns_.push_back(p);
            }
        }
    }
}

void SessionAnalyzer::detect_frequency_patterns() {
    // Find frequently occurring topics
    for (const auto& [topic, count] : topic_frequency_) {
        if (count >= 5) {  // Appears at least 5 times
            std::string trigger = "topic:" + topic;
            
            auto existing = std::find_if(patterns_.begin(), patterns_.end(),
                [&](const Pattern& p) { return p.trigger == trigger && 
                                              p.type == PatternType::FrequencyBased; });
            
            if (existing != patterns_.end()) {
                existing->occurrence_count += count;
                existing->last_seen = std::chrono::system_clock::now();
            } else {
                Pattern p;
                p.id = "pattern_" + std::to_string(patterns_.size());
                p.type = PatternType::FrequencyBased;
                p.trigger = trigger;
                p.description = "Topic '" + topic + "' appears frequently";
                p.action = "Consider creating a shortcut or skill for this topic";
                p.confidence = std::min(0.8, 0.3 + count * 0.05);
                p.occurrence_count = count;
                p.first_seen = std::chrono::system_clock::now();
                p.last_seen = p.first_seen;
                patterns_.push_back(p);
            }
        }
    }
}

std::vector<Pattern> SessionAnalyzer::get_patterns() const {
    return patterns_;
}

std::vector<Pattern> SessionAnalyzer::get_patterns_by_type(PatternType type) const {
    std::vector<Pattern> result;
    std::copy_if(patterns_.begin(), patterns_.end(), std::back_inserter(result),
        [&](const Pattern& p) { return p.type == type; });
    return result;
}

std::vector<Pattern> SessionAnalyzer::search_patterns(const std::string& keyword) const {
    std::vector<Pattern> result;
    std::copy_if(patterns_.begin(), patterns_.end(), std::back_inserter(result),
        [&](const Pattern& p) { 
            return p.description.find(keyword) != std::string::npos ||
                   p.trigger.find(keyword) != std::string::npos;
        });
    return result;
}

std::vector<SessionSummary> SessionAnalyzer::get_session_summaries() const {
    return session_summaries_;
}

std::vector<SessionSummary> SessionAnalyzer::get_recent_sessions(int count) const {
    std::vector<SessionSummary> result;
    int start = std::max(0, (int)session_summaries_.size() - count);
    for (int i = start; i < (int)session_summaries_.size(); ++i) {
        result.push_back(session_summaries_[i]);
    }
    return result;
}

nlohmann::json SessionAnalyzer::analyze_session(const std::string& session_id) const {
    auto it = std::find_if(session_summaries_.begin(), session_summaries_.end(),
        [&](const SessionSummary& s) { return s.session_id == session_id; });
    
    if (it == session_summaries_.end()) {
        return {{"error", "Session not found"}};
    }
    
    nlohmann::json result = it->to_json();
    
    // Add analysis
    result["analysis"] = {
        {"efficiency", it->message_count > 0 ? 
            (double)it->tool_call_count / it->message_count : 0.0},
        {"error_rate", it->message_count > 0 ? 
            (double)it->error_count / it->message_count : 0.0},
        {"topics_count", it->topics.size()},
        {"tools_diversity", it->tools_used.size()}
    };
    
    return result;
}

std::vector<std::string> SessionAnalyzer::generate_insights() const {
    std::vector<std::string> insights;
    
    // High-confidence patterns
    for (const auto& p : patterns_) {
        if (p.confidence >= 0.7) {
            insights.push_back("🔍 " + p.description);
        }
    }
    
    // Common errors
    for (const auto& [error, count] : error_frequency_) {
        if (count >= 3) {
            insights.push_back("⚠️ Common error (" + std::to_string(count) + 
                              "x): " + error.substr(0, 50));
        }
    }
    
    // Frequent topics
    std::vector<std::pair<std::string, int>> topics(topic_frequency_.begin(), 
        topic_frequency_.end());
    std::sort(topics.begin(), topics.end(), 
        [](const auto& a, const auto& b) { return a.second > b.second; });
    
    for (size_t i = 0; i < std::min(topics.size(), size_t(3)); ++i) {
        if (topics[i].second >= 5) {
            insights.push_back("📊 Frequent topic: '" + topics[i].first + 
                              "' (" + std::to_string(topics[i].second) + " mentions)");
        }
    }
    
    return insights;
}

std::vector<std::string> SessionAnalyzer::get_improvement_suggestions() const {
    std::vector<std::string> suggestions;
    
    // Based on error patterns
    auto error_patterns = get_patterns_by_type(PatternType::ErrorBased);
    for (const auto& p : error_patterns) {
        suggestions.push_back("🔧 Fix recurring error: " + p.trigger);
    }
    
    // Based on frequency patterns
    auto freq_patterns = get_patterns_by_type(PatternType::FrequencyBased);
    for (const auto& p : freq_patterns) {
        suggestions.push_back("⚡ Create shortcut for frequent task: " + p.trigger);
    }
    
    // Based on session efficiency
    for (const auto& summary : session_summaries_) {
        if (summary.error_count > 2) {
            suggestions.push_back("📈 Session " + summary.session_id + 
                                " had high error rate, investigate workflow");
        }
    }
    
    return suggestions;
}

std::vector<std::pair<std::string, int>> SessionAnalyzer::get_frequent_topics(int min_count) const {
    std::vector<std::pair<std::string, int>> result;
    for (const auto& [topic, count] : topic_frequency_) {
        if (count >= min_count) {
            result.push_back({topic, count});
        }
    }
    std::sort(result.begin(), result.end(), 
        [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
}

std::vector<std::pair<std::string, int>> SessionAnalyzer::get_common_errors(int min_count) const {
    std::vector<std::pair<std::string, int>> result;
    for (const auto& [error, count] : error_frequency_) {
        if (count >= min_count) {
            result.push_back({error, count});
        }
    }
    std::sort(result.begin(), result.end(), 
        [](const auto& a, const auto& b) { return a.second > b.second; });
    return result;
}

int SessionAnalyzer::total_sessions() const {
    return session_summaries_.size();
}

int SessionAnalyzer::total_events() const {
    int total = 0;
    for (const auto& s : session_summaries_) {
        total += s.message_count + s.tool_call_count + s.error_count;
    }
    return total;
}

std::chrono::hours SessionAnalyzer::total_session_time() const {
    std::chrono::seconds total(0);
    for (const auto& s : session_summaries_) {
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            s.end_time - s.start_time);
        total += duration;
    }
    return std::chrono::duration_cast<std::chrono::hours>(total);
}

bool SessionAnalyzer::save() const {
    nlohmann::json j;
    
    j["patterns"] = nlohmann::json::array();
    for (const auto& p : patterns_) {
        j["patterns"].push_back(p.to_json());
    }
    
    j["session_summaries"] = nlohmann::json::array();
    for (const auto& s : session_summaries_) {
        j["session_summaries"].push_back(s.to_json());
    }
    
    j["topic_frequency"] = topic_frequency_;
    j["error_frequency"] = error_frequency_;
    
    std::ofstream file(data_path_);
    if (!file.is_open()) {
        spdlog::error("Failed to save session data to {}", data_path_);
        return false;
    }
    
    file << j.dump(2);
    spdlog::debug("Saved session data to {}", data_path_);
    return true;
}

bool SessionAnalyzer::load() {
    std::ifstream file(data_path_);
    if (!file.is_open()) {
        spdlog::info("No existing session data at {}, starting fresh", data_path_);
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        patterns_.clear();
        if (j.contains("patterns")) {
            for (const auto& pj : j["patterns"]) {
                patterns_.push_back(Pattern::from_json(pj));
            }
        }
        
        session_summaries_.clear();
        if (j.contains("session_summaries")) {
            for (const auto& sj : j["session_summaries"]) {
                session_summaries_.push_back(SessionSummary::from_json(sj));
            }
        }
        
        topic_frequency_.clear();
        if (j.contains("topic_frequency")) {
            for (auto it = j["topic_frequency"].begin(); it != j["topic_frequency"].end(); ++it) {
                topic_frequency_[it.key()] = it.value().get<int>();
            }
        }
        
        error_frequency_.clear();
        if (j.contains("error_frequency")) {
            for (auto it = j["error_frequency"].begin(); it != j["error_frequency"].end(); ++it) {
                error_frequency_[it.key()] = it.value().get<int>();
            }
        }
        
        spdlog::info("Loaded {} patterns, {} sessions from {}", 
            patterns_.size(), session_summaries_.size(), data_path_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse session data: {}", e.what());
        return false;
    }
}

} // namespace agent
