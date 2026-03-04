# CPP-Agent 🤖

A powerful AI Agent framework built with C++23, featuring chat, task execution, tools, skills, and MCP support.

## Features

- **💬 Chat Interface** - Natural language conversations with AI
- **⚡ Task Execution** - Autonomous task completion using tools
- **🔧 Tool System** - Extensible tool architecture with auto-registration
- **🎯 Skills** - Higher-level capabilities composed of tools
- **🔌 MCP Support** - Model Context Protocol for external integrations
- **🌐 Modern Web UI** - TDesign-based responsive interface
- **🔒 Safe Execution** - Permission levels and confirmation dialogs

## Architecture

```
cpp-agent/
├── include/
│   ├── core/           # Agent core (Agent, LLMClient)
│   ├── tools/          # Tool system
│   ├── skills/         # Skill system
│   ├── mcp/            # MCP client/server
│   └── api/            # REST API controllers
├── src/
│   ├── core/           # Core implementations
│   ├── tools/          # Built-in tools
│   ├── skills/         # Skill implementations
│   └── main.cpp        # Entry point
├── web/                # Vue 3 + TDesign frontend
├── tests/              # Unit tests
└── configs/            # Configuration files
```

## Tech Stack

### Backend
- **C++23** - Modern C++ features
- **Oat++** - High-performance web framework
- **Boost** - Asio, filesystem, and utilities
- **nlohmann/json** - JSON handling
- **spdlog** - Fast logging
- **SQLite3** - Persistent storage

### Frontend
- **Vue 3** - Progressive JavaScript framework
- **TDesign** - Enterprise-level design system
- **TypeScript** - Type safety
- **Vite** - Fast build tool
- **Axios** - HTTP client
- **marked** - Markdown rendering

## Build & Run

### Prerequisites

- C++23 compatible compiler (GCC 13+, Clang 16+, MSVC 2022+)
- xmake build system
- Node.js 18+ (for frontend)

### Backend Build

```bash
# Install dependencies
xmake config --mode=release

# Build
xmake build

# Run
./build/cpp-agent
```

### Frontend Build

```bash
cd web
npm install
npm run dev  # Development
npm run build  # Production
```

### Environment Variables

```bash
export OPENAI_API_KEY="your-api-key"
export OPENAI_API_BASE="https://api.openai.com/v1"  # Optional
export SKILLS_DIR="./skills"  # Optional
```

## Usage

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/chat` | POST | Send a chat message |
| `/api/chat/stream` | POST | Streaming chat (SSE) |
| `/api/task` | POST | Execute a task |
| `/api/tools` | GET | List available tools |
| `/api/tools/{name}/execute` | POST | Execute a tool |
| `/api/skills` | GET | List available skills |
| `/api/skills/{name}/execute` | POST | Execute a skill |
| `/api/conversation` | GET/DELETE | Get/clear history |
| `/api/mcp/connect` | POST | Connect MCP server |

### Example: Chat

```bash
curl -X POST http://localhost:8000/api/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "Hello, who are you?"}'
```

### Example: Execute Tool

```bash
curl -X POST http://localhost:8000/api/tools/read_file/execute \
  -H "Content-Type: application/json" \
  -d '{"path": "/tmp/test.txt"}'
```

## Built-in Tools

| Tool | Description | Permission |
|------|-------------|------------|
| `execute_command` | Execute shell commands | execute |
| `read_file` | Read file contents | read |
| `write_file` | Write file contents | write |
| `list_directory` | List directory contents | read |
| `web_search` | Search the web | read |

## Extending

### Adding a New Tool

```cpp
#include "tools/Tool.hpp"

class MyTool : public agent::Tool {
public:
    std::string name() const override { return "my_tool"; }
    std::string description() const override { return "My custom tool"; }
    
    ToolSchema schema() const override {
        return ToolSchema{name(), description(), {
            {"type", "object"},
            {"properties", {
                {"input", {{"type", "string"}}}
            }},
            {"required", {"input"}}
        }};
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        std::string input = args["input"];
        return ToolResult::ok("Processed: " + input);
    }
};

// Register
REGISTER_TOOL(MyTool)
```

### Adding a New Skill

```cpp
#include "skills/Skill.hpp"

class MySkill : public agent::Skill {
public:
    SkillMetadata metadata() const override {
        return {"my_skill", "1.0.0", "My skill", "You", {}, {}};
    }
    
    std::vector<std::string> actions() const override {
        return {"do_something", "do_another_thing"};
    }
    
    SkillResult execute(
        const std::string& action,
        const nlohmann::json& params,
        const SkillContext& context
    ) override {
        // Implementation
        return {true, "Success", {}};
    }
};
```

## MCP Support

Connect to Model Context Protocol servers:

```bash
# Add MCP server via API
curl -X POST http://localhost:8000/api/mcp/connect \
  -H "Content-Type: application/json" \
  -d '{"name": "filesystem", "endpoint": "stdio:///path/to/mcp-server-filesystem"}'
```

## License

MIT License

## Contributing

Contributions welcome! Please read CONTRIBUTING.md first.

## Roadmap

- [ ] Vector database integration (semantic memory)
- [ ] Multi-agent collaboration
- [ ] Plugin marketplace
- [ ] Voice interface
- [ ] Mobile app
- [ ] Cloud deployment templates
