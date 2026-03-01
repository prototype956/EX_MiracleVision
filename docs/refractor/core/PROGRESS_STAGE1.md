# 阶段一进度记录与问题分析

**分支**: `refactor/core-infra`
**日期**: 2026-02-26
**阶段**: 阶段一 —— 基础设施建设（ConfigManager + Logger + 目录重组）

---

## 一、已完成内容

### 1.1 目录结构重组 ✅

新建了 `src/` 目录作为重构代码的根，与旧代码并存，互不干扰：

```
src/
├── core/           ← 阶段一：基础设施
│   ├── config.hpp  ← ConfigManager（header-only）
│   ├── logger.hpp  ← Logger 接口
│   ├── logger.cpp  ← Logger 实现
│   └── CMakeLists.txt
├── app/
│   ├── smoke_test.cpp   ← 冒烟测试
│   └── CMakeLists.txt
├── hal/            ← 阶段二（空目录，待实现）
├── interfaces/     ← 阶段三（空目录，待实现）
├── factory/        ← 阶段三（空目录，待实现）
├── pipeline/       ← 阶段四（空目录，待实现）
├── fsm/            ← 阶段五（空目录，待实现）
└── CMakeLists.txt
```

根 `CMakeLists.txt` 已接入 `add_subdirectory(src)`，旧有 `utils/`、`devices/`、`module/` 完全保留未动。

---

### 1.2 Logger ✅（功能正常）

**文件**: `src/core/logger.hpp` + `src/core/logger.cpp`

**功能**：
- 基于 `spdlog` 的单例封装，全局唯一实例
- 控制台彩色输出 + 自动按时间命名的文件输出（`logs/YYYY-MM-DD_HH-MM-SS.log`）
- 带模块前缀的格式化接口：`MV_LOG_INFO("module", "msg {}", val)`
- 懒初始化兜底：未显式调用 `Init()` 时自动初始化
- 支持运行时动态修改日志级别

**冒烟测试结果**：✅ 通过

```
[2026-02-26 18:31:34.746] [info]    [smoke_test] Logger 初始化成功
[2026-02-26 18:31:34.746] [debug]   [smoke_test] 调试信息: value = 42
[2026-02-26 18:31:34.746] [warning] [smoke_test] 这是一条警告
```

---

### 1.3 ConfigManager ✅（完成）

**文件**: `src/core/config.hpp`（header-only 单例）

**已实现的接口设计**：
- `Load(path, namespace)` —— 加载 YAML 文件到指定命名空间
- `Reload()` —— 热重载所有已加载文件（原子替换，中途抛出不破坏原数据）
- `Has(key_path)` —— 检查点分隔路径是否存在
- `Get<T>(key_path, default_val)` —— 类型安全读取，失败返回默认值
- `GetRequired<T>(key_path)` —— 必须存在的键，不存在抛 `std::out_of_range`
- `Subtree(ns_path)` —— 获取子树的只读副本
- `LoadedFiles()` —— 返回已加载文件列表

**存储模型（Solution A）**：
使用 `std::unordered_map<std::string, YAML::Node> namespaces_` 存储，每个命名空间
持有独立的 `YAML::Node` 树。写入时直接对 map 中存储的节点赋值，规避了
yaml-cpp 值语义陷阱（详见第二章）。

**冒烟测试结果**：✅ 全部通过（15/15）

```
[PASS] cfg.Has("system.log_dir")
[PASS] cfg.Has("auto_aim.enemy_color")
[PASS] !cfg.Has("this.key.does.not.exist")
[PASS] log_dir == "logs"
[PASS] enemy_color == "red"
[PASS] min_count == 5
[PASS] yaw_offset == -2.0
[PASS] auto_fire
[PASS] missing == 999
[PASS] color == "red"
[PASS] threw
[PASS] cfg.Has("aim.enemy_color")     ← 命名空间隔离
[PASS] aim_color == "red"
[PASS] cfg.Has("auto_aim.enemy_color") ← Reload() 后数据保持
[PASS] files.size() >= 1
```

---

### 1.4 统一配置文件 `configs/vision.yaml` ✅

新增了全局配置入口文件，整合了原先散落在多个 `.xml` 中的参数：

```yaml
system:    { log_dir, log_level, log_console }
camera:    { backend, device_index, resolution, exposure, video_path }
serial:    { device, baudrate, show_info }
auto_aim:  { enemy_color, tracker, aimer, shooter }
calibration: { camera_matrix, distort_coeffs, R/t 外参 }
```

---

## 二、当前阻塞问题：yaml-cpp 节点写入语义

### 问题描述

`ConfigManager::Load()` 加载文件后，`root_` 没有实际写入数据，导致所有 `Has()` / `Get()` 返回空。

### 根本原因

经过多次尝试和隔离测试，确认了 **yaml-cpp `YAML::Node` 的引用语义与直觉不符**：

| 操作 | 预期 | 实际行为 |
|---|---|---|
| `YAML::Node b = a; b["key"] = val;` | 修改 `a` | **只修改 `b` 的局部副本，`a` 不变** |
| `void fn(YAML::Node dest) { dest["k"]=v; }` | 写入调用方的树 | **`dest` 是值拷贝，写入不回传** |
| `void fn(YAML::Node dest) { dest.reset(other); }` | 替换节点内容 | **`reset` 仅影响局部句柄** |
| `root_["a"]["b"] = val;` 直接在成员变量上操作 | 修改成功 | ✅ **有效** |

**结论**：yaml-cpp 的 `YAML::Node` 看起来像引用语义，但将 Node 赋值给局部变量后，对局部变量的**重新赋值**（`cur = something`）会断开与原树的连接。只有通过**原始 Node 变量直接调用 `operator[]`** 才能真正修改树。

### 已尝试的方案

1. **按值传参 `DeepMerge(YAML::Node dest, ...)`** —— 写入不回传 ❌
2. **`cur = cur[part]` 钻入后调用 `RecursiveMerge(cur, src)`** —— cur 已脱离 root_ 引用 ❌
3. **`dest.reset(from)`** —— reset 无法替换原树节点 ❌
4. **遍历 from 后用 `dest[key] = item.second`** —— 顶层 Map 合并无效 ❌

### 已采用的解决方案（方案 A）✅

不再用单个 `YAML::Node root_` 存储所有配置，改为用
`std::unordered_map<std::string, YAML::Node> namespaces_` 按命名空间独立存储。

- 每个 `namespaces_[ns]` 都是独立的 YAML 树的根节点。
- `FlatMerge(namespaces_[ns], incoming)` 直接对 **map 里存储的节点引用**赋值，写入永久生效。
- `Resolve(key_path)` 两步查找：先按命名空间前缀匹配，再回落到根命名空间 `""`。
- `Reload()` 使用临时 map 原子替换，中途抛出不影响原始数据。

---

## 三、cmake 构建库 `mv-core` ✅

```
[ 50%] Building CXX object src/core/CMakeFiles/mv-core.dir/logger.cpp.o
[100%] Linking CXX static library libmv-core.a
```

`mv-core` 静态库编译通过，其他模块通过 `target_link_libraries(xxx mv-core)` 使用。

---

## 四、阶段一总结 ✅ 完成

**完成时间**: 2026-02-26

所有阶段一目标均已达成并通过冒烟测试，已提交至 `refactor/core-infra` 分支。

### 下一阶段：阶段二 —— HAL 硬件抽象层

| 目标 | 文件 |
|---|---|
| 相机纯虚接口 | `src/hal/camera/i_camera.hpp` |
| MindVision Pimpl 封装 | `src/hal/camera/mindvision_camera.hpp/cpp` |
| OpenCV 相机 Pimpl 封装 | `src/hal/camera/opencv_camera.hpp/cpp` |
| 串口纯虚接口 | `src/hal/serial/i_serial.hpp` |
| 串口 Pimpl 重构 | `src/hal/serial/uart_serial.hpp/cpp` |
| 相机工厂（配置驱动） | `src/factory/camera_factory.hpp` |

---

## 五、文件清单

| 文件 | 状态 | 说明 |
|---|---|---|
| `src/core/config.hpp` | ✅ 完成 | ConfigManager，Solution A，15/15 测试通过 |
| `src/core/logger.hpp` | ✅ 完成 | Logger 接口 + 便捷宏 |
| `src/core/logger.cpp` | ✅ 完成 | Logger spdlog 实现 |
| `src/core/CMakeLists.txt` | ✅ 完成 | 编译为 mv-core 静态库 |
| `src/CMakeLists.txt` | ✅ 完成 | src/ 目录入口，各阶段注释占位 |
| `src/app/smoke_test.cpp` | ✅ 完成 | 阶段一冒烟测试，15/15 全绿 |
| `src/app/CMakeLists.txt` | ✅ 完成 | 冒烟测试可执行文件 |
| `src/app/smoke_test.cpp` | ⚠️ 等待修复 | 冒烟测试，Logger部分通过 |
| `configs/vision.yaml` | ✅ 完成 | 新版全局统一配置文件 |
| `CMakeLists.txt` | ✅ 完成 | 已接入 add_subdirectory(src) |
