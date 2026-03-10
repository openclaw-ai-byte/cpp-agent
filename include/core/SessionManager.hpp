#pragma once

#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace agent {

// Session metadata
struct SessionMeta {
    std::string id;
    std::string name;
    std::string created_at;
    std::string updated_at;
    int message_count;
    
    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"name", name},
            {"created_at", created_at},
            {"updated_at", updated_at},
            {"message_count", message_count}
        };
    }
    
    static SessionMeta from_json(const nlohmann::json& j) {
        SessionMeta meta;
        meta.id = j.value("id", "");
        meta.name = j.value("name", "");
        meta.created_at = j.value("created_at", "");
        meta.updated_at = j.value("updated_at", "");
        meta.message_count = j.value("message_count", 0);
        return meta;
    }
};

// Full session with messages
struct Session {
    SessionMeta meta;
    std::string system_prompt;
    nlohmann::json messages;  // Array of messages
    
    nlohmann::json to_json() const {
        return {
            {"id", meta.id},
            {"name", meta.name},
            {"created_at", meta.created_at},
            {"updated_at", meta.updated_at},
            {"message_count", meta.message_count},
            {"system_prompt", system_prompt},
            {"messages", messages}
        };
    }
    
    static Session from_json(const nlohmann::json& j) {
        Session sess;
        sess.meta.id = j.value("id", "");
        sess.meta.name = j.value("name", "");
        sess.meta.created_at = j.value("created_at", "");
        sess.meta.updated_at = j.value("updated_at", "");
        sess.meta.message_count = j.value("message_count", 0);
        sess.system_prompt = j.value("system_prompt", "");
        sess.messages = j.value("messages", nlohmann::json::array());
        return sess;
    }
};

// Session manager - handles persistence to disk
class SessionManager {
public:
    explicit SessionManager(const std::string& data_dir = "data/sessions");
    
    // List all sessions (metadata only)
    std::vector<SessionMeta> list_sessions() const;
    
    // Get session by ID (full session with messages)
    std::optional<Session> get_session(const std::string& id) const;
    
    // Save a new session, returns session ID
    std::string save_session(const std::string& name, 
                             const std::string& system_prompt,
                             const nlohmann::json& messages);
    
    // Update existing session
    bool update_session(const std::string& id,
                        const std::string& name,
                        const std::string& system_prompt,
                        const nlohmann::json& messages);
    
    // Delete session
    bool delete_session(const std::string& id);
    
    // Get session count
    size_t count() const;
    
    // Get data directory
    std::string get_data_dir() const { return data_dir_; }
    
private:
    std::string data_dir_;
    std::string index_file_;
    
    // Load/save index file
    nlohmann::json load_index() const;
    void save_index(const nlohmann::json& index);
    
    // Generate unique ID
    std::string generate_id() const;
    
    // Get current timestamp
    std::string current_timestamp() const;
    
    // Ensure data directory exists
    void ensure_data_dir() const;
};

} // namespace agent
