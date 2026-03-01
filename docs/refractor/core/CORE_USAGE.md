# Core 基础设施模块使用文档

本文档介绍了 `src/core` 目录下基础设施模块（ConfigManager 和 Logger）的技术方案、设计原因以及在后续开发中的使用方法。

---

## 1. ConfigManager (统一配置管理器)

### 1.1 技术方案与设计原因

**技术方案**：
- 基于 `yaml-cpp` 的 Header-only 单例类。
- **存储模型 (Solution A)**：使用 `std::unordered_map<std::string, YAML::Node>` 按命名空间独立存储配置树。
- **线程安全**：使用 `std::shared_mutex` 实现读写锁（读多写少）。

**为什么选择这种方案？**
1. **为什么用单例？**
   比赛代码生命周期简单，配置对象整个程序只需要一份。单例比全局变量多一层懒初始化保证，比依赖注入少大量样板代码。
2. **为什么用 YAML？**
   YAML 支持注释，方便调参时在文件里写备注；`yaml-cpp` 已是项目依赖（Foxglove 传递引入），无需额外安装；相比之下，JSON 不支持注释，XML 层级冗余且解析慢。
3. **为什么要命名空间隔离（Solution A）？**
   `yaml-cpp` 的 `YAML::Node` 是引用计数句柄，"赋值"只改变句柄指向，不修改原树节点。尝试用单棵树+递归合并的方案均失败。改为每个命名空间独立持有一棵树，写入时通过 `dest[key]=val` 对已持有的树节点赋值，彻底绕开这个陷阱，同时还顺便实现了命名空间隔离。
4. **为什么用 `shared_mutex`？**
   配置在运行时几乎只读（写只发生在启动和热重载两个时机），`shared_mutex` 允许多线程并发读，只有 Load/Reload 时独占写，读操作零锁争用。

### 1.2 快速上手与使用指南

**1. 引入头文件**
```cpp
#include "core/config.hpp"
```

**2. 加载配置 (通常在 main 函数初始化时调用)**
```cpp
auto& cfg = mv::ConfigManager::Instance();

// 加载到根命名空间（全局配置）
cfg.Load("configs/vision.yaml");

// 加载到特定命名空间（模块配置隔离）
cfg.Load("configs/armor/basic_armor.yaml", "armor"); 
```

**3. 读取配置**
```cpp
auto& cfg = mv::ConfigManager::Instance();

// 方式一：安全读取（找不到时返回默认值，绝不抛异常）
// 适用于频繁调用的参数读取
auto color = cfg.Get<std::string>("auto_aim.enemy_color", "red");
auto thresh = cfg.Get<int>("armor.light_thresh", 100);

// 方式二：强制读取（必须存在，不存在抛 std::out_of_range）
// 适用于启动时必须有的硬依赖参数（如相机分辨率、串口波特率）
auto fps = cfg.GetRequired<int>("camera.fps");

// 方式三：检查键是否存在
if (cfg.Has("armor.debug_mode")) {
    // ...
}
```

**4. 获取子树 (传递给子模块)**
```cpp
// 获取 "auto_aim.tracker" 下的所有配置，返回一个只读的 YAML::Node 副本
YAML::Node tracker_cfg = cfg.Subtree("auto_aim.tracker");
// 子模块可以自己解析这个 Node
```

**5. 热重载**
```cpp
// 重新读取所有已加载的文件，刷新内存中的配置
// 注意：重载期间持有独占写锁，建议在独立的管理线程中接收信号后调用（如 SIGUSR1）
cfg.Reload();
```

---

## 2. Logger (统一日志系统)

### 2.1 技术方案与设计原因

**技术方案**：
- 基于 `spdlog` 的单例封装。
- 支持控制台彩色输出 + 文件输出（自动按日期命名）。
- 提供带模块前缀的格式化宏（如 `[armor] message`）。

**为什么选择这种方案？**
1. **为什么封装 `spdlog` 而不直接用？**
   `spdlog` 的全局函数（`spdlog::info()` 等）共享一个全局 logger，各模块混用时日志格式不统一，也无法在不改调用点的情况下整体控制级别。封装后，所有日志都经过 `[module]` 前缀格式化，可以在 grep 时精确过滤来源模块。
2. **为什么用单例？**
   日志是横切关注点（cross-cutting concern），每个模块都需要，如果作为参数传递会导致所有构造函数都多一个 Logger 参数。
3. **为什么 `Init` 分离出去（不在构造函数里初始化）？**
   构造函数里无法控制日志目录路径和级别，而这两个参数需要从配置文件读取。先构造单例（零开销），等 ConfigManager 加载完配置后再调用 `Init()`，顺序依赖关系更清晰。未调用 `Init` 时会懒初始化到默认值，保证不崩溃。
4. **为什么分离到 `.cpp`？**
   `spdlog` 的 sink 头文件包含大量模板实现，放在 `.hpp` 里会导致每个 include `logger.hpp` 的翻译单元都编译一遍这些模板，显著增加编译时间。分离后只有 `logger.cpp` 编译一次。

### 2.2 快速上手与使用指南

**1. 引入头文件**
```cpp
#include "core/logger.hpp"
```

**2. 初始化 (在 main 函数中，ConfigManager 加载配置之后调用)**
```cpp
// 从配置中读取日志参数
auto& cfg = mv::ConfigManager::Instance();
std::string log_dir = cfg.Get<std::string>("system.log_dir", "logs");
// ... 解析 log_level ...

// 初始化 Logger
mv::Logger::Instance().Init(log_dir, spdlog::level::debug, true);
```

**3. 打印日志 (推荐使用宏)**
```cpp
// 宏的第一个参数是模块名，用于过滤日志来源
// 后面是 fmt 格式化字符串和参数
MV_LOG_TRACE("tracker", "处理耗时: {} ms", 1.2);
MV_LOG_DEBUG("tracker", "预测坐标 ({}, {})", x, y);
MV_LOG_INFO("armor",  "检测到 {} 块装甲板", count);
MV_LOG_WARN("serial", "串口校验失败，重试...");
MV_LOG_ERROR("cam",   "相机打开失败: {}", err_msg);
MV_LOG_CRITICAL("sys", "发生致命错误，程序即将退出");
```

**4. 动态修改日志级别**
```cpp
// 运行时修改控制台输出级别（调试时临时开启 trace，比赛时切换到 warn）
mv::Logger::Instance().SetLevel(spdlog::level::warn);
```

---

## 3. 在后续开发中的最佳实践

1. **禁止直接使用 `YAML::LoadFile` 和 `spdlog::info`**：所有配置读取必须通过 `ConfigManager`，所有日志打印必须通过 `MV_LOG_*` 宏。
2. **配置键名规范**：使用 `snake_case`，层级之间用 `.` 分隔。例如 `auto_aim.tracker.min_detect_count`。
3. **模块名规范**：在日志宏中，模块名应简短且具有辨识度，如 `"cam"`, `"serial"`, `"armor"`, `"tracker"`, `"fsm"` 等。
4. **CMake 依赖**：新模块的 `CMakeLists.txt` 中，只需 `target_link_libraries(your_target mv-core)` 即可使用配置和日志功能。