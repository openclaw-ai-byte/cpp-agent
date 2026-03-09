#include "cron/CronManager.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <random>

namespace agent {

// ===== 时间工具函数 =====

std::string format_time(const std::tm& tm, const std::string& fmt) {
    std::ostringstream oss;
    oss << std::put_time(&tm, fmt.c_str());
    return oss.str();
}

std::tm parse_time(const std::string& str, const std::string& fmt) {
    std::tm tm = {};
    std::istringstream iss(str);
    iss >> std::get_time(&tm, fmt.c_str());
    return tm;
}

std::tm now_tm() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    return *std::localtime(&t);
}

// ===== CronExpr 实现 =====

CronExpr CronExpr::parse(const std::string& expr) {
    CronExpr result;
    std::istringstream iss(expr);
    std::string token;
    std::vector<std::string> parts;
    
    while (std::getline(iss, token, ' ')) {
        if (!token.empty()) parts.push_back(token);
    }
    
    if (parts.size() != 5) {
        spdlog::error("Invalid cron expression: {}", expr);
        return result;
    }
    
    auto parse_field = [](const std::string& s) -> int {
        if (s == "*") return -1;
        try {
            return std::stoi(s);
        } catch (...) {
            return -1;
        }
    };
    
    result.minute = parse_field(parts[0]);
    result.hour = parse_field(parts[1]);
    result.day = parse_field(parts[2]);
    result.month = parse_field(parts[3]);
    result.weekday = parse_field(parts[4]);
    
    return result;
}

bool CronExpr::matches(const std::tm& tm) const {
    if (minute >= 0 && minute != tm.tm_min) return false;
    if (hour >= 0 && hour != tm.tm_hour) return false;
    if (day >= 0 && day != tm.tm_mday) return false;
    if (month >= 0 && month != (tm.tm_mon + 1)) return false;
    if (weekday >= 0 && weekday != tm.tm_wday) return false;
    return true;
}

std::tm CronExpr::next_run(const std::tm& from) const {
    std::tm next = from;
    next.tm_sec = 0;
    next.tm_min++;
    
    // 向前推进直到找到匹配的时间
    // 简化实现：最多检查 366 天
    for (int i = 0; i < 366 * 24 * 60; ++i) {
        std::mktime(&next);  // 规范化
        if (matches(next)) {
            return next;
        }
        next.tm_min++;
    }
    
    return from;  // 未找到
}

// ===== CronTask 序列化 =====

nlohmann::json CronTask::to_json() const {
    return {
        {"id", id},
        {"name", name},
        {"cron_expr", cron_expr},
        {"command", command},
        {"enabled", enabled},
        {"last_run", last_run},
        {"next_run", next_run},
        {"run_count", run_count},
        {"metadata", metadata}
    };
}

CronTask CronTask::from_json(const nlohmann::json& j) {
    CronTask task;
    task.id = j.value("id", "");
    task.name = j.value("name", "");
    task.cron_expr = j.value("cron_expr", "");
    task.command = j.value("command", "");
    task.enabled = j.value("enabled", true);
    task.last_run = j.value("last_run", "");
    task.next_run = j.value("next_run", "");
    task.run_count = j.value("run_count", 0);
    task.metadata = j.value("metadata", nlohmann::json::object());
    return task;
}

// ===== CronManager::Impl =====

class CronManager::Impl {
public:
    std::string config_path_;
    std::vector<CronTask> tasks_;
    TaskCallback callback_;
    mutable std::mutex mutex_;
    std::thread thread_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    
    std::string generate_id() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(100000, 999999);
        return "cron_" + std::to_string(dis(gen));
    }
    
    void update_next_run(CronTask& task) {
        auto expr = CronExpr::parse(task.cron_expr);
        auto now = now_tm();
        auto next = expr.next_run(now);
        task.next_run = format_time(next);
    }
    
    bool save_internal() {
        nlohmann::json j;
        j["tasks"] = nlohmann::json::array();
        
        for (const auto& task : tasks_) {
            j["tasks"].push_back(task.to_json());
        }
        
        std::ofstream file(config_path_);
        if (!file.is_open()) {
            spdlog::error("Failed to open cron config file: {}", config_path_);
            return false;
        }
        
        file << j.dump(2);
        return true;
    }
    
    void run_loop() {
        while (running_) {
            // 执行 tick 逻辑
            std::vector<CronTask> to_run;
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto now = now_tm();
                std::string now_str = format_time(now);
                
                for (auto& task : tasks_) {
                    if (!task.enabled) continue;
                    
                    auto expr = CronExpr::parse(task.cron_expr);
                    if (expr.matches(now)) {
                        to_run.push_back(task);
                        task.last_run = now_str;
                        task.run_count++;
                        update_next_run(task);
                    }
                }
                
                if (!to_run.empty()) {
                    save_internal();
                }
            }
            
            // 执行回调（不持有锁）
            for (const auto& task : to_run) {
                spdlog::info("Executing cron task: {} ({})", task.name, task.id);
                try {
                    if (callback_) {
                        callback_(task);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("Cron task execution error: {}", e.what());
                }
            }
            
            // 每分钟检查一次
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(60), [this] { return !running_; });
        }
    }
};

// ===== CronManager 实现 =====

CronManager::CronManager(const std::string& config_path)
    : impl_(std::make_unique<Impl>()) {
    impl_->config_path_ = config_path;
    load();
}

CronManager::~CronManager() {
    stop();
}

std::string CronManager::add_task(const std::string& name,
                                   const std::string& cron_expr,
                                   const std::string& command,
                                   const nlohmann::json& metadata) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    
    CronTask task;
    task.id = impl_->generate_id();
    task.name = name;
    task.cron_expr = cron_expr;
    task.command = command;
    task.enabled = true;
    task.metadata = metadata;
    
    impl_->update_next_run(task);
    impl_->tasks_.push_back(task);
    
    impl_->save_internal();
    spdlog::info("Added cron task: {} ({})", name, task.id);
    
    return task.id;
}

bool CronManager::remove_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    
    auto it = std::find_if(impl_->tasks_.begin(), impl_->tasks_.end(),
        [&](const CronTask& t) { return t.id == task_id; });
    
    if (it == impl_->tasks_.end()) {
        return false;
    }
    
    impl_->tasks_.erase(it);
    impl_->save_internal();
    spdlog::info("Removed cron task: {}", task_id);
    
    return true;
}

bool CronManager::toggle_task(const std::string& task_id, bool enabled) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    
    for (auto& task : impl_->tasks_) {
        if (task.id == task_id) {
            task.enabled = enabled;
            impl_->save_internal();
            spdlog::info("{} cron task: {}", enabled ? "Enabled" : "Disabled", task_id);
            return true;
        }
    }
    
    return false;
}

std::vector<CronTask> CronManager::list_tasks() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->tasks_;
}

std::optional<CronTask> CronManager::get_task(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    
    for (const auto& task : impl_->tasks_) {
        if (task.id == task_id) {
            return task;
        }
    }
    
    return std::nullopt;
}

void CronManager::set_callback(TaskCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->callback_ = std::move(callback);
}

void CronManager::tick() {
    std::vector<CronTask> to_run;
    
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        auto now = now_tm();
        std::string now_str = format_time(now);
        
        for (auto& task : impl_->tasks_) {
            if (!task.enabled) continue;
            
            auto expr = CronExpr::parse(task.cron_expr);
            if (expr.matches(now)) {
                to_run.push_back(task);
                task.last_run = now_str;
                task.run_count++;
                impl_->update_next_run(task);
            }
        }
        
        if (!to_run.empty()) {
            impl_->save_internal();
        }
    }
    
    // 执行回调（不持有锁）
    for (const auto& task : to_run) {
        spdlog::info("Executing cron task: {} ({})", task.name, task.id);
        try {
            if (impl_->callback_) {
                impl_->callback_(task);
            }
        } catch (const std::exception& e) {
            spdlog::error("Cron task execution error: {}", e.what());
        }
    }
}

void CronManager::start() {
    if (impl_->running_) return;
    
    impl_->running_ = true;
    impl_->thread_ = std::thread(&Impl::run_loop, impl_.get());
    spdlog::info("CronManager started");
}

void CronManager::stop() {
    if (!impl_->running_) return;
    
    impl_->running_ = false;
    impl_->cv_.notify_all();
    
    if (impl_->thread_.joinable()) {
        impl_->thread_.join();
    }
    
    spdlog::info("CronManager stopped");
}

bool CronManager::is_running() const {
    return impl_->running_;
}

bool CronManager::save() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->save_internal();
}

bool CronManager::load() {
    std::ifstream file(impl_->config_path_);
    if (!file.is_open()) {
        spdlog::info("Cron config file not found, starting fresh: {}", impl_->config_path_);
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        impl_->tasks_.clear();
        for (const auto& task_json : j["tasks"]) {
            auto task = CronTask::from_json(task_json);
            impl_->update_next_run(task);
            impl_->tasks_.push_back(task);
        }
        
        spdlog::info("Loaded {} cron tasks", impl_->tasks_.size());
        return true;
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load cron config: {}", e.what());
        return false;
    }
}

} // namespace agent
