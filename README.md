# C++ AI Agent

一个功能完整的 AI Agent 框架，支持对话、任务执行、工具调用和技能扩展。

## 特性

- 🤖 **对话能力**: 支持 OpenAI API 兼容接口的 LLM 对话
- 🛠️ **工具系统**: 内置文件读写、目录列表、命令执行、网络搜索等工具
- 🎯 **技能扩展**: 可动态加载的技能系统，支持 YAML/JSON 配置
- 🔌 **MCP 协议**: 支持 Model Context Protocol（规划中）
- 🌐 **HTTP API**: RESTful API 接口
- 🖥️ **Web UI**: 基于 TDesign 的现代 Web 界面
- 📦 **C++23**: 使用最新 C++ 标准和模块化设计

## 构建

### 依赖

- C++23 编译器 (GCC 13+, Clang 16+, MSVC 19.35+)
- xmake
- libcurl

### 编译

```bash
# 安装依赖
xmake config --mode=release
xmake build

# 运行 CLI 模式
xmake run cpp-agent

# 运行服务器模式
xmake run cpp-agent --server --port 8080
```

## 配置

编辑 `config.json`:

```json
{
  "api_key": "your-api-key",
  "api_base": "https://api.openai.com/v1",
  "model": "gpt-4",
  "system_prompt": "You are a helpful AI assistant.",
  "max_tokens": 4096,
  "temperature": 0.7
}
```

或设置环境变量:

```bash
export OPENAI_API_KEY=your-api-key
export OPENAI_API_BASE=https://api.openai.com/v1  # 可选
```

## 使用

### CLI 模式

```bash
./cpp-agent
```

命令:
- `tools` - 列出所有可用工具
- `skills` - 列出所有可用技能
- `clear` - 清空对话历史
- `exit` - 退出程序

### 服务器模式

```bash
./cpp-agent --server --port 8080
```

API 端点:
- `POST /api/chat` - 发送消息
- `GET /api/tools` - 列出工具
- `POST /api/tools/{name}/execute` - 执行工具
- `GET /api/skills` - 列出技能
- `DELETE /api/conversation` - 清空对话
- `GET /api/conversation` - 获取对话历史
- `GET /api/health` - 健康检查

### Web UI

```bash
# 构建 Web 前端
cd web
npm install
npm run build

# 启动服务器
cd ..
./cpp-agent --server

# 访问 http://localhost:8080
```

开发模式（前端热重载）:
```bash
# 终端 1: 启动后端
./cpp-agent --server --port 8080

# 终端 2: 启动前端开发服务器
cd web
npm run dev
# 访问 http://localhost:3000 (自动代理到 8080)
```

## 内置工具

| 工具 | 描述 | 权限级别 |
|------|------|----------|
| `web_search` | 网络搜索 | read |
| `file_read` | 读取文件 | read |
| `file_write` | 写入文件 | write |
| `list_directory` | 列出目录 | read |
| `execute` | 执行命令 | execute |
| `get_time` | 获取时间 | read |

## 扩展

### 添加自定义工具

```cpp
#include "tools/Tool.hpp"

class MyTool : public agent::Tool {
public:
    std::string name() const override { return "my_tool"; }
    std::string description() const override { return "My custom tool"; }
    
    ToolSchema schema() const override {
        return ToolSchema{name(), description(),
            {{"type", "object"}, {"properties", {
                {"input", {{"type", "string"}, {"description", "Input value"}}}
            }}}
        };
    }
    
    ToolResult execute(const nlohmann::json& args) override {
        return ToolResult::ok("Result: " + args["input"].get<std::string>());
    }
};

// 注册
ToolRegistry::instance().add_tool(std::make_shared<MyTool>());
```

## 许可证

MIT License
