# esp-agent — esp-claw 桌面模拟器

在 Linux 桌面端完整运行 [esp-claw](https://github.com/espressif/esp-claw) 嵌入式 AI Agent 框架，无需 ESP32 硬件。通过一套 POSIX 兼容层（FreeRTOS→pthread、ESP-IDF→stub），将 esp-claw 的核心代码直接在桌面 Linux 上编译运行。

## 特性

- **零硬件依赖** — 不需要 ESP32 开发板、不需要 JTAG 调试器，纯软件模拟
- **即装即用** — 提供预构建 `.deb` 包，一键安装，自动解决依赖
- **DeepSeek 兼容** — 内置 OpenAI 兼容后端，支持 DeepSeek、OpenAI、Ollama 等
- **完整 CLI 支持** — 9 条内置命令行（`ask`、`cap`、`auto`、`lua`、`skill` 等）通过 Unix Socket 可用
- **可选的屏幕模拟** — 通过 SDL2 窗口模拟 ESP32 LCD 显示屏，可配置关闭
- **systemd 用户服务** — 支持登录自动启动
- **独立设计** — 模拟器代码完全与上游 `esp-claw/` 分离，互不污染

## 系统要求

- **操作系统**: Linux（Ubuntu 22.04+ / Debian 12+ 或其他 systemd 发行版）
- **架构**: x86_64 (amd64)

## 快速开始

### 方式一：预构建包安装（推荐）

```bash
# 1. 安装 .deb 包
sudo apt install ./esp-agent_1.0.0_amd64.deb

# 2. 首次配置（设置 LLM API Key）
esp-agent config

# 3. 启动 Agent
esp-agent start

# 4. 在另一个终端连接 CLI
esp-agent connect
```

### 方式二：从源码构建

```bash
# 1. 安装编译依赖
sudo apt install build-essential cmake pkg-config \
  libcurl4-openssl-dev liblua5.4-dev libsdl2-dev libjson-c-dev

# 2. 克隆仓库
git clone <repo-url> esp-agent && cd esp-agent
git submodule update --init

# 3. 编译
./esp-agent build

# 4. 配置并启动
./esp-agent config
./esp-agent start
./esp-agent connect
```

## 配置 LLM

### 方式一：交互式配置向导

```bash
esp-agent config
```

交互式向导会依次询问 API Key、模型名称、Profile 类型、自定义 Base URL 等信息。

### 方式二：环境变量

```bash
LLM_API_KEY=sk-xxx LLM_MODEL=gpt-4o LLM_PROFILE=openai esp-agent start
```

环境变量会覆盖 `config.json` 中的对应值。

### 方式三：直接编辑配置文件

`~/.esp-agent/config.json`:

```json
{
  "llm": {
    "api_key": "sk-your-api-key",
    "model": "deepseek-chat",
    "profile": "openai",
    "base_url": ""
  },
  "display": {
    "enabled": true
  }
}
```

**支持的 `profile` 值：**

| profile | 默认 API 端点 |
|---------|-------------|
| `openai` | `https://api.openai.com/v1` |
| `anthropic` | `https://api.anthropic.com/v1` |
| `custom` | 自定义后端 |

### 常用 LLM 服务商配置示例

**DeepSeek:**
```json
{
  "llm": {
    "api_key": "sk-your-deepseek-key",
    "model": "deepseek-chat",
    "profile": "openai",
    "base_url": "https://api.deepseek.com"
  }
}
```

**本地 Ollama:**
```json
{
  "llm": {
    "api_key": "ollama",
    "model": "llama3",
    "profile": "openai",
    "base_url": "http://localhost:11434/v1"
  }
}
```

## CLI 命令参考

### esp-agent 管理命令

| 命令 | 说明 |
|------|------|
| `esp-agent config` | 交互式配置向导（首次使用） |
| `esp-agent start` | 后台启动 Agent，实时显示日志 |
| `esp-agent stop` | 优雅停止 Agent |
| `esp-agent status` | 检查 Agent 运行状态 |
| `esp-agent connect` | 连接到 Agent 的 CLI REPL |
| `esp-agent build` | 从源码编译（仅开发模式） |
| `esp-agent clean` | 清理编译产物 |
| `esp-agent service enable` | 启用 systemd 用户服务（登录自启） |
| `esp-agent service disable` | 禁用 systemd 用户服务 |
| `esp-agent --version` | 显示版本号 |

### Agent CLI 命令（通过 `connect` 进入）

| 命令 | 说明 |
|------|------|
| `help` | 列出所有可用命令 |
| `ask <prompt>` | 提交多轮对话请求（使用当前会话） |
| `ask_once <prompt>` | 提交单轮对话请求（无会话历史） |
| `session [id]` | 查看或切换当前会话 |
| `cap list` | 列出所有已注册的能力 (33项) |
| `cap groups` | 列出所有能力分组 (7组) |
| `cap call <name> <json>` | 直接调用指定能力 |
| `auto rules` | 查看事件路由规则 |
| `auto last` | 查看最后一次路由结果 |
| `auto reload` | 重新加载路由规则 |
| `lua --list` | 列出可用的 Lua 脚本 |
| `lua --run --path <file>` | 运行 Lua 脚本 |
| `skill --catalog` | 查看技能目录 (3项) |
| `skill --activate <name>` | 激活指定技能 |

## systemd 用户服务

安装 `.deb` 包后，systemd 用户服务文件已位于 `/usr/lib/systemd/user/esp-agent.service`。

```bash
# 启用登录自动启动
esp-agent service enable

# 启动/停止/状态
esp-agent service start
esp-agent service stop
esp-agent service status
```

## 数据目录

所有运行时数据存储在 `~/.esp-agent/`：

```
~/.esp-agent/
├── config.json              # Agent 配置
├── agent.sock               # CLI 连接用的 Unix Socket
├── agent.pid                # 进程 PID 文件
├── skills/                  # 技能注册表 + 技能文档 (.md)
├── scripts/builtin/         # Lua 脚本（自动加载）
├── router_rules/            # 事件路由规则
├── scheduler/               # 定时任务配置
├── sessions/                # 会话状态
├── memory/                  # 长期记忆存储
└── inbox/                   # IM 附件存储
```

## 架构概览

```
esp-agent/
├── esp-claw/            # 上游 esp-claw 仓库（只读，不修改）
│   └── components/      # 原始 ESP32 源码
├── sim_hal/             # 桌面模拟层
│   ├── include/         # ESP-IDF / FreeRTOS 头文件桩
│   │   ├── esp/         #   esp_err, esp_log, esp_console, ...
│   │   ├── freertos/    #   FreeRTOS.h, task.h, queue.h, ...
│   │   └── argtable3/   #   argtable3 最小桩
│   ├── freertos_shim.c  # FreeRTOS → pthread 实现
│   ├── console_unix.c   # esp_console → Unix Socket REPL
│   ├── http_curl.c      # LLM HTTP 传输 (libcurl)
│   ├── display_sdl2.c   # 显示屏模拟 (SDL2)
│   ├── nvs.c            # NVS 存储模拟 (cJSON)
│   └── cJSON.c          # JSON 解析库
├── main_desktop.c       # 桌面程序入口
├── CMakeLists.txt       # CMake 构建系统
├── esp-agent            # CLI 管理脚本
├── package.sh           # .deb 打包脚本
├── packaging/           # Debian 包结构
└── README.md
```

### FreeRTOS → POSIX 映射

| FreeRTOS API | Linux 等效实现 |
|-------------|--------------|
| `xTaskCreate` | `pthread_create` |
| `vTaskDelete` | `pthread_cancel` + `pthread_join` |
| `vTaskDelay` | `usleep` |
| `xQueueCreate/Send/Receive` | 环形缓冲区 + `pthread_mutex_t` + `pthread_cond_t` |
| `xSemaphoreCreateMutex/Take/Give` | `pthread_mutex_t` + `pthread_cond_t` |
| `xTaskGetTickCount` | `gettimeofday` → 毫秒 |

### 已启用的能力模块

| 模块 | 功能 | 状态 |
|------|------|------|
| cap_im_local | 本地 Web 即时通讯界面 | 已启用 |
| cap_files | 文件系统读写操作 (6 项能力) | 已启用 |
| cap_lua | Lua 脚本执行引擎 (8 项能力) | 已启用 |
| cap_skill_mgr | 技能注册/激活/停用 (5 项能力) | 已启用 |
| cap_router_mgr | 事件路由规则管理 (6 项能力) | 已启用 |
| cap_session_mgr | 会话管理 | 已启用 |
| claw_memory | 长期记忆存储与提取 (5 项能力) | 已启用 |

## 常用操作示例

```bash
# 查看所有注册的能力
$ esp-agent connect
app> cap list

# 向 LLM 发送消息
app> ask 你好，请介绍一下你自己

# 让 Agent 操作文件
app> ask 请列出当前目录的所有文件

# 让 Agent 记住信息
app> ask 请帮我记住：我最喜欢的颜色是蓝色

# 运行 Lua 脚本
app> lua --run --path hello.lua

# 查看记忆
app> cap call memory_list {}

# 切换会话
app> session my-new-session
```

## 常见问题

**Q: 启动时报 `Failed to start app_claw` 错误？**

检查 `~/.esp-agent/` 目录权限是否正确。可以尝试删除该目录后重新运行。

**Q: 连接 CLI 时报 `Agent is not running`？**

先运行 `esp-agent start` 启动服务，用 `esp-agent status` 确认状态。

**Q: 没有设置 LLM API Key 能运行吗？**

可以。Agent 框架（事件路由、能力注册、技能管理、CLI 等）会正常启动，对话功能（`ask`）和记忆提取不可用。

**Q: SDL2 窗口没有出现？**

检查 `~/.esp-agent/config.json` 中 `display.enabled` 是否为 `true`。

**Q: 如何切换不同的 LLM 服务商？**

修改 `config.json` 中的 `profile` 和 `base_url` 即可。见上方"配置 LLM"章节的示例。

**Q: 如何卸载？**

```bash
# 先停止所有运行中的实例
esp-agent stop
# 卸载
sudo apt remove esp-agent
# 可选：删除用户数据
rm -rf ~/.esp-agent
```
