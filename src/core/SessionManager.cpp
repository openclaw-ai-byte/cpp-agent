#include "core/SessionManager.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <random>

namespace fs = std::filesystem;

namespace agent {

SessionManager::SessionManager(const std::string& data_dir)
    : data_dir_(data_dir)
    , index_file_(data_dir + "/index.json")
{
    ensure_data_dir();
}

void SessionManager::ensure_data_dir() const {
    try {
        if (!fs::exists(data_dir_)) {
            fs::create_directories(data_dir_);
            spdlog::info("Created session data directory: {}", data_dir_);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to create data directory: {}", e.what());
    }
}

std::string SessionManager::generate_id() const {
    // Generate a simple ID based on timestamp + random
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::ostringstream oss;
    oss << "sess_" << ms << "_" << dis(gen);
    return oss.str();
}

std::string SessionManager::current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

nlohmann::json SessionManager::load_index() const {
    try {
        if (fs::exists(index_file_)) {
            std::ifstream file(index_file_);
            nlohmann::json index;
            file >> index;
            return index;
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to load session index: {}", e.what());
    }
    return nlohmann::json::object();
}

void SessionManager::save_index(const nlohmann::json& index) {
    try {
        std::ofstream file(index_file_);
        file << index.dump(2);
    } catch (const std::exception& e) {
        spdlog::error("Failed to save session index: {}", e.what());
    }
}

std::vector<SessionMeta> SessionManager::list_sessions() const {
    std::vector<SessionMeta> sessions;
    
    nlohmann::json index = load_index();
    for (auto& [id, meta] : index.items()) {
        sessions.push_back(SessionMeta::from_json(meta));
    }
    
    // Sort by updated_at descending (most recent first)
    std::sort(sessions.begin(), sessions.end(), [](const SessionMeta& a, const SessionMeta& b) {
        return a.updated_at > b.updated_at;
    });
    
    return sessions;
}

std::optional<Session> SessionManager::get_session(const std::string& id) const {
    std::string session_file = data_dir_ + "/" + id + ".json";
    
    try {
        if (!fs::exists(session_file)) {
            return std::nullopt;
        }
        
        std::ifstream file(session_file);
        nlohmann::json data;
        file >> data;
        
        return Session::from_json(data);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load session {}: {}", id, e.what());
        return std::nullopt;
    }
}

std::string SessionManager::save_session(const std::string& name,
                                          const std::string& system_prompt,
                                          const nlohmann::json& messages) {
    std::string id = generate_id();
    std::string now = current_timestamp();
    
    Session session;
    session.meta.id = id;
    session.meta.name = name.empty() ? "Session " + now.substr(0, 10) : name;
    session.meta.created_at = now;
    session.meta.updated_at = now;
    session.meta.message_count = messages.is_array() ? messages.size() : 0;
    session.system_prompt = system_prompt;
    session.messages = messages;
    
    // Save session file
    std::string session_file = data_dir_ + "/" + id + ".json";
    try {
        std::ofstream file(session_file);
        file << session.to_json().dump(2);
        spdlog::info("Saved session to {}", session_file);
    } catch (const std::exception& e) {
        spdlog::error("Failed to save session file: {}", e.what());
        return "";
    }
    
    // Update index
    nlohmann::json index = load_index();
    index[id] = session.meta.to_json();
    save_index(index);
    
    return id;
}

bool SessionManager::update_session(const std::string& id,
                                     const std::string& name,
                                     const std::string& system_prompt,
                                     const nlohmann::json& messages) {
    // Check if session exists
    auto existing = get_session(id);
    if (!existing) {
        return false;
    }
    
    std::string now = current_timestamp();
    
    Session session;
    session.meta.id = id;
    session.meta.name = name.empty() ? existing->meta.name : name;
    session.meta.created_at = existing->meta.created_at;
    session.meta.updated_at = now;
    session.meta.message_count = messages.is_array() ? messages.size() : 0;
    session.system_prompt = system_prompt;
    session.messages = messages;
    
    // Save session file
    std::string session_file = data_dir_ + "/" + id + ".json";
    try {
        std::ofstream file(session_file);
        file << session.to_json().dump(2);
    } catch (const std::exception& e) {
        spdlog::error("Failed to update session file: {}", e.what());
        return false;
    }
    
    // Update index
    nlohmann::json index = load_index();
    index[id] = session.meta.to_json();
    save_index(index);
    
    return true;
}

bool SessionManager::delete_session(const std::string& id) {
    std::string session_file = data_dir_ + "/" + id + ".json";
    
    try {
        if (fs::exists(session_file)) {
            fs::remove(session_file);
        }
        
        // Remove from index
        nlohmann::json index = load_index();
        index.erase(id);
        save_index(index);
        
        spdlog::info("Deleted session {}", id);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to delete session {}: {}", id, e.what());
        return false;
    }
}

size_t SessionManager::count() const {
    nlohmann::json index = load_index();
    return index.size();
}

} // namespace agent
