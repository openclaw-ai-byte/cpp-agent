# C++ AI Agent

一个功能完整的 AI Agent 框架，支持对话、任务执行、工具调用、技能扩展和定时任务。

## 特性

- 🤖 **对话能力**: 支持 OpenAI API 兼容接口的 LLM 对话，支持流式响应
- ⏰ **定时任务**: 内置 Cron 调度器，支持通过 API 添加/管理定时任务
- 🔄 **重试机制**: 自动重试失败的请求，可配置重试次数和延迟
- 🌐 **连接池**: CURL 连接复用，提升性能
- 🛠️ **工具系统**: 内置文件读写、目录列表、命令执行、网络搜索等工具
- 🎯 **技能扩展**: 可动态加载的技能系统，支持 YAML/JSON 配置
- 🌐 **HTTP API**: RESTful API 接口
- 📦 **C++23**: 使用最新 C++ 标准和协程设计

## 构建

### 依赖

- C++23 编译器 (Clang 16+)
- xmake
- libcurl, boost, spdlog, nlohmann_json

### 编译

```bash
xmake config --mode=release
xmake build
```

## 使用

### CLI 模式

```bash
./cpp-agent
```

命令:
- `tools` - 列出所有可用工具
- `cron` - 列出定时任务
- `clear` - 清空对话历史
- `exit` - 退出程序

### 服务器模式

```bash
./cpp-agent --server --port 8080
```

## API 端点

### 对话
- `POST /api/chat` - 发送消息
- `GET /api/conversation` - 获取对话历史
- `DELETE /api/conversation` - 清空对话

### 工具
- `GET /api/tools` - 列出工具
- `POST /api/tools/{name}/execute` - 执行工具

### Cron 定时任务
- `GET /api/cron` - 列出所有定时任务
- `POST /api/cron` - 添加定时任务
- `GET /api/cron/{id}` - 获取任务详情
- `DELETE /api/cron/{id}` - 删除任务
- `POST /api/cron/{id}/toggle` - 启用/禁用任务
- `POST /api/cron/start` - 启动调度器
- `POST /api/cron/stop` - 停止调度器

### Cron 表达式格式

```
minute hour day month weekday
  *     *    *    *      *
  
例如:
  "0 8 * * *"      - 每天 8:00
  "30 9 * * 1-5"   - 工作日 9:30
  "0 */2 * * *"    - 每 2 小时
```

### 添加定时任务示例

```bash
curl -X POST http://localhost:8080/api/cron \
  -H "Content-Type: application/json" \
  -d '{
    "name": "每日报告",
    "cron": "0 8 * * *",
    "command": "生成今天的报告"
  }'
```

## 配置

编辑 `config.json`:

```json
{
  "api_key": "your-api-key",
  "api_base": "https://api.openai.com/v1",
  "model": "gpt-4",
  "max_tokens": 4096,
  "temperature": 0.7
}
```

或设置环境变量:

```bash
export OPENAI_API_KEY=your-api-key
```

## 许可证

MIT License
