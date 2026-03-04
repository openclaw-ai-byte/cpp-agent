#include "core/Agent.hpp"
#include "tools/ToolRegistry.hpp"
#include "skills/SkillRegistry.hpp"
#include "http/httplib.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <memory>
#include <thread>

// Forward declaration
namespace agent {
    void register_builtin_tools();
}

void print_banner() {
    std::cout << R"(
  _____ _____ _____    _   
 / ____|  __ \_   _|  | |  
| |    | |  | || |  __| |_ 
| |    | |  | || | / _` __|
| |____| |__| || || (_| |  
 \_____|_____/ |_| \__,_|  

    AI Agent Framework v0.1.0
    C++23 + libcurl + Vue
)" << std::endl;
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  --config <file>  Load config from JSON file (default: config.json)\n"
              << "  --server         Run in server mode (HTTP API)\n"
              << "  --port <port>    Server port (default: 8080)\n"
              << "  --help           Show this help message\n\n"
              << "Environment Variables:\n"
              << "  OPENAI_API_KEY   API key for OpenAI-compatible services\n"
              << "  OPENAI_API_BASE  API base URL (default: https://api.openai.com/v1)\n";
}

void run_cli(std::shared_ptr<agent::Agent> agent) {
    std::cout << "\nChat ready. Type 'exit' to quit, 'help' for commands.\n" << std::endl;
    
    std::string input;
    while (true) {
        std::cout << "You: ";
        std::getline(std::cin, input);
        
        if (input.empty()) continue;
        
        if (input == "exit" || input == "quit") break;
        
        if (input == "help") {
            std::cout << "\nCommands:\n"
                      << "  exit/quit - Exit the program\n"
                      << "  help      - Show this help\n"
                      << "  tools     - List available tools\n"
                      << "  clear     - Clear conversation history\n\n";
            continue;
        }
        
        if (input == "tools") {
            std::cout << "\nAvailable Tools:\n";
            for (const auto& tool : agent->list_tools()) {
                std::cout << "  - " << tool << "\n";
            }
            std::cout << "\n";
            continue;
        }
        
        if (input == "clear") {
            agent->clear_conversation();
            std::cout << "Conversation cleared.\n\n";
            continue;
        }
        
        try {
            std::cout << "\nThinking...\n";
            auto future = agent->chat(input);
            std::string response = future.get();
            std::cout << "\nAI: " << response << "\n" << std::endl;
        } catch (const std::exception& e) {
            spdlog::error("Error: {}", e.what());
            std::cout << "\nError: " << e.what() << "\n" << std::endl;
        }
    }
}

void run_server(std::shared_ptr<agent::Agent> agent, int port) {
    httplib::Server svr;
    
    // CORS middleware
    svr.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });
    
    // Handle preflight
    svr.Options(R"(.*)", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 204;
    });
    
    // Chat endpoint
    svr.Post("/api/chat", [agent](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string message = body.value("message", "");
            std::string system_prompt = body.value("system_prompt", "");
            
            if (message.empty()) {
                res.status = 400;
                res.set_content(R"({"error":"message is required"})", "application/json");
                return;
            }
            
            if (!system_prompt.empty()) {
                agent->set_system_prompt(system_prompt);
            }
            
            auto future = agent->chat(message);
            std::string response = future.get();
            
            nlohmann::json result = {
                {"response", response},
                {"success", true}
            };
            res.set_content(result.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    // List tools
    svr.Get("/api/tools", [](const httplib::Request& req, httplib::Response& res) {
        auto tools = agent::ToolRegistry::instance().list_tools();
        nlohmann::json result = {
            {"tools", tools},
            {"count", tools.size()}
        };
        res.set_content(result.dump(), "application/json");
    });
    
    // Execute tool
    svr.Post("/api/tools/([^/]+)/execute", [](const httplib::Request& req, httplib::Response& res) {
        std::string tool_name = req.matches[1];
        try {
            auto body = nlohmann::json::parse(req.body);
            auto result = agent::ToolRegistry::instance().execute_tool(tool_name, body);
            
            nlohmann::json response = {
                {"success", result.success},
                {"output", result.output}
            };
            if (!result.data.is_null()) {
                response["data"] = result.data;
            }
            res.set_content(response.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });
    
    // List skills
    svr.Get("/api/skills", [](const httplib::Request& req, httplib::Response& res) {
        auto skills = agent::SkillRegistry::instance().list_skills();
        nlohmann::json result = {
            {"skills", skills},
            {"count", skills.size()}
        };
        res.set_content(result.dump(), "application/json");
    });
    
    // Clear conversation
    svr.Delete("/api/conversation", [agent](const httplib::Request& req, httplib::Response& res) {
        agent->clear_conversation();
        res.set_content(R"({"success":true,"message":"Conversation cleared"})", "application/json");
    });
    
    // Get history
    svr.Get("/api/conversation", [agent](const httplib::Request& req, httplib::Response& res) {
        auto history = agent->get_conversation_history();
        nlohmann::json messages = nlohmann::json::array();
        for (const auto& msg : history) {
            messages.push_back(msg.to_json());
        }
        res.set_content(nlohmann::json{{"messages", messages}}.dump(), "application/json");
    });
    
    // Health check
    svr.Get("/api/health", [](const httplib::Request& req, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });
    
    // Serve static files (web UI)
    svr.set_mount_point("/", "./web/dist");
    
    spdlog::info("HTTP Server starting on port {}", port);
    spdlog::info("API endpoints:");
    spdlog::info("  POST /api/chat              - Chat with agent");
    spdlog::info("  GET  /api/tools             - List available tools");
    spdlog::info("  POST /api/tools/:name/execute - Execute a tool");
    spdlog::info("  GET  /api/skills            - List available skills");
    spdlog::info("  GET  /api/conversation      - Get conversation history");
    spdlog::info("  DELETE /api/conversation    - Clear conversation");
    spdlog::info("  GET  /api/health            - Health check");
    spdlog::info("  GET  /                      - Web UI (if built)");
    
    std::cout << "\n🌐 Server running at http://localhost:" << port << "\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
    
    svr.listen("0.0.0.0", port);
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    print_banner();
    
    // Parse arguments
    std::string config_file = "config.json";
    bool server_mode = false;
    int port = 8080;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--server") {
            server_mode = true;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    // Register built-in tools
    agent::register_builtin_tools();
    spdlog::info("Registered {} tools", agent::ToolRegistry::instance().list_tools().size());
    
    // Create agent config
    agent::AgentConfig config;
    
    // Load from config file
    std::ifstream cf(config_file);
    if (cf.is_open()) {
        try {
            nlohmann::json j;
            cf >> j;
            config.api_key = j.value("api_key", "");
            config.api_base = j.value("api_base", "https://api.openai.com/v1");
            config.model = j.value("model", "gpt-4");
            config.system_prompt = j.value("system_prompt", "You are a helpful AI assistant.");
            config.max_tokens = j.value("max_tokens", 4096);
            config.temperature = j.value("temperature", 0.7);
            spdlog::info("Loaded config from {}", config_file);
        } catch (const std::exception& e) {
            spdlog::warn("Failed to parse config file: {}", e.what());
        }
    }
    
    // Override with environment variables
    const char* env_api_key = std::getenv("OPENAI_API_KEY");
    if (env_api_key && config.api_key.empty()) {
        config.api_key = env_api_key;
        spdlog::info("Using API key from OPENAI_API_KEY");
    }
    
    const char* env_api_base = std::getenv("OPENAI_API_BASE");
    if (env_api_base) {
        config.api_base = env_api_base;
    }
    
    // Warn if no API key
    if (config.api_key.empty()) {
        spdlog::warn("No API key configured. Set OPENAI_API_KEY or add 'api_key' to config.json");
    }
    
    // Create agent
    auto agent = std::make_shared<agent::Agent>(config);
    
    spdlog::info("Agent initialized");
    spdlog::info("API Base: {}", config.api_base);
    spdlog::info("Model: {}", config.model);
    
    if (server_mode) {
        run_server(agent, port);
    } else {
        run_cli(agent);
    }
    
    return 0;
}
