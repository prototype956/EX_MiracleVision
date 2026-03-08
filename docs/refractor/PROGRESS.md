# 重构进展追踪

> 最后更新：2026-03-07
> 分支：`refactor/core-infra`

---

## 总体规划

```
Stage 1 ─ 基础设施          ✅ 完成
Stage 2 ─ 硬件抽象层 (HAL)  ✅ 完成
Stage 3 ─ 接口层 + 工厂      ✅ 完成
Stage 4 ─ 线程 Pipeline      ✅ 完成
Stage 5 ─ 状态机 (FSM)       ✅ 完成
Stage 6 ─ 替换旧模块 + main  🔶 进行中（模块 + main 完成，旧代码清理待做）
Stage 7 ─ 工具层 + 可视化      🔶 进行中（FoxgloveSink 实现完成，接入待做）
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

## Stage 3：接口层 + 工厂系统 ✅

**提交**：`8ee585a`（初始实现） / `87f2ad2`（factory lint 修复） / `6f9ce88`（枚举命名修复）

### 交付物

| 文件 | 说明 |
|------|------|
| `src/interfaces/types.hpp` | 跨层共享数据类型：`ArmorColor/Type/Number`、`Detection`、`GimbalControl`、`TrackTarget` |
| `src/interfaces/i_detector.hpp` | `IDetector` 纯虚接口（帧 → `Detection` 列表） |
| `src/interfaces/i_solver.hpp` | `ISolver` 纯虚接口（角点 → 3D 位姿，就地填充） |
| `src/interfaces/i_predictor.hpp` | `IPredictor` 纯虚接口（跨帧跟踪 → `GimbalControl`） |
| `src/interfaces/CMakeLists.txt` | `mv-interfaces` INTERFACE 库 |
| `src/factory/factory.hpp` | `Factory<Base>` 模板注册表 + `MV_REGISTER_*` 宏 |
| `src/factory/CMakeLists.txt` | `mv-factory` INTERFACE 库 |
| `docs/refractor/interfaces/INTERFACES_USAGE.md` | 使用指南 |

### 关键设计决策

- **三层分离**：`IDetector`（2D）→ `ISolver`（3D，就地填充）→ `IPredictor`（跨帧预测）各自独立接口，可替换任意一层而不影响其他层。
- **统一数据类型**：旧代码各模块自定义结构体（`basic_armor::Armor_Data`、`predictor::Armor`...），新设计用 `types.hpp` 统一约定，接口只传递这些类型。
- **Meyers Singleton 注册表**：`Factory<Base>::Registry()` 中的 `static unordered_map` 第一次调用时构造，绕开静态初始化顺序问题（SIOF）。
- **全局静态触发注册**：`MV_REGISTER_*` 宏在 `.cpp` 文件作用域展开，利用全局静态变量在 `main()` 之前初始化的特性完成注册，`main()` 只调用 `Create()`，新增实现无需修改 `main`。
- **枚举常量 UPPER_CASE**：遵守 `.clang-tidy` 中 `readability-identifier-naming.EnumConstantCase = UPPER_CASE`，与 clang-tidy 检查对齐。
- **INTERFACE 库**：接口和工厂全为纯头文件，不产生 `.a/.so`，只传播 include 路径和依赖，避免链接无用符号。

### 与旧代码对比

| 方面 | 旧代码 | 新设计 |
|------|--------|--------|
| 切换检测算法 | 修改 main.cpp 的 if-else | 改 YAML `detector.type` |
| 新增实现 | 修改 main.cpp + 适配新接口 | 在 `.cpp` 添加 `MV_REGISTER_DETECTOR` |
| 跨层数据传递 | 各模块自定义结构体，互相 include | 统一 `Detection` / `GimbalControl` |
| 单元测试 | 依赖真实算法和硬件 | 注入 `MockDetector` / `MockSolver` |
| 依赖方向 | 算法层依赖具体实现头文件 | 只依赖 `IDetector` / `ISolver` 接口 |

### clangd 额外配置（本阶段修复）

| 问题 | 修复 |
|------|------|
| `Eigen/Dense` 找不到 | `.clangd` `Add: -I/usr/include/eigen3` |
| `cv::` 找不到 | `.clangd` `Add: -I/usr/include/opencv4` |
| `chrono consteval` 误报 | `.clangd` `Suppress: invalid_consteval_call`（GCC libstdc++ 与 clang 前端不兼容） |
| `-std=c++17` | 升级为 `-std=c++20` |

### 编译状态

```
mv-interfaces  ✓ INTERFACE 库配置成功（CMake 输出验证）
mv-factory     ✓ INTERFACE 库配置成功（CMake 输出验证）
Mock 实现 + Factory::Create / Keys   ✓ g++ -std=c++20 编译通过
Stage 1 冒烟测试：15 / 15 仍全部通过 ✓
```

---

## Stage 4：线程 Pipeline ✅

### 交付物

| 文件 | 说明 |
|------|------|
| `src/interfaces/i_voter.hpp` | `IVoter` 纯虚接口（TrackTarget → 是否开火） |
| `src/interfaces/i_shooter.hpp` | `IShooter` 纯虚接口（GimbalControl → 串口字节流） |
| `src/pipeline/packet.hpp` | 节点间数据包：`FramePacket`、`DetectPacket`、`ControlPacket`、`RecvPacket` |
| `src/pipeline/channel.hpp` | `Channel<T>`：带 `Shutdown` 机制的有界线程安全通道 |
| `src/pipeline/node.hpp` | `PipelineNode` 抽象基类（线程生命周期 + 诊断接口） |
| `src/pipeline/capture_node.hpp/.cpp` | 采集节点（ICamera→FramePacket，带重试和 error_code）|
| `src/pipeline/detect_node.hpp/.cpp` | 检测+解算节点（IDetector+ISolver→DetectPacket） |
| `src/pipeline/predict_node.hpp/.cpp` | 预测+投票节点（IPredictor+IVoter→ControlPacket） |
| `src/pipeline/serial_node.hpp/.cpp` | 串口收发节点（IShooter发送+上行解析→shared state）|
| `src/pipeline/pipeline.hpp` | `VisionPipeline` 编排器（Builder 模式，Start/Stop/CheckErrors） |
| `src/pipeline/CMakeLists.txt` | `mv-pipeline` 静态库 |

### 关键设计决策

- **`Channel<T>` 带 Shutdown**：原 `ThreadSafeQueue` 的 `pop()` 无超时/关闭支持，会永久阻塞导致 Pipeline 无法停止。新的 `Channel` 增加 `Pop(value, timeout)` 和 `Shutdown()`，节点线程用 10ms 超时轮询 stop 标志，实现干净退出。
- **有界溢出策略（丢最旧帧）**：`Channel::Push` 队满时丢弃最旧帧而非阻塞，优先保证实时性。帧 ID 可用于检测掉帧。
- **共享原子状态（`SharedState`）**：`enemy_color` 是 `std::atomic<ArmorColor>`，`SerialNode` 写、`DetectNode`/`PredictNode` 读，无 mutex、无拷贝，跨线程可见性通过 `load()`/`store()` 默认 `seq_cst` 保证。
- **`VisionPipeline::Builder`**：强制调用方显式注入所有 7 个依赖（`Build()` 前检查）；通道容量可选配置（帧通道默认 2，控制通道默认 1）；`Build()` 内部负责通道和节点的创建，外部调用点极简。
- **停止顺序**：`Stop()` 必须按前级→后级依次停止（Capture→Detect→Predict→Serial），每级 `Stop()` 会 `Shutdown` 其输出通道，触发下级退出阻塞等待，防止死锁。
- **`ICamera` / `ISerial` 访问控制修复**：原接口的虚函数误放 `protected`，导致接口无法从外部调用。本阶段修复为 `public`（保留移动构造/赋值为 `protected`，符合 C.67）。

### 数据流

```
CaptureNode (ICamera)
    │  frame_ch_ (容量 2, FramePacket)
    ▼
DetectNode (IDetector + ISolver)
    │  detect_ch_ (容量 2, DetectPacket)
    ▼
PredictNode (IPredictor + IVoter)
    │  control_ch_ (容量 1, ControlPacket)
    ▼
SerialNode (IShooter + ISerial)
    │  上行解析 → SharedState::enemy_color (atomic)
    └────────────────────────────────▶ DetectNode/PredictNode (下一帧生效)
```

### 编译状态

| 提交 | 说明 |
|------|------|
| `bb6a1b8` | Stage 4 初始实现：IVoter/IShooter、Channel、4 节点、VisionPipeline |
| `34c2f02` | lint 修复：17 处 clang-tidy/clangd 警告全部消除 |

```
mv-pipeline  ✓ 零警告零错误（4 个 .cpp，1 个静态库）
整体构建     ✓ make -j$(nproc) 全部目标通过
```

### `node.hpp` API（lint 修复后）

`PipelineNode` 的 4 个原子成员由 `protected` 改为 `private`，派生类通过受保护方法访问：

| 方法 | 替代原子成员 | 说明 |
|------|------------|------|
| `ShouldStop()` | `stop_requested_.load()` | 检查停止标志 |
| `SetError(int code)` | `error_code_.store(code)` | 设置错误码（会置位 stop） |
| `IncrementProcessed()` | `processed_count_++` | 帧计数 +1 |

> 派生类 **不可** 直接访问 `stop_requested_`、`running_`、`error_code_`、`processed_count_`。

---

## Stage 5：状态机（FSM） ✅

**提交**：`d521f23`（实现） / `ae4759f`（lint 修复）

### 交付物

| 文件 | 说明 |
|------|------|
| `src/fsm/state_machine.hpp` | `StateMachine<StateEnum, Context>` 通用模板（header-only，纯标准库）|
| `src/fsm/vision_fsm.hpp` | `VisionFSM` 声明、`SystemState` 枚举（6 个状态）、`SystemContext` |
| `src/fsm/vision_fsm.cpp` | 6 个状态处理器实现 + `VisionFSM` 方法实现 |
| `src/fsm/CMakeLists.txt` | `mv-fsm` 静态库，依赖 `mv-pipeline` + `mv-core` |
| `docs/refractor/fsm/FSM_USAGE.md` | 使用指南（与 PIPELINE_USAGE.md 格式统一）|

### 关键设计决策

- **双层架构**：`StateMachine<>` 模板零业务依赖，可复用于其他子系统；`VisionFSM` 是视觉专用的业务层，仅此文件持有 `VisionPipeline` 所有权。
- **Pending Transition 机制**：状态处理器内部通过写 `ctx.requested_state` 请求跳转，`VisionFSM::Update()` 在每次 `on_update` 结束后统一处理，避免在回调内部嵌套调用 `Transition()`（重入）。
- **自动恢复限制**：ERROR 状态有 `MAX_RECOVERY = 3` 次冷却重试，超限后停留 ERROR 等待人工干预，防止无限重启刷日志。
- **析构安全**：`VisionFSM::~VisionFSM()` 在 Pipeline 仍在运行时调用 `Stop()`，保证线程安全退出。
- **`assert` → `if + throw`**：生产代码不依赖 `assert`（Release 模式会被删除），空指针检查改为 `std::invalid_argument` / `std::logic_error`，同时消除 `array-to-pointer-decay` clang-tidy 警告。

### 状态流转

```
IDLE ──Start()──► INIT ──稳定 200ms──► AUTO_AIM ◄──► ENERGY_BUFF（预留）
                          │                 │
                     CheckErrors       CheckErrors
                          └──────┬──────────┘
                               ERROR ──冷却 500ms──► RECOVERY ──Reset──► INIT
※ 任意状态 ──Stop()──► IDLE
```

### 编译状态

| 提交 | 说明 |
|------|------|
| `d521f23` | Stage 5 初始实现：6 状态 FSM，`StateMachine<>` 模板 |
| `ae4759f` | lint 修复：`assert` 替换为 `if+throw`，消除 array-to-pointer-decay 警告 |

```
mv-fsm  ✓ 零警告零错误（1 个 .cpp，1 个静态库）
```

---

## Stage 6：替换旧模块 + main.cpp 🔶

> 最后更新：2026-03-06

### 交付物

| 文件 | 说明 |
|------|------|
| `src/modules/armor_detector/basic_armor_detector.hpp/.cpp` | `BasicArmorDetector`（灯条轮廓法，实现 `IDetector`）|
| `src/modules/pnp_solver/pnp_solver.hpp/.cpp` | `PnpSolver`（`cv::solvePnP`，实现 `ISolver`）|
| `src/modules/simple_predictor/simple_predictor.hpp/.cpp` | `SimplePredictor`（无 EKF 简单跟踪器，实现 `IPredictor`）|
| `src/modules/simple_voter/simple_voter.hpp/.cpp` | `SimpleVoter`（is_tracking + auto_fire，实现 `IVoter`）|
| `src/modules/rm_shooter/rm_shooter.hpp/.cpp` | `RmShooter`（占位 8 字节协议，实现 `IShooter`）|
| `src/modules/CMakeLists.txt` | 5 个独立静态库 `mv-mod-*` |
| `src/app/main.cpp` | 入口：加载配置、初始化 Logger、信号处理、构建 Pipeline、驱动 VisionFSM |
| `src/app/CMakeLists.txt` | 新增 `mv-vision-main` 可执行文件 |
| `src/CMakeLists.txt` | 解除 `add_subdirectory(modules)` 注释 |

### 编译状态

```
mv-mod-armor-detector   ✓ 零警告零错误
mv-mod-pnp-solver       ✓ 零警告零错误
mv-mod-simple-predictor ✓ 零警告零错误
mv-mod-simple-voter     ✓ 零警告零错误
mv-mod-rm-shooter       ✓ 零警告零错误
mv-vision-main          ✓ 零警告零错误（8.7 MB 可执行文件）
整体构建（make -j）      ✓ 全部目标通过，无回归
```

### 关键设计决策

- **直接实例化而非工厂注册**：`main.cpp` 通过 `std::make_unique<BasicArmorDetector>()` 等直接创建模块，规避静态库链接器 dead-strip 导致的工厂注册丢失问题。工厂注册代码仍在各模块命名空间内保留（面向未来的 dynamic mode 使用）。
- **`MV_REGISTER_*` 宏内命名空间限制**：宏内部使用 `##ConcreteT` 标识符粘贴，`::` 字符导致非法标识符。解决方案：在各模块的 `namespace mv::modules {}` 内手写静态 bool 完成注册，不依赖宏中的类型前缀。
- **整棵配置树传给 Init()**：`cfg.Subtree()` 返回全配置树，各模块的 `Init()` 自行查找所需子节点（如 `auto_aim.tracker`、`calibration`），避免 `main.cpp` 了解模块内部配置结构。
- **串口失败不终止**：串口 `Open()` 失败时只打 `WARN`，不终止进程——无下位机时仍可在调试模式下运行 Pipeline（`RmShooter::Send()` 会静默失败，`SerialNode` 到达 `max_send_fail` 后进入 `ERROR` 状态触发 FSM 错误恢复）。
- **相机失败回退**：`mindvision` 模式失败时自动回退到 `OpenCvCamera`；`OpenCvCamera` 也失败时退出（无帧输入 Pipeline 无法运行）。

### 剩余技术债

| 文件 | 问题 | 优先级 |
|------|------|--------|
| `src/pipeline/serial_node.cpp` | `TryRecv()` 使用 5 字节占位帧，需与下位机确认协议后替换 | 中（比赛前）|
| `src/modules/rm_shooter/rm_shooter.cpp` | 帧格式同上为临时协议，CRC8 使用简单 XOR | 中 |
| `src/modules/simple_predictor/` | 无 EKF，无飞行时间延迟补偿，yaw/pitch 为直通值 | 中（提升精度时替换） |
| `src/fsm/vision_fsm.cpp` | `ENERGY_BUFF` 状态逻辑未接入上行 `mode` 字段 | 低 |
| `base/` `devices/` `module/` | 旧代码未清理，待新模块验证稳定后删除 | 低（Stage 6 末尾）|

---

---

## Stage 7：工具层 + Foxglove 可视化 🔶

**提交**：`de27a5f`

### 交付物

| 文件 | 说明 |
|------|------|
| `src/tool/foxglove/CMakeLists.txt` | `mv-tool-foxglove` 静态库声明 |
| `src/tool/foxglove/foxglove_sink.hpp` | 对外唯一公开头文件（PImpl 门面，零 SDK 依赖）|
| `src/tool/foxglove/foxglove_sink.cpp` | `Impl` 实现：WebSocket Server 初始化、参数管理、委托 5 个子发布器 |
| `src/tool/foxglove/detail/utils.hpp` | header-only 共用辅助：时间戳转换、颜色常量、`MakePose`、`EigenToFoxPose`、`MakeArrow` |
| `src/tool/foxglove/detail/image_publisher.hpp/.cpp` | `cv::Mat` → `RawImage`，按 topic 懒建 `RawImageChannel` |
| `src/tool/foxglove/detail/detection_publisher.hpp/.cpp` | `mv::Detection` 列表 → `ImageAnnotations`（2D）+ `SceneUpdate`（3D 装甲板立方体）|
| `src/tool/foxglove/detail/pnp_visualizer.hpp/.cpp` | PnP 三层可视化：调试图像 / RGB 坐标轴箭头 / JSON 残差统计 |
| `src/tool/foxglove/detail/tf_publisher.hpp/.cpp` | `Eigen::Matrix4d` → `/tf`（`FrameTransforms`）|
| `src/tool/foxglove/detail/thread_monitor.hpp/.cpp` | `ThreadMetrics` 列表 → `pipeline/nodes`（JSON via `RawChannel`）|
| `src/tool/CMakeLists.txt` | 新增 `USE_FOXGLOVE_SDK` 开关控制 `add_subdirectory(foxglove)` |
| `docs/refractor/tool/foxglove/FOXGLOVE_MODULE.md` | 12 节完整模块文档 |

### 关键设计决策

- **PImpl 完全隔离**：`foxglove_sink.hpp` 不 include 任何 SDK 头文件，所有 SDK 类型（`WebSocketServer`、`RawImageChannel` 等）完全封装在 `foxglove_sink.cpp` 及 `detail/*.cpp` 内，修改 SDK 版本只重编这些 `.cpp`。
- **5 个子发布器**：`ImagePublisher`、`DetectionPublisher`、`PnpVisualizer`、`TfPublisher`、`ThreadMonitor` 各自独立，单一职责，内部各持一把 `std::mutex`，可在任意线程安全调用。
- **FoxgloveSinkConfig 独立结构体**：GCC 不允许含非平凡成员的嵌套类作为函数默认参数，将 `Config` 移出类外为 `FoxgloveSinkConfig`，类内用 `using Config = FoxgloveSinkConfig;` 保留旧名，并分拆为 `FoxgloveSink()` + `explicit FoxgloveSink(Config)` 双构造函数（前者委托后者）。
- **双向参数管理**：`SetParameterCallback` 接受来自 Foxglove Studio 的 JSON 参数写入；`UpdateParameters` / `GetParameter` 支持代码侧主动推送参数至 Studio，实现实时调参可视化（阈值、增益等）。
- **PnP 三层可视化**：调试图（`pnp/debug_image`，底图+角点+连线+距离标注）+ 3D 坐标轴（`pnp/axes_3d`，RGB 箭头三轴）+ JSON 残差（`pnp/residuals`，含 `x/y/z/dist/yaw/pitch`），满足不同调试粒度需求。
- **线程健康看板**：`ThreadMetrics`（`node_name / fps / latency_ms / drop_count / is_alive / error_msg`）序列化为 JSON 发布到 `pipeline/nodes`，Foxglove Studio 自定义面板可实时展示各节点健康状态。
- **CMake 开关**：`USE_FOXGLOVE_SDK=ON`（默认）时才链接 SDK；`OFF` 时跳过整个子目录，零侵入已有目标。

### 编译状态

```
mv-tool-foxglove  ✓ 零警告零错误
整体构建（16 个目标，make -j）  ✓ 全部通过，无回归
```

### 待完成（Stage 7 剩余）

| 任务 | 说明 | 优先级 |
|------|------|--------|
| `FoxgloveSink` 接入 `DebugSession` | `debug_session.cpp` 持有 `optional<FoxgloveSink>`，`Feed()` 透传图像和检测结果 | 高 |
| `FoxgloveSink` 接入 `VisionPipeline` | `DetectNode::Run()` 调 `PublishDetections` + `PublishPnpResult`；`VisionPipeline` 定时汇聚调 `PublishThreadMetrics` | 高 |
| PnP IPPE 双解消歧 | `PnpSolver` 移植旧代码中 `SOLVEPNP_IPPE` 双解+远距离消歧逻辑，边改边在 Foxglove 观察 `pnp/axes_3d` | 中 |
| `ThreadMetrics` 上报机制 | `PipelineNode` 内计时，`VisionPipeline` 每 100ms 汇聚后调 `PublishThreadMetrics` | 中 |

---

## 已知问题 / 技术债（当前）

| 文件 | 问题 | 优先级 |
|------|------|--------|
| `src/app/smoke_test.cpp` | `main()` 认知复杂度超阈值（47 > 25），`files.size() >= 1` 应为 `!files.empty()` | 低（测试代码） |
| `module/foxglove_publisher/foxglove_publisher.cpp` | `schemas.hpp` 类型在 `namespace foxglove::schemas` 内失效，编译报错 | 中（旧代码，不影响新模块） |
| `src/pipeline/serial_node.cpp` | 上行帧格式使用占位协议（帧头 0xAA + 5 字节），正式比赛需与下位机队友确认协议后修改 `TryRecv()` | 中（比赛前修改）|
| `src/modules/rm_shooter/rm_shooter.cpp` | 帧格式为临时 8 字节占位协议，与上行协议需统一 | 中 |
| `src/modules/simple_predictor/` | 无 EKF，无飞行时间延迟补偿，仅直通当前帧角度 | 中（阶段性）|
| `src/fsm/vision_fsm.cpp` | `ENERGY_BUFF` 状态逻辑未接入上行 `mode` 字段 | 低 |
| `base/` `devices/` `module/` | 旧代码目录未清理，待新模块验证稳定后删除 | 低 |
