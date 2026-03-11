#pragma once

#include <string>
#include <memory>

namespace agent {

class GoalTracker;
class SessionAnalyzer;
class SleepProcessor;

/// Initialize the self-evolution system
/// Call this after ToolRegistry is initialized
void init_self_evolution(const std::string& data_dir = ".");

/// Get the global goal tracker instance
std::shared_ptr<GoalTracker> get_goal_tracker();

/// Get the global session analyzer instance
std::shared_ptr<SessionAnalyzer> get_session_analyzer();

/// Get the global sleep processor instance
std::shared_ptr<SleepProcessor> get_sleep_processor();

} // namespace agent
