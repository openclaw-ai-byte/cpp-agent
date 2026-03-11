#include "core/Agent.hpp"
#include "core/SessionManager.hpp"
#include "cron/CronManager.hpp"
#include "tools/ToolRegistry.hpp"
#include "skills/SkillRegistry.hpp"
#include "memory/SelfEvolution.hpp"
#include "memory/GoalTracker.hpp"
#include "memory/SessionAnalyzer.hpp"
#include "memory/SleepProcessor.hpp"
#include "http/httplib.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <memory>
#include <boost/asio.hpp>

namespace asio = boost::asio;

namespace agent { 
    void register_builtin_tools(); 
    void set_cron_manager(class CronManager* mgr);
}

void print_banner() {
    std::cout << R"(
  _____ _____ _____    _   
 / ____|  __ \_   _|  | |  
| |    | |  | || |  __| |_ 
| |    | |  | || | / _` __|
| |____| |__| || || (_| |  
 \_____|_____/ |_| \__,_|  

    AI Agent Framework v0.3.0
    C++23 + ASIO + Cron
)" << std::endl;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --config <file>  Config file (default: config.json)\n"
              << "  --cron <file>    Cron tasks file (default: cron_tasks.json)\n"
              << "  --server         HTTP server mode\n"
              << "  --port <n>       Server port (default: 8080)\n"
              << "  --threads <n>    Thread pool size (default: 4)\n"
              << "  --help           Show help\n";
}

void run_cli(std::shared_ptr<agent::Agent> agent, std::shared_ptr<agent::CronManager> cron) {
    std::cout << "\nReady. Commands: exit, help, tools, cron, clear\n\n";
    std::string input;
    while (true) {
        std::cout << "You: ";
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;
        if (input == "exit" || input == "quit") break;
        if (input == "help") { 
            std::cout << "\nCommands:\n"
                      << "  exit    - Exit the program\n"
                      << "  tools   - List available tools\n"
                      << "  cron    - List cron tasks\n"
                      << "  clear   - Clear conversation\n\n"; 
            continue; 
        }
        if (input == "tools") {
            for (auto& t : agent->list_tools()) std::cout << "  - " << t << "\n";
            std::cout << "\n"; continue;
        }
        if (input == "cron") {
            auto tasks = cron->list_tasks();
            std::cout << "\nCron Tasks (" << tasks.size() << "):\n";
            for (const auto& t : tasks) {
                std::cout << "  [" << (t.enabled ? "ON" : "OFF") << "] " 
                          << t.id << " - " << t.name << "\n"
                          << "       " << t.cron_expr << " - " << t.command << "\n"
                          << "       Next: " << t.next_run << " (runs: " << t.run_count << ")\n";
            }
            std::cout << "\n"; continue;
        }
        if (input == "clear") { agent->clear_conversation(); std::cout << "Cleared.\n\n"; continue; }
        try {
            std::cout << "\nThinking...\n";
            auto r = agent->chat_sync(input);
            std::cout << "\nAI: " << r << "\n\n";
        } catch (const std::exception& e) {
            std::cout << "\nError: " << e.what() << "\n\n";
        }
    }
}

void run_server(std::shared_ptr<agent::Agent> agent, std::shared_ptr<agent::CronManager> cron, int port) {
    httplib::Server svr;
    svr.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS"},
                             {"Access-Control-Allow-Headers", "Content-Type"}});
    svr.Options(R"(.*)", [](auto&, auto& res) { res.status = 204; });
    
    // ===== Chat API =====
    svr.Post("/api/chat", [agent](auto& req, auto& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string msg = body.value("message", "");
            if (msg.empty()) { res.status = 400; res.set_content(R"({"error":"message required"})","application/json"); return; }
            if (body.count("system_prompt")) agent->set_system_prompt(body["system_prompt"]);
            auto r = agent->chat_sync(msg);
            res.set_content(nlohmann::json{{"response", r}, {"success", true}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Delete("/api/conversation", [agent](auto&, auto& res) {
        agent->clear_conversation();
        res.set_content(R"({"success":true})", "application/json");
    });
    
    svr.Get("/api/conversation", [agent](auto&, auto& res) {
        nlohmann::json arr = nlohmann::json::array();
        for (auto& m : agent->get_conversation_history()) arr.push_back(m.to_json());
        res.set_content(nlohmann::json{{"messages", arr}}.dump(), "application/json");
    });
    
    // ===== Tools API =====
    svr.Get("/api/tools", [](auto&, auto& res) {
        auto tools = agent::ToolRegistry::instance().list_tools();
        res.set_content(nlohmann::json{{"tools", tools}, {"count", tools.size()}}.dump(), "application/json");
    });
    
    svr.Post("/api/tools/([^/]+)/execute", [](auto& req, auto& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            auto r = agent::ToolRegistry::instance().execute_tool(req.matches[1], body);
            nlohmann::json j{{"success", r.success}, {"output", r.output}};
            if (!r.data.is_null()) j["data"] = r.data;
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    // ===== Skills API =====
    svr.Get("/api/skills", [](auto&, auto& res) {
        auto skills = agent::SkillRegistry::instance().list_skills();
        res.set_content(nlohmann::json{{"skills", skills}}.dump(), "application/json");
    });
    
    // ===== Cron API =====
    svr.Get("/api/cron", [cron](auto&, auto& res) {
        auto tasks = cron->list_tasks();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : tasks) arr.push_back(t.to_json());
        res.set_content(nlohmann::json{{"tasks", arr}, {"count", tasks.size()}, {"running", cron->is_running()}}.dump(), "application/json");
    });
    
    svr.Post("/api/cron", [cron](auto& req, auto& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string name = body.value("name", "Unnamed Task");
            std::string cron_expr = body.value("cron", "");
            std::string command = body.value("command", "");
            
            if (cron_expr.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"cron expression required"})","application/json");
                return;
            }
            
            auto metadata = body.value("metadata", nlohmann::json::object());
            std::string id = cron->add_task(name, cron_expr, command, metadata);
            
            res.set_content(nlohmann::json{{"success", true}, {"id", id}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Get("/api/cron/([^/]+)", [cron](auto& req, auto& res) {
        auto task = cron->get_task(req.matches[1]);
        if (!task) {
            res.status = 404;
            res.set_content(R"({"error":"task not found"})","application/json");
            return;
        }
        res.set_content(task->to_json().dump(), "application/json");
    });
    
    svr.Delete("/api/cron/([^/]+)", [cron](auto& req, auto& res) {
        bool removed = cron->remove_task(req.matches[1]);
        res.set_content(nlohmann::json{{"success", removed}}.dump(), "application/json");
    });
    
    svr.Post("/api/cron/([^/]+)/toggle", [cron](auto& req, auto& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            bool enabled = body.value("enabled", true);
            bool toggled = cron->toggle_task(req.matches[1], enabled);
            res.set_content(nlohmann::json{{"success", toggled}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Post("/api/cron/start", [cron](auto&, auto& res) {
        cron->start();
        res.set_content(nlohmann::json{{"running", cron->is_running()}}.dump(), "application/json");
    });
    
    svr.Post("/api/cron/stop", [cron](auto&, auto& res) {
        cron->stop();
        res.set_content(nlohmann::json{{"running", cron->is_running()}}.dump(), "application/json");
    });
    
    // ===== Health =====
    svr.Get("/api/health", [](auto&, auto& res) { 
        res.set_content(R"({"status":"ok"})", "application/json"); 
    });
    
    
    // ===== Self-Evolution API =====
    
    // Goals
    svr.Get("/api/goals", [](auto&, auto& res) {
        auto tracker = agent::get_goal_tracker();
        if (!tracker) {
            res.status = 500;
            res.set_content(R"({"error":"goal tracker not initialized"})", "application/json");
            return;
        }
        auto goals = tracker->get_all_goals();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& g : goals) arr.push_back(g.to_json());
        res.set_content(nlohmann::json{{"goals", arr}, {"count", goals.size()}}.dump(), "application/json");
    });
    
    svr.Post("/api/goals", [](auto& req, auto& res) {
        try {
            auto tracker = agent::get_goal_tracker();
            if (!tracker) {
                res.status = 500;
                res.set_content(R"({"error":"goal tracker not initialized"})", "application/json");
                return;
            }
            auto body = nlohmann::json::parse(req.body);
            std::string title = body.value("title", "");
            if (title.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"title required"})", "application/json");
                return;
            }
            std::string desc = body.value("description", "");
            std::string priority_str = body.value("priority", "medium");
            agent::GoalPriority priority = agent::GoalPriority::Medium;
            if (priority_str == "low") priority = agent::GoalPriority::Low;
            else if (priority_str == "high") priority = agent::GoalPriority::High;
            else if (priority_str == "critical") priority = agent::GoalPriority::Critical;
            
            auto goal = tracker->create_goal(title, desc, priority);
            res.set_content(nlohmann::json{{"success", true}, {"goal", goal.to_json()}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Get("/api/goals/attention", [](auto&, auto& res) {
        auto tracker = agent::get_goal_tracker();
        if (!tracker) {
            res.status = 500;
            res.set_content(R"({"error":"goal tracker not initialized"})", "application/json");
            return;
        }
        auto goals = tracker->get_goals_needing_attention();
        auto reminders = tracker->generate_reminders();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& g : goals) arr.push_back(g.to_json());
        res.set_content(nlohmann::json{{"goals", arr}, {"reminders", reminders}}.dump(), "application/json");
    });
    
    svr.Get("/api/goals/report", [](auto&, auto& res) {
        auto tracker = agent::get_goal_tracker();
        if (!tracker) {
            res.status = 500;
            res.set_content(R"({"error":"goal tracker not initialized"})", "application/json");
            return;
        }
        std::string report = tracker->generate_report();
        res.set_content(nlohmann::json{
            {"report", report},
            {"stats", {
                {"total", tracker->total_goals()},
                {"completed", tracker->completed_goals()},
                {"in_progress", tracker->in_progress_goals()},
                {"average_progress", tracker->average_progress()}
            }}
        }.dump(), "application/json");
    });
    
    svr.Post("/api/goals/([^/]+)/checkin", [](auto& req, auto& res) {
        try {
            auto tracker = agent::get_goal_tracker();
            if (!tracker) {
                res.status = 500;
                res.set_content(R"({"error":"goal tracker not initialized"})", "application/json");
                return;
            }
            std::string goal_id = req.matches[1];
            auto body = nlohmann::json::parse(req.body);
            double progress = body.value("progress", 0.0);
            std::string notes = body.value("notes", "");
            std::vector<std::string> milestones;
            if (body.contains("milestones")) {
                milestones = body["milestones"].template get<std::vector<std::string>>();
            }
            if (tracker->check_in(goal_id, progress, notes, milestones)) {
                auto goal = tracker->get_goal(goal_id);
                res.set_content(nlohmann::json{{"success", true}, {"goal", goal ? goal->to_json() : nlohmann::json()}}.dump(), "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"goal not found"})", "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    // Patterns & Insights
    svr.Get("/api/patterns", [](auto& req, auto& res) {
        auto analyzer = agent::get_session_analyzer();
        if (!analyzer) {
            res.status = 500;
            res.set_content(R"({"error":"session analyzer not initialized"})", "application/json");
            return;
        }
        auto patterns = analyzer->get_patterns();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : patterns) arr.push_back(p.to_json());
        res.set_content(nlohmann::json{{"patterns", arr}, {"count", patterns.size()}}.dump(), "application/json");
    });
    
    svr.Post("/api/patterns/detect", [](auto&, auto& res) {
        auto analyzer = agent::get_session_analyzer();
        if (!analyzer) {
            res.status = 500;
            res.set_content(R"({"error":"session analyzer not initialized"})", "application/json");
            return;
        }
        auto patterns = analyzer->detect_patterns();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : patterns) arr.push_back(p.to_json());
        res.set_content(nlohmann::json{{"success", true}, {"patterns", arr}, {"count", patterns.size()}}.dump(), "application/json");
    });
    
    svr.Get("/api/insights", [](auto&, auto& res) {
        auto analyzer = agent::get_session_analyzer();
        if (!analyzer) {
            res.status = 500;
            res.set_content(R"({"error":"session analyzer not initialized"})", "application/json");
            return;
        }
        auto insights = analyzer->generate_insights();
        auto suggestions = analyzer->get_improvement_suggestions();
        res.set_content(nlohmann::json{{"insights", insights}, {"suggestions", suggestions}}.dump(), "application/json");
    });
    
    // Sleep & Memory
    svr.Post("/api/sleep", [](auto&, auto& res) {
        auto processor = agent::get_sleep_processor();
        if (!processor) {
            res.status = 500;
            res.set_content(R"({"error":"sleep processor not initialized"})", "application/json");
            return;
        }
        auto result = processor->process({});
        res.set_content(result.to_json().dump(), "application/json");
    });
    
    svr.Get("/api/memory", [](auto&, auto& res) {
        auto processor = agent::get_sleep_processor();
        if (!processor) {
            res.status = 500;
            res.set_content(R"({"error":"sleep processor not initialized"})", "application/json");
            return;
        }
        auto memories = processor->get_long_term_memory();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : memories) arr.push_back(m.to_json());
        res.set_content(nlohmann::json{{"memories", arr}, {"count", memories.size()}}.dump(), "application/json");
    });
    
    svr.Get("/api/predictions", [](auto&, auto& res) {
        auto processor = agent::get_sleep_processor();
        if (!processor) {
            res.status = 500;
            res.set_content(R"({"error":"sleep processor not initialized"})", "application/json");
            return;
        }
        auto predictions = processor->get_predictions();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : predictions) arr.push_back(p.to_json());
        res.set_content(nlohmann::json{{"predictions", arr}, {"count", predictions.size()}}.dump(), "application/json");
    });
    
    svr.Get("/api/wake-report", [](auto&, auto& res) {
        auto processor = agent::get_sleep_processor();
        auto tracker = agent::get_goal_tracker();
        auto analyzer = agent::get_session_analyzer();
        if (!processor || !tracker || !analyzer) {
            res.status = 500;
            res.set_content(R"({"error":"self-evolution system not initialized"})", "application/json");
            return;
        }
        agent::SleepResult sr;
        sr.patterns_detected = analyzer->get_patterns().size();
        sr.reminders = tracker->generate_reminders();
        sr.recommendations = analyzer->get_improvement_suggestions();
        for (const auto& p : processor->get_predictions()) {
            sr.predicted_tasks.push_back(p.task);
        }
        std::string report = processor->generate_wake_report(sr);
        res.set_content(nlohmann::json{{"report", report}}.dump(), "application/json");
    });
    
    svr.Post("/api/sessions/start", [](auto&, auto& res) {
        auto analyzer = agent::get_session_analyzer();
        if (!analyzer) {
            res.status = 500;
            res.set_content(R"({"error":"session analyzer not initialized"})", "application/json");
            return;
        }
        std::string session_id = analyzer->start_session();
        res.set_content(nlohmann::json{{"success", true}, {"session_id", session_id}}.dump(), "application/json");
    });
    
    svr.Post("/api/sessions/end", [](auto& req, auto& res) {
        try {
            auto analyzer = agent::get_session_analyzer();
            if (!analyzer) {
                res.status = 500;
                res.set_content(R"({"error":"session analyzer not initialized"})", "application/json");
                return;
            }
            auto body = nlohmann::json::parse(req.body);
            std::string summary = body.value("summary", "");
            auto s = analyzer->end_session(summary);
            res.set_content(nlohmann::json{
                {"success", true},
                {"session_id", s.session_id},
                {"message_count", s.message_count},
                {"tool_call_count", s.tool_call_count},
                {"error_count", s.error_count}
            }.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    // ===== Session API =====
    auto sessions = std::make_shared<agent::SessionManager>("data/sessions");
    
    svr.Get("/api/sessions", [sessions](auto&, auto& res) {
        auto list = sessions->list_sessions();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : list) arr.push_back(s.to_json());
        res.set_content(nlohmann::json{{"sessions", arr}, {"count", list.size()}}.dump(), "application/json");
    });
    
    svr.Get("/api/sessions/([^/]+)", [sessions](auto& req, auto& res) {
        auto session = sessions->get_session(req.matches[1]);
        if (!session) {
            res.status = 404;
            res.set_content(R"({"error":"session not found"})","application/json");
            return;
        }
        res.set_content(session->to_json().dump(), "application/json");
    });
    
    svr.Post("/api/sessions", [agent, sessions](auto& req, auto& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string name = body.value("name", "");
            
            // Get current conversation
            nlohmann::json msgs = nlohmann::json::array();
            for (const auto& m : agent->get_conversation_history()) {
                msgs.push_back(m.to_json());
            }
            
            std::string id = sessions->save_session(name, agent->get_config().system_prompt, msgs);
            if (id.empty()) {
                res.status = 500;
                res.set_content(R"({"error":"failed to save session"})","application/json");
                return;
            }
            
            res.set_content(nlohmann::json{{"success", true}, {"id", id}}.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Post("/api/sessions/([^/]+)/load", [agent, sessions](auto& req, auto& res) {
        try {
            auto session = sessions->get_session(req.matches[1]);
            if (!session) {
                res.status = 404;
                res.set_content(R"({"error":"session not found"})","application/json");
                return;
            }
            
            // Load messages into agent
            std::vector<agent::Message> msgs;
            if (session->messages.is_array()) {
                for (const auto& m : session->messages) {
                    msgs.push_back(agent::Message::from_json(m));
                }
            }
            agent->load_conversation(msgs);
            if (!session->system_prompt.empty()) {
                agent->set_system_prompt(session->system_prompt);
            }
            
            res.set_content(nlohmann::json{
                {"success", true}, 
                {"message_count", msgs.size()},
                {"name", session->meta.name}
            }.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Delete("/api/sessions/([^/]+)", [sessions](auto& req, auto& res) {
        bool removed = sessions->delete_session(req.matches[1]);
        res.set_content(nlohmann::json{{"success", removed}}.dump(), "application/json");
    });
    
    // ===== MCP API =====
    svr.Post("/api/mcp/connect", [agent](auto& req, auto& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string endpoint = body.value("endpoint", "");
            std::string transport = body.value("transport", "stdio");
            
            if (endpoint.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"endpoint required"})","application/json");
                return;
            }
            
            if (agent->connect_mcp_server(endpoint, transport)) {
                auto clients = agent->get_mcp_clients();
                auto& last = clients.back();
                auto info = last->get_server_info();
                res.set_content(nlohmann::json{
                    {"success", true},
                    {"server", {
                        {"name", info.name},
                        {"version", info.version},
                        {"capabilities", info.capabilities}
                    }}
                }.dump(), "application/json");
            } else {
                res.status = 500;
                res.set_content(R"({"error":"failed to connect to MCP server"})","application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Get("/api/mcp/servers", [agent](auto&, auto& res) {
        auto clients = agent->get_mcp_clients();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& client : clients) {
            auto info = client->get_server_info();
            arr.push_back({
                {"name", info.name},
                {"version", info.version},
                {"connected", client->is_connected()},
                {"capabilities", info.capabilities}
            });
        }
        res.set_content(nlohmann::json{{"servers", arr}, {"count", arr.size()}}.dump(), "application/json");
    });
    
    svr.Get("/api/mcp/tools", [agent](auto&, auto& res) {
        auto tools = agent->list_mcp_tools();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& t : tools) {
            arr.push_back({
                {"name", t.name},
                {"description", t.description},
                {"inputSchema", t.input_schema}
            });
        }
        res.set_content(nlohmann::json{{"tools", arr}, {"count", arr.size()}}.dump(), "application/json");
    });
    
    svr.Post("/api/mcp/tools/([^/]+)/call", [agent](auto& req, auto& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string server_name = body.value("server", "");
            std::string tool_name = req.matches[1];
            nlohmann::json args = body.value("arguments", nlohmann::json::object());
            
            auto result = agent->call_mcp_tool(server_name, tool_name, args);
            
            nlohmann::json content_arr = nlohmann::json::array();
            for (const auto& c : result.content) {
                content_arr.push_back({
                    {"type", c.type},
                    {"text", c.text},
                    {"data", c.data},
                    {"mimeType", c.mime_type},
                    {"uri", c.uri}
                });
            }
            
            res.set_content(nlohmann::json{
                {"isError", result.is_error},
                {"content", content_arr},
                {"errorMessage", result.error_message}
            }.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500; res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    svr.Get("/api/mcp/prompts", [agent](auto&, auto& res) {
        auto clients = agent->get_mcp_clients();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& client : clients) {
            auto prompts = client->list_prompts();
            for (const auto& p : prompts) {
                arr.push_back({
                    {"name", p.name},
                    {"description", p.description},
                    {"arguments", p.arguments}
                });
            }
        }
        res.set_content(nlohmann::json{{"prompts", arr}, {"count", arr.size()}}.dump(), "application/json");
    });
    
    svr.Get("/api/mcp/resources", [agent](auto&, auto& res) {
        auto clients = agent->get_mcp_clients();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& client : clients) {
            auto resources = client->list_resources();
            for (const auto& r : resources) {
                arr.push_back({
                    {"uri", r.uri},
                    {"name", r.name},
                    {"description", r.description},
                    {"mimeType", r.mime_type}
                });
            }
        }
        res.set_content(nlohmann::json{{"resources", arr}, {"count", arr.size()}}.dump(), "application/json");
    });
    
    svr.set_mount_point("/", "./web/dist");
    
    spdlog::info("Server on port {}", port);
    svr.listen("0.0.0.0", port);
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    print_banner();
    
    std::string config_file = "config.json";
    std::string cron_file = "cron_tasks.json";
    bool server_mode = false;
    int port = 8080, threads = 4;
    
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i+1 < argc) config_file = argv[++i];
        else if (a == "--cron" && i+1 < argc) cron_file = argv[++i];
        else if (a == "--server") server_mode = true;
        else if (a == "--port" && i+1 < argc) port = std::stoi(argv[++i]);
        else if (a == "--threads" && i+1 < argc) threads = std::stoi(argv[++i]);
        else if (a == "--help" || a == "-h") { print_usage(argv[0]); return 0; }
    }
    
    agent::register_builtin_tools();
    agent::init_self_evolution(".");  // Initialize self-evolution system
    spdlog::info("Tools: {}", agent::ToolRegistry::instance().list_tools().size());
    
    // Load config
    agent::AgentConfig cfg;
    std::ifstream cf(config_file);
    if (cf.is_open()) {
        try { nlohmann::json j; cf >> j;
            cfg.api_key = j.value("api_key", "");
            cfg.api_base = j.value("api_base", "https://api.openai.com/v1");
            cfg.model = j.value("model", "gpt-4");
            cfg.system_prompt = j.value("system_prompt", "You are a helpful AI assistant.");
            cfg.max_tokens = j.value("max_tokens", 4096);
            cfg.temperature = j.value("temperature", 0.7);
        } catch (...) {}
    }
    
    const char* k = std::getenv("OPENAI_API_KEY");
    if (k && cfg.api_key.empty()) cfg.api_key = k;
    const char* b = std::getenv("OPENAI_API_BASE");
    if (b) cfg.api_base = b;
    
    if (cfg.api_key.empty()) spdlog::warn("No API key");
    
    asio::io_context io;
    auto agent = std::make_shared<agent::Agent>(io, cfg);
    auto cron = std::make_shared<agent::CronManager>(cron_file);
    
    // Enable cron tools
    agent::set_cron_manager(cron.get());
    
    // Set cron callback to execute commands via agent
    cron->set_callback([&agent](const agent::CronTask& task) {
        spdlog::info("Executing cron task: {} - {}", task.name, task.command);
        try {
            auto result = agent->chat_sync(task.command);
            spdlog::info("Cron result: {}", result.substr(0, 200));
        } catch (const std::exception& e) {
            spdlog::error("Cron error: {}", e.what());
        }
    });
    
    // Start cron scheduler
    cron->start();
    
    spdlog::info("Model: {}", cfg.model);
    spdlog::info("Cron tasks: {}", cron->list_tasks().size());
    
    if (server_mode) run_server(agent, cron, port);
    else run_cli(agent, cron);
    
    cron->stop();
    return 0;
}
