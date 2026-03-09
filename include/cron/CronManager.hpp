#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

namespace agent {

// Cron 表达式解析（简化版，支持: minute hour day month weekday）
// 例如: "0 8 * * *" = 每天 8:00
//       "30 9 * * 1-5" = 工作日 9:30
//       "0 */2 * * *" = 每 2 小时

struct CronTask {
    std::string id;              // 任务 ID
    std::string name;            // 任务名称
    std::string cron_expr;       // cron 表达式
    std::string command;         // 要执行的命令/提示词
    bool enabled = true;         // 是否启用
    std::string last_run;        // 上次运行时间
    std::string next_run;        // 下次运行时间
    int run_count = 0;           // 运行次数
    nlohmann::json metadata;     // 额外元数据
    
    nlohmann::json to_json() const;
    static CronTask from_json(const nlohmann::json& j);
};

struct CronExpr {
    int minute = -1;      // -1 表示任意值 (*)
    int hour = -1;
    int day = -1;
    int month = -1;
    int weekday = -1;     // 0=Sunday, 6=Saturday
    
    // 解析 cron 表达式
    static CronExpr parse(const std::string& expr);
    
    // 检查给定时间是否匹配
    bool matches(const std::tm& tm) const;
    
    // 计算下次运行时间
    std::tm next_run(const std::tm& from) const;
};

class CronManager {
public:
    using TaskCallback = std::function<void(const CronTask& task)>;
    
    explicit CronManager(const std::string& config_path = "cron_tasks.json");
    ~CronManager();
    
    // ===== 任务管理 =====
    
    /// 添加定时任务，返回任务 ID
    std::string add_task(const std::string& name, 
                         const std::string& cron_expr,
                         const std::string& command,
                         const nlohmann::json& metadata = {});
    
    /// 通过 ID 删除任务
    bool remove_task(const std::string& task_id);
    
    /// 启用/禁用任务
    bool toggle_task(const std::string& task_id, bool enabled);
    
    /// 获取所有任务
    std::vector<CronTask> list_tasks() const;
    
    /// 获取单个任务
    std::optional<CronTask> get_task(const std::string& task_id) const;
    
    // ===== 执行 =====
    
    /// 设置回调函数（任务触发时调用）
    void set_callback(TaskCallback callback);
    
    /// 检查并执行到期的任务（由外部定时器调用，如每分钟）
    void tick();
    
    /// 启动内部定时器（使用独立线程）
    void start();
    
    /// 停止内部定时器
    void stop();
    
    /// 检查是否运行中
    bool is_running() const;
    
    // ===== 持久化 =====
    
    /// 保存任务到文件
    bool save();
    
    /// 从文件加载任务
    bool load();
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// 工具函数：格式化时间
std::string format_time(const std::tm& tm, const std::string& fmt = "%Y-%m-%d %H:%M:%S");
std::tm parse_time(const std::string& str, const std::string& fmt = "%Y-%m-%d %H:%M:%S");
std::tm now_tm();

} // namespace agent
