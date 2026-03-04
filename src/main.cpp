#include "core/Agent.hpp"
#include "tools/ToolRegistry.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

// Forward declaration for builtin tools registration
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
    C++23 + libcurl + TDesign
)" << std::endl;
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n\n"
              << "Options:\n"
              << "  --config <file>  Load config from JSON file (default: config.json)\n"
              << "  --help           Show this help message\n\n"
              << "Environment Variables:\n"
              << "  OPENAI_API_KEY   API key for OpenAI-compatible services\n"
              << "  OPENAI_API_BASE  API base URL (default: https://api.openai.com/v1)\n";
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    print_banner();
    
    // Parse arguments
    std::string config_file = "config.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
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
    
    std::cout << "\nChat ready. Type 'exit' to quit, 'help' for commands.\n" << std::endl;
    
    std::string input;
    while (true) {
        std::cout << "You: ";
        std::getline(std::cin, input);
        
        if (input.empty()) continue;
        
        if (input == "exit" || input == "quit") {
            break;
        }
        
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
    
    std::cout << "Goodbye!\n";
    return 0;
}
