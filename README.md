# 🦞 Crush Claw

> 一个安全、可爱、属于两个人的桌面小伙伴。

Crush Claw 运行在你对象（或朋友、伴侣）的电脑上，你通过 AI 可以远程控制屏幕上的一只小龙虾 —— 让它绘制彩虹、告白动画、迷你游戏，或者只是静静地趴在那里陪伴。

**给枯燥的研究生生活添点色彩，给打工人摸鱼的桌面来点惊喜。**

---

## 它是怎么玩的？

```
你（远程）              你对象的电脑
    │                        │
    ├─ 不说话 → ─────────── 小龙虾窗口最小化 or 置顶陪伴
    │                         └─ 由对象自己决定
    │
    ├─ 发消息 ────────────── 小龙虾保持或隐藏
    │  "画一颗爱心"           └─ Lua 绘图窗口自动弹出
    │                          └─ 全屏极简画布
    │                          └─ 键盘/鼠标可互动
    │                          └─ 绘制结束 → 绘图窗口关闭
    │                          └─ 小龙虾恢复原先的显隐状态
    │
    └─ 对象嫌你太烦 ──────── 任务栏右键 "始终隐藏"
                              └─（希望你不会让对象走到这一步）
```

---

## 特色

| 特性                       | 说明                                                                             |
| -------------------------- | -------------------------------------------------------------------------------- |
| 🦞**小龙虾动画**     | Emote 动画引擎驱动的桌面吉祥物，支持多种情绪和动作                               |
| 🎨**远程绘画**       | AI 通过 Lua 脚本在对方的屏幕上绘制图案、文字、小游戏                             |
| 🛡️**沙盒安全**     | Lua 脚本引擎完全隔离（`io/os/load/stdin` 全部封禁，10MB 内存硬限制，路径锚定） |
| ⌨️**键鼠交互**     | 支持键盘和鼠标输入，小龙虾可以回应互动                                           |
| 🪟**双窗口智能显隐** | 小龙虾桌宠始终置顶常伴；折叠后不影响绘图弹窗；绘图结束自动恢复                   |
| 🌐**多 IM 渠道**     | QQ、Telegram、微信、飞书、Web IM 均可接入                                        |
| 💻**跨平台**         | Windows + Linux                                                                  |

---

## 快速开始

### 给对象安装

```cmd
REM 1. 在你的电脑上一键安装
esp-agent-setup-1.0.0.exe

REM 2. 把配置文件发给对象
REM 把你的 LLM Token 和 IM Channel Key 写入 config.json
```

### 配置文件 （config.json）

```json
{
  "llm": {
    "api_key": "sk-d**********************c",
    "model": "deepseek-v4-flash",
    "profile": "anthropic",
    "base_url": "https://api.deepseek.com/anthropic",
    "auth_type": "",
    "timeout_ms": "120000",
    "max_tokens": "409600"
  },
  "channels": {
    "local_im": {
      "enabled": true
    },
    "qq": {
      "enabled": true,
      "app_id": "1*******4",
      "app_secret": "J4********************Pg"
    },
    "telegram": {
      "enabled": false,
      "bot_token": ""
    },
    "feishu": {
      "enabled": false,
      "app_id": "",
      "app_secret": ""
    },
    "wechat": {
      "enabled": false,
      "token": "",
      "base_url": "",
      "cdn_base_url": "",
      "account_id": ""
    }
  },
  "search": {
    "brave_key": "",
    "tavily_key": ""
  },
  "display": {
    "enabled": true,
    "lcd_width": 480,
    "lcd_height": 480,
	"emote_text": "zz6zz666!"
  }
}

```

### 启动

```cmd
REM 对象收到配置文件后，放入数据目录
start %USERPROFILE%\.crush-agent\config.json

REM 启动小龙虾
esp-agent start
```

看到屏幕上的小龙虾了？现在你可以通过 IM 向它发送指令了。

---

## 安全说明

**Crush Claw 将沙箱安全性作为第一要务。** 所有用户提交的 Lua 脚本都在一个深度隔离的虚拟环境中执行：

```
❌ os.execute()          — 任意命令执行
❌ io.open()             — 任意文件读写
❌ load() / loadfile()   — 动态代码注入
❌ debug.* (除 traceback) — VM 内部操纵
❌ string.dump()         — 字节码逃逸
❌ package.loadlib()     — C 模块加载
❌ storage 路径穿越       — 超出数据目录
❌ capability.call 外能力 — 只读白名单
✅ 内存分配硬限制 10MB    — 防内存耗尽
✅ 执行超时 60 秒        — 防死循环
```

详细参看 [SANDBOX_SECURITY.md](SANDBOX_SECURITY.md)。

> ⚠️ **重要须知**：使用 Crush Claw 需要双方相互信任。你的 LLM API Key 和 Channel Token 会存储在对方的电脑上。请确保：
>
> 1. 你不会恶意利用 Lua 脚本寻找漏洞攻击对方电脑
> 2. 对方不会盗用你的 API Key
> 3. 沙箱机制目前较为完善，但不排除存在尚未检测到的漏洞

---

## CLI 命令

| 命令                                    | 说明           |
| --------------------------------------- | -------------- |
| `esp-agent config`                    | 交互式配置向导 |
| `esp-agent start`                     | 后台启动 Agent |
| `esp-agent stop`                      | 停止 Agent     |
| `esp-agent restart`                   | 重启 Agent     |
| `esp-agent status`                    | 查看运行状态   |
| `esp-agent logs`                      | 查看日志       |
| `esp-agent service enable\|disable`    | 开机自启管理   |
| `esp-agent --help`                    | 查看全部命令   |
| `esp-agent ask "你好"`                | 远程对话       |
| `esp-agent lua --run --path demo.lua` | 运行 Lua 脚本  |
| `esp-agent cap list`                  | 查看能力列表   |

---

## 数据目录

```
%USERPROFILE%\.crush-agent/
├── config.json              # 配置文件（含你的 Key）
├── agent.sock               # IPC 通道
├── agent.pid                # PID 文件
├── scripts/builtin/         # Lua 脚本
├── skills/                  # 技能文档
├── memory/                  # 记忆存储
└── sessions/                # 会话记录
```

---

## 系统要求

- **Windows**: Windows 10+ x64
- **Linux**: Ubuntu 22.04+ / Debian 12+ x86_64

---

## 从源码构建

```bash
git clone <repo-url> && cd esp-agent
git submodule update --init

# 依赖安装见 CLAUDE.md
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)        # Linux
# mingw32-make -j4     # Windows
```

---

## 架构

```
crush-agent/
├── esp-claw/            # 上游 AI Agent 引擎（只读）
├── sim_hal/             # 桌面模拟层 + Lua 沙箱
├── cli/                 # CLI 管理工具
├── installer/           # Inno Setup 安装器
└── main_desktop.c       # 桌面入口
```

---

Made with ❤️ for someone special.
