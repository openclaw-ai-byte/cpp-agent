#include "core/Agent.hpp"
#include "tools/ToolRegistry.hpp"
#include "http/httplib.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

namespace asio = boost::asio;

namespace agent { void register_builtin_tools(); }

void print_banner() {
    std::cout << R"(
  _____ _____ _____    _   
 / ____|  __ \_   _|  | |  
| |    | |  | || |  __| |_ 
| |    | |  | || | / _` __|
| |____| |__| || || (_| |  
 \_____|_____/ |_| \__,_|  

    AI Agent Framework v0.2.0
    C++23 + ASIO Coroutine
)" << std::endl;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  --config <file>  Config file (default: config.json)\n"
              << "  --server         HTTP server mode\n"
              << "  --port <n>       Server port (default: 8080)\n"
              << "  --threads <n>    Thread pool size (default: 4)\n"
              << "  --help           Show help\n";
}

void run_cli(std::shared_ptr<agent::Agent> agent) {
    std::cout << "\nReady. Commands: exit, help, tools, clear\n\n";
    std::string input;
    while (true) {
        std::cout << "You: ";
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;
        if (input == "exit" || input == "quit") break;
        if (input == "help") { std::cout << "\nexit, help, tools, clear\n\n"; continue; }
        if (input == "tools") {
            for (auto& t : agent->list_tools()) std::cout << "  - " << t << "\n";
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

void run_server(std::shared_ptr<agent::Agent> agent, int port) {
    httplib::Server svr;
    svr.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Methods", "GET,POST,DELETE,OPTIONS"},
                             {"Access-Control-Allow-Headers", "Content-Type"}});
    svr.Options(R"(.*)", [](auto&, auto& res) { res.status = 204; });
    
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
    
    svr.Get("/api/skills", [](auto&, auto& res) {
        auto skills = agent::SkillRegistry::instance().list_skills();
        res.set_content(nlohmann::json{{"skills", skills}}.dump(), "application/json");
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
    
    svr.Get("/api/health", [](auto&, auto& res) { res.set_content(R"({"status":"ok"})", "application/json"); });
    svr.set_mount_point("/", "./web/dist");
    
    spdlog::info("Server on port {}", port);
    svr.listen("0.0.0.0", port);
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    print_banner();
    
    std::string config_file = "config.json";
    bool server_mode = false;
    int port = 8080, threads = 4;
    
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i+1 < argc) config_file = argv[++i];
        else if (a == "--server") server_mode = true;
        else if (a == "--port" && i+1 < argc) port = std::stoi(argv[++i]);
        else if (a == "--threads" && i+1 < argc) threads = std::stoi(argv[++i]);
        else if (a == "--help" || a == "-h") { print_usage(argv[0]); return 0; }
    }
    
    agent::register_builtin_tools();
    spdlog::info("Tools: {}", agent::ToolRegistry::instance().list_tools().size());
    
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
    spdlog::info("Model: {}", cfg.model);
    
    if (server_mode) run_server(agent, port);
    else run_cli(agent);
    
    return 0;
}
