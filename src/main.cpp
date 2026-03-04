#include "core/Agent.hpp"
#include "tools/ToolRegistry.hpp"
#include <spdlog/spdlog.h>
#include <iostream>

namespace tools = agent::tools;

void print_banner() {
    std::cout << R"(
  _____ _____ _____    _   
 / ____|  __ \_   _|  | |  
| |    | |  | || |  __| |_ 
| |    | |  | || | / _` __|
| |____| |__| || || (_| |  
 \_____|_____/ |_| \__,_|  

    AI Agent Framework v0.1.0
    C++23 + Oat++ + TDesign
)" << std::endl;
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    print_banner();
    
    // Register built-in tools
    tools::register_builtin_tools();
    spdlog::info("Registered {} tools", agent::ToolRegistry::instance().list_tools().size());
    
    // Create agent
    agent::AgentConfig config;
    const char* api_key = std::getenv("OPENAI_API_KEY");
    if (api_key) config.api_key = api_key;
    
    const char* api_base = std::getenv("OPENAI_API_BASE");
    if (api_base) config.api_base = api_base;
    
    config.model = "gpt-4";
    config.system_prompt = "You are a helpful AI assistant with access to various tools.";
    config.enable_tools = true;
    
    auto agent = std::make_shared<agent::Agent>(config);
    
    spdlog::info("Agent initialized");
    spdlog::info("API Base: {}", config.api_base);
    spdlog::info("Model: {}", config.model);
    
    // TODO: Start Oat++ server
    // For now, simple CLI demo
    std::cout << "\nChat ready. Type 'exit' to quit.\n" << std::endl;
    
    std::string input;
    while (true) {
        std::cout << "You: ";
        std::getline(std::cin, input);
        
        if (input == "exit" || input == "quit") break;
        if (input.empty()) continue;
        
        try {
            auto future = agent->chat(input);
            std::string response = future.get();
            std::cout << "\nAI: " << response << "\n" << std::endl;
        } catch (const std::exception& e) {
            spdlog::error("Error: {}", e.what());
        }
    }
    
    return 0;
}
