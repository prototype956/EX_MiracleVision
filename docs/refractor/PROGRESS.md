# 重构进展追踪

> 最后更新：2026-03-01
> 分支：`refactor/core-infra`

---

## 总体规划

```
Stage 1 ─ 基础设施          ✅ 完成
Stage 2 ─ 硬件抽象层 (HAL)  ✅ 完成
Stage 3 ─ 接口层 + 工厂      🔲 待开始
Stage 4 ─ 线程 Pipeline      🔲 待开始
Stage 5 ─ 状态机 (FSM)       🔲 待开始
Stage 6 ─ 替换旧模块         🔲 待开始
```

---

## Stage 1：基础设施 ✅

**提交**：`76269c8`（代码） / `fe1ddf5`（文档）

### 交付物

| 文件 | 说明 |
|------|------|
| `src/core/config.hpp` | ConfigManager 单例，YAML 多命名空间，线程安全读写 |
| `src/core/logger.hpp` | Logger 单例声明，`MV_LOG_*` 宏 |
| `src/core/logger.cpp` | spdlog 初始化，控制台 + 文件双输出 |
| `src/app/smoke_test.cpp` | 15 项冒烟测试 |
| `configs/vision.yaml` | 全局配置入口 |

### 关键设计决策

- **Solution A（命名空间独立存储）**：`yaml-cpp` 的 `YAML::Node` 是引用计数句柄，递归合并到单棵树会静默失败。改为 `unordered_map<string, YAML::Node>` 按命名空间独立持有，彻底绕开此陷阱。
- **`shared_mutex` 读写锁**：配置运行时几乎只读，允许多线程并发读，Load/Reload 独占写。
- **Logger 懒初始化**：构造函数零开销，`Init()` 分离出去等 ConfigManager 加载完再调用，未调用时有默认行为，不崩溃。

### 测试状态

```
15 / 15 冒烟测试通过 ✓
```

---

## Stage 2：硬件抽象层（HAL） ✅

**提交**：`d2b4a63`（初始实现） / `67c4245`（lint 修复） / `b07f9d4`（命名规范修复）

### 交付物

| 文件 | 说明 |
|------|------|
| `src/hal/camera/i_camera.hpp` | `ICamera` 纯虚接口 |
| `src/hal/camera/mindvision_camera.hpp/.cpp` | MindVision 工业相机（Pimpl） |
| `src/hal/camera/opencv_camera.hpp/.cpp` | OpenCV 相机（Pimpl） |
| `src/hal/serial/i_serial.hpp` | `ISerial` 纯虚接口 |
| `src/hal/serial/uart_serial.hpp/.cpp` | Linux UART 串口（Pimpl，termios） |
| `src/hal/camera/CMakeLists.txt` | `mv-hal-camera` 静态库 |
| `src/hal/serial/CMakeLists.txt` | `mv-hal-serial` 静态库 |

### 关键设计决策

- **Pimpl 模式**：`CameraApi.h` / `termios.h` 完全隔离在 `.cpp` 里，头文件零 SDK 依赖。修改 SDK 用法只重编一个 `.cpp`。
- **MindVision 桩实现**：`MV_HAS_MVSDK` 未定义时编译为始终返回 `false` 的桩，CI 无硬件也能编译全部代码。
- **分拆为两个库**：不需要相机的目标（如纯串口测试）无需链接 OpenCV；串口库零外部依赖，可独立 mock。
- **非阻塞串口**：`VTIME=0 VMIN=0`，Pipeline 线程可定期检查退出标志，实现干净退出。
- **ICamera/ISerial Rule of Five（C.67）**：拷贝 `= delete`，移动降为 `protected default`，防止对象切片，同时保留派生类移动能力。

### 与旧代码对比

| 方面 | 旧代码（`devices/`）| 新 HAL（`src/hal/`）|
|------|-------------------|-------------------|
| SDK 依赖范围 | 渗透到算法层 | 隔离在 `.cpp` 内 |
| 无硬件编译 | ✗ 依赖 CameraApi.h | ✓ 桩实现，零 SDK 依赖 |
| 协议与 I/O | 混写在同一个类 | I/O 和协议完全分离 |
| 配置来源 | cv::FileStorage | yaml-cpp（与 ConfigManager 统一） |
| 测试隔离 | 需要真实硬件 | 可注入 MockCamera/MockSerial |

### 编译状态

```
mv-hal-camera  ✓ 零警告零错误
mv-hal-serial  ✓ 零警告零错误
Stage 1 冒烟测试：15 / 15 仍全部通过 ✓
```

---

## Stage 3：接口层 + 工厂系统 🔲

**计划**：下一阶段

### 目标

```
src/interfaces/   ← 业务接口（IDetector, IPredictor, ITracker...）
src/factory/      ← 模板工厂注册表
```

### 主要工作

1. **`IDetector` 接口**：统一装甲板/能量机关检测器的调用接口
2. **模板工厂**：通过字符串键注册和创建具体实现
   ```cpp
   // 注册
   Factory<ICamera>::Register<MindVisionCamera>("mindvision");
   Factory<ICamera>::Register<OpenCvCamera>("opencv");
   // 创建
   auto cam = Factory<ICamera>::Create("mindvision");
   ```
3. **配置驱动**：从 YAML 中读取 `type` 字段，工厂根据类型名创建对应实现
4. **数据类型定义**：统一 `DetectionResult`、`PredictionResult` 等跨层数据结构

---

## Stage 4：线程 Pipeline 🔲

### 目标

```
src/pipeline/
├── pipeline.hpp    ← Pipeline 管理器
├── node.hpp        ← Pipeline 节点基类
└── thread_pool.hpp ← 线程池
```

### 主要工作

- 基于 `ThreadSafeQueue`（已有，`utils/thread_safe_queue.hpp`）设计节点间数据流
- 相机采集、检测、预测、串口通信各自独立线程
- 统一的启动/停止/重启机制

---

## Stage 5：状态机（FSM） 🔲

### 目标

```
src/fsm/
├── state.hpp       ← 状态基类
├── state_machine.hpp ← 状态机模板
└── states/         ← 具体状态（AutoAim, Buff, Calibration...）
```

### 主要工作

- 替换原有的 `RunMode` 枚举 + `switch-case`
- 状态转换由 FSM 驱动，业务逻辑集中在各 State 类中

---

## Stage 6：替换旧模块 🔲

逐步用新模块替换 `devices/`、`module/`、`base/` 下的旧代码，最终删除旧目录。

替换顺序建议（依赖关系从浅到深）：
1. `devices/camera/` → `src/hal/camera/`（已完成接口，待工厂和 Pipeline 就绪后替换调用点）
2. `devices/serial/` → `src/hal/serial/`（同上）
3. `module/armor/` → 新检测器接口（待 Stage 3）
4. `base/MiracleVision.cpp` → 新 Pipeline 主程序（待 Stage 4）

---

## 已知问题 / 技术债

| 文件 | 问题 | 优先级 |
|------|------|--------|
| `src/app/smoke_test.cpp` | `main()` 认知复杂度超阈值（47 > 25），`files.size() >= 1` 应为 `!files.empty()` | 低（测试代码） |
| `module/foxglove_publisher/foxglove_publisher.cpp` | `schemas.hpp` 类型在 `namespace foxglove::schemas` 内失效，编译报错 | 中（旧代码，不影响新模块） |
