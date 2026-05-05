# Windows 迁移计划

## 概述

将 crush-claw 从 Linux 桌面模拟器迁移到 Windows。当前代码大量依赖 POSIX/Linux 特定 API（`/proc` 文件系统、Unix domain socket、`pthread`、`sysconf`、`getifaddrs` 等），需要替换为 Windows API。

## 架构决策

### 编译器：MinGW-w64（MSYS2 环境）
- 当前环境已是 MINGW64，可直接使用
- MinGW 支持大部分 POSIX API，但 `/proc`、`AF_UNIX`、`daemon()` 等不可用
- 对于 MinGW 不支持的 API，使用 Win32 API 替代
- 构建系统：CMake + MinGW Makefiles

### IPC 方案：Windows Named Pipes 替代 Unix Domain Socket
- `console_unix.c` → `console_win.c`（或 `#ifdef _WIN32` 条件编译）
- Named Pipe 路径：`\\.\pipe\crush-claw` 替代 `~/.crush-claw/agent.sock`

### CLI 工具：C 语言编译（`crush-claw.exe`）
- 编译为独立 .exe，作为守护进程的管理客户端
- 通过 Named Pipe 与守护进程通信
- 替代原 Bash 脚本的 config/start/stop/status/logs/build/ask 子命令

### 守护进程方案：独立后台进程
- 使用 `CreateProcess(DETACHED_PROCESS)` 实现类似 daemon 的后台进程
- 通过 PID 文件管理生命周期

### 字体方案：从程序目录 fonts/ 加载
- 字体文件随程序分发到 `fonts/` 子目录
- 100% 可控，不需要用户安装额外字体

### 数据目录
- `~/.crush-claw/` → `%USERPROFILE%\.crush-claw\`（即 `C:\Users\<user>\.crush-claw\`）

---

## 分步任务清单

### 第 1 步：CMakeLists.txt 改造

**文件：`CMakeLists.txt`**

改动点：
- 添加 `if(WIN32)` 条件分支
- 用 `find_path` / `find_library` 查找 Windows 版 SDL2、SDL2_ttf、libcurl、lua、json-c（MSYS2 的 pkg-config 仍可用，但需处理路径格式）
- 链接 Windows 系统库：`ws2_32`（Winsock）、`crypt32`、`iphlpapi`、`wlanapi`、`bcrypt`
- 移除 Linux 专属库：`pthread`（MinGW 内置）、`dl`（Windows 无）、`m`（MinGW 内置）
- 输出目标改为 `esp-claw-desktop.exe`

```cmake
# 示例条件分支
if(WIN32)
    target_link_libraries(esp-claw-desktop PRIVATE
        ws2_32 crypt32 iphlpapi wlanapi
    )
    set_target_properties(esp-claw-desktop PROPERTIES SUFFIX ".exe")
else()
    target_link_libraries(esp-claw-desktop PRIVATE
        pthread m dl ssl crypto
    )
endif()
```

---

### 第 2 步：平台抽象层 — 新增 `sim_hal/include/platform/`

创建平台抽象头文件，统一封装 POSIX/Windows 差异：

```
sim_hal/include/platform/
├── platform.h          # 主入口：检测平台，include 对应实现
├── platform_posix.h    # POSIX 平台实现（原代码抽取）
└── platform_win32.h    # Windows 平台实现
```

`platform.h` 提供以下抽象：

| 功能 | POSIX | Windows |
|------|-------|---------|
| 线程创建 | `pthread_create` | `CreateThread` |
| 互斥锁 | `pthread_mutex_t` | `CRITICAL_SECTION` |
| 休眠 | `usleep` | `Sleep` |
| 时间戳 | `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` |
| 主目录 | `getenv("HOME")` | `getenv("USERPROFILE")` |
| 数据目录 | `~/.crush-claw/` | `%USERPROFILE%\.crush-claw\` |
| 创建目录 | `mkdir(path, 0755)` | `_mkdir(path)` / `CreateDirectoryA` |
| 删除文件 | `unlink()` | `DeleteFileA()` |
| 路径分隔符 | `/` | `\`（内部统一使用 `/`，Windows API 也接受） |

---

### 第 3 步：`main_desktop.c` Windows 适配

**文件：`main_desktop.c`**

改动点：
1. **信号处理**：`signal(SIGINT/SIGTERM/SIGHUP)` → `SetConsoleCtrlHandler()`
2. **守护进程化**：Linux 的 `fork()` + `setsid()` →
   - 前台模式：直接运行（不变）
   - 后台模式：用 `CreateProcess(NULL, cmdline, NULL, NULL, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)` 启动子进程，当前进程退出
   - 通过 PID 文件管理生命周期
3. **PID 文件**：`getpid()` → `GetCurrentProcessId()`
4. **主目录**：`getenv("HOME")` → `getenv("USERPROFILE")`，fallback 为 `getenv("HOMEDRIVE") + getenv("HOMEPATH")`
5. **路径拼接**：路径分隔符使用 `\\`（或保持 `/`，Windows API 也接受）
6. **`realpath()`** → `_fullpath()` 或 `GetFullPathNameA()`
7. **`copy_tree()` 中的 `fork()` + `execlp("cp")`**：用 Win32 `SHFileOperationA()` 或手动递归复制
8. **`daemonize()` 重写**：`open("/dev/null")` → `NUL` 设备；`dup2()` → `_dup2()`；`chdir("/")` → `SetCurrentDirectoryA("C:\\")`；整个逻辑放在 `#ifdef _WIN32` 分支
9. **`#pragma GCC diagnostic`**：改为 `#ifdef __GNUC__` 条件包裹

---

### 第 4 步：`console_unix.c` → 支持 Named Pipes

**文件：`sim_hal/console_unix.c`**

方案：使用 `#ifdef _WIN32` 条件编译，保留 Unix socket 路径不变，新增 Named Pipe 路径。

核心差异：
```c
// POSIX：socket(AF_UNIX, SOCK_STREAM, 0) + bind + listen + accept
// Windows Named Pipe：
// CreateNamedPipe("\\.\pipe\crush-claw", PIPE_ACCESS_DUPLEX, ...)
// ConnectNamedPipe()
// 客户端用 CreateFile("\\.\pipe\crush-claw", ...) + WriteFile + ReadFile
```

One-shot 模式保持不变：读取一行命令 → 执行 → 返回结果 → 断开。

客户端 CLI (crush-claw CLI) 通过 `CreateFile` + `WriteFile` + `ReadFile` 与 daemon 通信。

---

### 第 5 步：`freertos_shim.c` — CPU/线程统计

**文件：`sim_hal/freertos_shim.c`（运行时统计部分，行 680-865）**

替换 `/proc/stat`、`/proc/self/task/`、`sysconf(_SC_CLK_TCK)` 为 Windows API：

| Linux | Windows |
|-------|---------|
| `opendir("/proc/self/task")` | `CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)` + `Thread32First/Next` |
| `/proc/self/task/<tid>/stat` | `OpenThread(THREAD_QUERY_INFORMATION)` + `GetThreadTimes()` |
| `/proc/stat` (CPU idle) | `GetSystemTimes()` |
| `sysconf(_SC_CLK_TCK)` | `GetSystemInfo()` 获取 `dwNumberOfProcessors` |

注意事项：
- Windows 下线程名通过 `SetThreadDescription()` 设置，需要 `GetThreadDescription()` 读取（Win10 1607+）
- 线程枚举需要 `TH32CS_SNAPTHREAD`，且需要处理权限
- `GetSystemTimes()` 返回 FILETIME（100ns 单位），需要转换为 ticks

---

### 第 6 步：系统信息头文件 — `/proc` → Win32 API

以下头文件包含 `static inline` 函数，直接读取 `/proc`：

#### 6a. `sim_hal/include/esp/esp_system.h`
- `proc_meminfo_read("MemAvailable:")` → `GlobalMemoryStatusEx(&mex)`, 取 `mex.ullAvailPhys`

#### 6b. `sim_hal/include/esp/esp_heap_caps.h`
- `_heap_proc_meminfo_read("MemTotal:")` → `GlobalMemoryStatusEx(&mex)`, 取 `mex.ullTotalPhys`
- `_heap_proc_meminfo_read("MemAvailable:")` → `GlobalMemoryStatusEx(&mex)`, 取 `mex.ullAvailPhys`
- `_heap_proc_meminfo_read("MemFree:")` → `GlobalMemoryStatusEx(&mex)`, 取 `mex.ullAvailPhys -` 缓存等（近似）

#### 6c. `sim_hal/include/esp/esp_chip_info.h`
- `sysconf(_SC_NPROCESSORS_CONF)` → `GetSystemInfo(&si)`, 取 `si.dwNumberOfProcessors`
- `/proc/cpuinfo` 读取 model name → Windows 注册表 `HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString`，或用 `GetSystemInfo` 的 `wProcessorArchitecture` 生成描述

#### 6d. `sim_hal/include/esp/esp_netif.h`
- `getifaddrs()` → `GetAdaptersAddresses(AF_INET, ...)` (iphlpapi)
- 链接 `iphlpapi.lib`

#### 6e. `sim_hal/include/esp/esp_wifi.h`
- `/proc/net/wireless` → `WlanOpenHandle()` + `WlanEnumInterfaces()` + `WlanQueryInterface(wlan_intf_opcode_rssi)` (wlanapi)
- 链接 `wlanapi.lib`
- 注册表读取 SSID：`HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Profiles\`
- 注意：需要 WLAN AutoConfig 服务运行

#### 6f. `sim_hal/include/esp/esp_timer.h`
- `clock_gettime(CLOCK_MONOTONIC)` → `QueryPerformanceCounter()` + `QueryPerformanceFrequency()`

#### 6g. `sim_hal/include/freertos/portmacro.h`
- `sysconf(_SC_NPROCESSORS_CONF)` → `GetSystemInfo(&si)`, 取 `si.dwNumberOfProcessors`
- 移除 `#include <unistd.h>`

---

### 第 7 步：`esp_websocket_client.c` — POSIX Socket → WinSock2

**文件：`sim_hal/esp_websocket_client.c`**

这是最复杂的文件。改动：

1. **头文件替换**：
   - `arpa/inet.h`, `netdb.h`, `netinet/in.h`, `netinet/tcp.h`, `sys/socket.h`, `sys/select.h`, `unistd.h`, `fcntl.h` → `winsock2.h`, `ws2tcpip.h`
   - `openssl/ssl.h`, `openssl/err.h` → 保持 OpenSSL（MinGW 支持）

2. **API 替换**：
   - `close(fd)` → `closesocket(fd)`
   - `fcntl(fd, F_SETFL, O_NONBLOCK)` → `ioctlsocket(fd, FIONBIO, &mode)`
   - `MSG_NOSIGNAL` → `0`（Windows 无 SIGPIPE）
   - `/dev/urandom` → `BCryptGenRandom()` 或 `CryptGenRandom()`
   - `select()` → 保持（WinSock2 支持 select，但仅用于 socket）

3. **WSAStartup**：
   - 在进程启动时调用 `WSAStartup(MAKEWORD(2,2), &wsaData)`（放在 `main_desktop.c`）
   - 在进程退出时调用 `WSACleanup()`

---

### 第 8 步：`display_sdl2.c` — SDL2 Windows 适配

**文件：`sim_hal/display_sdl2.c`**

改动：
1. **头文件路径**：`<SDL2/SDL.h>` → `<SDL.h>`（Windows SDL2 使用 flat include）
2. **字体路径**（从程序目录 `fonts/` 子目录加载）：
   - 使用 `GetModuleFileNameA()` 定位程序所在目录
   - 字体文件从 `<exe_dir>/fonts/` 加载：
     - `fonts/wqy-zenhei.ttc`（CJK 字体）
     - `fonts/DejaVuSans.ttf`（拉丁字体）
     - `fonts/NotoColorEmoji.ttf`（Emoji 字体）
   - 打包时字体文件随 exe 一同分发
3. **目录遍历**：`opendir/readdir/closedir` → `FindFirstFileA/FindNextFileA/FindClose`
4. **mkdir**：`mkdir(dir, 0755)` → `CreateDirectoryA(dir, NULL)`
5. **getenv("HOME")**：→ `getenv("USERPROFILE")`

---

### 第 9 步：其他 sim_hal 文件

#### `sim_hal/nvs_stub.c`
- `getenv("HOME")` → `getenv("USERPROFILE")`
- `mkdir()` → `CreateDirectoryA()` / `_mkdir()`
- 移除 `#include <sys/stat.h>`, `<unistd.h>`

#### `sim_hal/nvs.c`
- `getenv("HOME")` → `getenv("USERPROFILE")`

#### `sim_hal/emote_stub.c`
- `stat()`, `S_ISDIR()` → `GetFileAttributesA()` 或 `_stat()`
- 路径调整

#### `sim_hal/esp_mmap_assets_stub.c`
- `opendir/readdir` → `FindFirstFileA/FindNextFileA`
- `DT_DIR` → `FILE_ATTRIBUTE_DIRECTORY`

#### `sim_hal/http_curl.c` & `esp_http_client.c`
- libcurl 本身跨平台，只需处理 pthread → Windows 线程/CriticalSection

---

### 第 10 步：`crush-claw` CLI 工具 — C 语言重写

**新文件：`cli/crush_claw_cli.c`**（编译为 `crush-claw.exe`）

作为一个独立的 CMake 目标构建，与守护进程共享 Named Pipe 通信协议。

支持的子命令：

| 命令 | C 实现 |
|------|--------|
| `config` | 交互式配置向导（stdin/stdout），读写 `%USERPROFILE%\\.crush-claw\\config.json` |
| `start` | `CreateProcess(DETACHED_PROCESS, esp-claw-desktop.exe --daemon --pid-file ...)` |
| `stop` | 通过 Named Pipe 发送 shutdown 命令，或 `OpenProcess` + `TerminateProcess`（读 PID 文件） |
| `restart` | stop + start |
| `status` | 检查 PID 文件对应进程是否存在 + Named Pipe 是否可连接 |
| `logs` | `CreateFile` + `ReadFile` tail 日志文件 |
| `build` | `CreateProcess` 调用 `cmake --build` |
| `clean` | `RemoveDirectory` 删除 build/ |
| `service` | `OpenSCManager` + `CreateService` 注册 Windows 服务 |
| `ask` | 通过 Named Pipe 发送命令并读取响应 |

Client 通信方式（Named Pipe）：
```c
HANDLE hPipe = CreateFileA("\\\\.\\pipe\\crush-claw",
    GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
WriteFile(hPipe, cmdline, strlen(cmdline), &written, NULL);
// shutdown write end to signal EOF
ReadFile(hPipe, buf, sizeof(buf), &read, NULL);
CloseHandle(hPipe);
```

CMake 中新增构建目标：
```cmake
add_executable(crush-claw cli/crush_claw_cli.c)
set_target_properties(crush-claw PROPERTIES SUFFIX ".exe")
target_link_libraries(crush-claw PRIVATE ws2_32)
```

保留兼容批处理包装 `crush-claw.bat`（主要用于从 MSYS2 终端调用时处理路径）：
```batch
@echo off
"%~dp0crush-claw.exe" %*
```

---

### 第 11 步：`_run_desktop.bat` — Windows 启动脚本

**新文件：`_run_desktop.bat`**

功能：
- 检查 cmake 和 make 是否可用
- `cmake -B build -G "MinGW Makefiles"`
- `cmake --build build --config Release`
- `.\build\esp-claw-desktop.exe`

同时提供 MSYS2/MinGW shell 版本 `_run_desktop.sh`（仅需移除 Linux 特定依赖检查）。

---

### 第 12 步：打包脚本

`package.sh` 重写为 `package.bat`：
- 收集构建产物（`esp-claw-desktop.exe`、`crush-claw.exe`、依赖 DLL）
- 使用 `Compress-Archive`（PowerShell）或 `7z` 打包为 ZIP
- MSYS2 环境下可选保留 shell 版本 `package.sh`

`packaging/DEBIAN/` 目录 → `packaging/windows/`：
- 替换 systemd service 文件为 Windows Service 注册脚本
- 创建 `install.bat` 安装脚本（复制文件 + 注册服务）

---

### 第 13 步：文档更新

- `CLAUDE.md`：添加 Windows 构建说明
- `README.md`：更新安装步骤
- `components/desktop/README.md`：更新平台支持说明
- `FEATURE_SUPPORT.md`：标注平台差异

---

## 风险与注意事项

1. **WiFi API 需要 WLAN AutoConfig 服务**：如果服务未运行，WiFi 查询会失败
2. **字体依赖**：需要找到 Windows 兼容的中文 + Emoji 字体，或降级渲染
3. **Named Pipe 安全性**：Windows Named Pipes 需要设置合适的 ACL
4. **路径长度限制**：Windows 默认 MAX_PATH=260，需要 `\\?\` 前缀处理长路径
5. **MSYS2 vs 原生 Windows**：MinGW 编译的 .exe 可以在没有 MSYS2 的 Windows 上运行，但需要分发依赖的 DLL（libcurl、SDL2、lua 等）
6. **fork() 不可用**：`copy_tree()` 和 `daemonize()` 需要完全重写
7. **setenv()**：MinGW 支持，但 `_putenv_s()` 更标准

---

## 实现顺序

按依赖关系排序：

1. CMakeLists.txt（构建系统，最先需要）
2. 平台抽象头文件 `platform.h`
3. `portmacro.h`（简单，无依赖）
4. `esp_timer.h`（简单，无依赖）
5. `esp_system.h` + `esp_heap_caps.h`（内存信息）
6. `esp_chip_info.h`（CPU 信息）
7. `esp_netif.h`（网络信息）
8. `esp_wifi.h`（WiFi 信息）
9. `freertos_shim.c`（线程统计）
10. `nvs.c` + `nvs_stub.c`（存储路径）
11. `console_unix.c` → Named Pipes（`#ifdef _WIN32` 条件编译）
12. `esp_websocket_client.c` → WinSock2（`#ifdef _WIN32` 条件编译）
13. `display_sdl2.c`（SDL2 + 字体路径）
14. `main_desktop.c`（信号、守护进程、路径）
15. 其余 sim_hal 文件（emote_stub, esp_mmap_assets_stub, http_curl 等）
16. CLI 工具 `cli/crush_claw_cli.c`（编译为 crush-claw.exe）
17. `_run_desktop.bat` 启动脚本
18. `package.bat` 打包脚本
19. 文档更新（CLAUDE.md, README.md 等）
