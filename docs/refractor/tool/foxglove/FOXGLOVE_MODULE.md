# Foxglove 可视化调试库（`mv-tool-foxglove`）

> 路径：`src/tool/foxglove/`
> CMake 目标：`mv-tool-foxglove`（STATIC 库，`USE_FOXGLOVE_SDK=ON` 时编译）

---

## 1. 设计目标

将 Foxglove Studio 实时可视化所需的全部发布逻辑封装为单一 `FoxgloveSink`，让调试程序和 Pipeline 只调用语义明确的 `Publish*` 方法，**不直接依赖 foxglove SDK 头文件**。

```
调用方（test / Pipeline）
        │  #include "tool/foxglove/foxglove_sink.hpp"   ← 唯一入口头文件
        ▼
  ┌──────────────────────────────────────────┐
  │          FoxgloveSink  (PImpl 门面)       │
  │  ┌──────────────────────────────────┐    │
  │  │           Impl                   │    │
  │  │  WebSocketServer  Context        │    │
  │  │  param_store  param_callback     │    │
  │  │                                  │    │
  │  │  ┌────────────┐ ┌─────────────┐  │    │
  │  │  │  image_pub │ │detection_pub│  │    │
  │  │  └────────────┘ └─────────────┘  │    │
  │  │  ┌────────────┐ ┌─────────────┐  │    │
  │  │  │  pnp_viz   │ │  tf_pub     │  │    │
  │  │  └────────────┘ └─────────────┘  │    │
  │  │  ┌──────────────────────────┐    │    │
  │  │  │    thread_monitor        │    │    │
  │  │  └──────────────────────────┘    │    │
  │  └──────────────────────────────────┘    │
  └──────────────────────────────────────────┘
           ↓ foxglove SDK（仅在 .cpp 中 include）
    WebSocket → Foxglove Studio
```

---

## 2. 文件一览

```
src/tool/foxglove/
├── CMakeLists.txt                  # mv-tool-foxglove 静态库
├── foxglove_sink.hpp               # 对外公开 API（PImpl 门面）——零 SDK 依赖
├── foxglove_sink.cpp               # PImpl 实现：Server 初始化 + 参数管理 + 委托子发布器
└── detail/
    ├── utils.hpp                   # header-only：时间戳/颜色/箭头辅助函数
    ├── image_publisher.hpp/.cpp    # cv::Mat → RawImage
    ├── detection_publisher.hpp/.cpp# mv::Detection → 2D 标注 + 3D 立方体
    ├── pnp_visualizer.hpp/.cpp     # PnP 三层调试输出
    ├── tf_publisher.hpp/.cpp       # Eigen::Matrix4d → FrameTransforms
    └── thread_monitor.hpp/.cpp     # ThreadMetrics → pipeline/nodes JSON
```

| 文件 | 作用 |
|------|------|
| `foxglove_sink.hpp` | PImpl 门面头文件。公开 `FoxgloveSink` 类与 `Config`、`ThreadMetrics` 数据结构 |
| `foxglove_sink.cpp` | `Impl` 实现，持有 `WebSocketServer`、`foxglove::Context` 以及全部子发布器 |
| `detail/utils.hpp` | `NowNs()`、`ResolveTs()`、`ToTs()`、颜色常量、`MakeArrow()`、`EigenToFoxPose()` |
| `detail/image_publisher` | 按 topic 懒创建 `RawImageChannel`，自动推断 bgr8 / mono8 / 16UC1 编码 |
| `detail/detection_publisher` | 同时写入 `detections/annotations`（ImageAnnotations）和 `detections/3d`（SceneUpdate）|
| `detail/pnp_visualizer` | 写入 `pnp/debug_image`、`pnp/axes_3d`、`pnp/residuals` 三条 topic |
| `detail/tf_publisher` | 写入 `/tf`（FrameTransforms），支持任意 parent/child 名称 |
| `detail/thread_monitor` | 写入 `pipeline/nodes`（JSON），`drop > 0` 时附加 `warn` 字段 |

---

## 3. Foxglove Topics 一览

| Topic | Schema | 来源方法 | 内容 |
|-------|--------|----------|------|
| `camera/raw` | `RawImage` | `PublishImage()` | 原始相机帧 |
| `camera/debug` | `RawImage` | `PublishImage()` | 处理后调试帧 |
| `detections/annotations` | `ImageAnnotations` | `PublishDetections()` | 装甲板 2D 角点多边形 + 文字标签 |
| `detections/3d` | `SceneUpdate` | `PublishDetections()` | 已解算装甲板 3D 立方体（云台坐标系）|
| `pnp/debug_image` | `RawImage` | `PublishPnpResult()` | 底图 + 绿色原始角点 |
| `pnp/axes_3d` | `SceneUpdate` | `PublishPnpResult()` | 每个装甲板 RGB XYZ 坐标轴箭头 |
| `pnp/residuals` | JSON | `PublishPnpResult()` | 位姿 / 深度 / yaw_deg / pitch_deg |
| `/tf` | `FrameTransforms` | `PublishTransform()` | 坐标系变换树（3D 面板使用）|
| `pipeline/nodes` | JSON | `PublishThreadMetrics()` | 各节点 fps / latency_ms / drop / alive |
| `control/gimbal` | JSON | `PublishGimbalControl()` | yaw / pitch / distance / fire / tracking |

---

## 4. 快速上手

### 4.1 引入头文件

```cpp
#include "tool/foxglove/foxglove_sink.hpp"
// 无需包含任何 foxglove SDK 头文件
```

链接（CMakeLists.txt）：

```cmake
target_link_libraries(my-target PRIVATE mv-tool-foxglove)
```

### 4.2 端到端测试程序（`mv-foxglove-vision-test`）

```bash
# 视频文件
mv-foxglove-vision-test armor.mp4
# 摄像头 0，识别蓝方，自定义端口
mv-foxglove-vision-test 0 blue 9090
```

测试程序覆盖全部 6 类 Publish* 接口，同时启动 TerminalHUD（见 §4.5），无 Studio 连接时仍可正常运行。

测试程序典型代码片段：

```cpp
mv::tool::FoxgloveSink sink{fox_cfg};
sink.Start();

// 外部 HasClients() 门控：零开销避免绘图
if (sink.HasClients()) {
    auto dbg = DrawDebug(frame, dets);
    sink.PublishImage(dbg, "camera/annotated");
}

// PublishImage / PublishPnpResult 内部已自动门控图像编码
sink.PublishImage(frame, "camera/raw");          // 零客户端时直接 return
sink.PublishDetections(dets);
sink.PublishPnpResult(dets, frame);
sink.PublishGimbalControl(ctrl);
sink.PublishTransform("world", "gimbal", T_wg);
sink.PublishThreadMetrics(metrics);
```

### 4.3 Pipeline 中使用

在 `VisionPipeline` 中持有 `shared_ptr<FoxgloveSink>`，各节点的发布调用在节点 `Run()` 循环内执行：

```cpp
// pipeline.hpp 成员
std::shared_ptr<mv::tool::FoxgloveSink> foxglove_sink_;

// detect_node.cpp（Run() 循环内）
if (foxglove_sink_) {
    foxglove_sink_->PublishDetections(packet.detections);
    foxglove_sink_->PublishPnpResult(packet.detections, packet.frame);
}

// serial_node.cpp（定时发布线程健康，每 100 ms 一次）
if (foxglove_sink_ && metrics_timer_.Elapsed() > 100ms) {
    foxglove_sink_->PublishThreadMetrics(CollectMetrics());
    metrics_timer_.Reset();
}
```

### 4.4 反向参数调节

```cpp
// 注册参数修改回调（Foxglove 端拖动滑块时被调用）
sink.SetParameterCallback([&](const std::string& name, const nlohmann::json&) {
    if (name == "light_thresh") {
        int v = sink.GetParameter(name).get<int>();
        params.light_thresh = v;
        detector.SetParams(params);
    }
});

// 初始化时推送当前参数快照
sink.UpdateParameters({
    {"light_thresh",    params.light_thresh},
    {"max_light_angle", params.max_light_angle},
});
```

### 4.5 赛场无网条件：TerminalHUD

当赛场环境无法接入笔电时，使用 `TerminalHUD`（头文件 `tool/debug/terminal_hud.hpp`）在终端提供双行状态刷新，开销极低：

```
[MV] 15:42:01 | FPS:  143 | DET:  2/ARMOR | LOCK: ✓ | YAW:   -3.2° PITCH:   1.1° DIST:  4.20m
[THR] Capture✓143fps  Detect✓115fps  Predict✓116fps  Serial✓999fps
```

典型用法：

```cpp
#include "tool/debug/terminal_hud.hpp"

mv::tool::TerminalHUD hud;   // 默认 200ms 刷新

// 主循环内（无网时 FoxgloveSink 不 Start()，只用 HUD）
std::vector<mv::tool::TerminalHUD::NodeMetrics> nm;
for (const auto& m : foxglove_metrics) {     // ThreadMetrics → NodeMetrics 1:1 转换
    nm.push_back({m.node_name, m.fps, m.latency_ms, m.drop_count, m.is_alive, m.error_msg});
}
hud.Update(fps, dets, &ctrl, &nm);
```

| 特性 | 说明 |
|------|------|
| 速率限制 | 默认 200 ms，`interval_ms=0` 每帧打印 |
| ANSI 颜色 | 绿/黄/红（fps 级别 / 锁定状态 / 节点健康），管道/文件输出时自动关闭 |
| TTY 检测 | `isatty()` 自动切换 `\r` 覆写 vs 换行追加 |
| 节点状态 | `NodeMetrics` 字段与 `FoxgloveSink::ThreadMetrics` 一一对应，可直接转换 |
| 零依赖 | header-only，无 OpenCV / Foxglove / yaml-cpp，编译到任意目标 |

---

## 5. API 速查

### `FoxgloveSinkConfig`（即 `FoxgloveSink::Config`）

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `name` | `string` | `"MiracleVision"` | Foxglove 中显示的服务名称 |
| `host` | `string` | `"0.0.0.0"` | WebSocket 监听地址 |
| `port` | `uint16_t` | `8765` | WebSocket 端口 |

### `FoxgloveSink::ThreadMetrics`

| 字段 | 类型 | 说明 |
|------|------|------|
| `node_name` | `string` | 节点名称（如 `"CaptureNode"`）|
| `fps` | `double` | 当前帧率 |
| `latency_ms` | `double` | 处理延迟（ms）|
| `drop_count` | `uint64_t` | 累计丢帧数，>0 时 JSON 附加 `warn` 字段 |
| `is_alive` | `bool` | 线程是否仍在运行 |
| `error_msg` | `string` | 最近错误描述（空 = 无错）|

### `FoxgloveSink` 主要方法

| 方法 | 说明 |
|------|------|
| `FoxgloveSink()` | 默认构造，使用 `0.0.0.0:8765` |
| `FoxgloveSink(Config)` | 自定义 host / port |
| `Start()` | 启动 WebSocket Server |
| `Stop()` | 停止 Server，断开所有客户端 |
| `HasClients()` | 是否有客户端连接（原子读，O(1)，无锁）|
| `PublishImage(img, topic, frame_id, ts_ns)` | 发布图像（零客户端时自动跳过编码）|
| `PublishDetections(dets, ts_ns)` | 发布检测结果到 `detections/annotations` + `detections/3d` |
| `PublishPnpResult(dets, frame, ts_ns)` | 发布 PnP 三层调试（零客户端时自动跳过图像编码）|
| `PublishTransform(parent, child, T, ts_ns)` | 发布坐标系变换到 `/tf` |
| `PublishThreadMetrics(metrics, ts_ns)` | 发布节点健康指标到 `pipeline/nodes` |
| `PublishGimbalControl(ctrl, ts_ns)` | 发布云台控制量到 `control/gimbal` |
| `SetParameterCallback(cb)` | 注册参数修改回调 |
| `UpdateParameters(json)` | 推送参数快照到 Foxglove 客户端 |
| `GetParameter(name)` | 从本地缓存查询参数值 |

> `ts_ns` 参数全部可省略（传 0 或不传），此时内部自动取 `system_clock::now()`。

---

## 6. 子发布器设计说明

### 6.1 `image_publisher`

- 按 topic 字符串懒创建 `foxglove::schemas::RawImageChannel`，首次调用时创建，后续复用。
- 根据 `cv::Mat::type()` 自动推断编码：`CV_8UC3` → `bgr8`，`CV_8UC1` → `mono8`，`CV_16UC1` → `16UC1`，`CV_8UC4` → `bgra8`。
- 内部 `mutex` 保护 channel 映射表，可从任意线程并发调用。

### 6.2 `detection_publisher`

**2D（`detections/annotations`）**：
- 每块装甲板 → `PointsAnnotation`（`LINE_LOOP`），颜色区分 RED / BLUE
- 中心点 → 黄色 `POINTS`（直径 5px）
- 标签 → `TextAnnotation`，格式 `"R-1 120cm"`（`is_solved=false` 时无距离）

**3D（`detections/3d`）**：
- 仅 `is_solved=true` 的装甲板参与
- `CubePrimitive`：按物理尺寸（SMALL 135×55mm / BIG 230×55mm）放置，半透明填色
- `TextPrimitive`（`billboard=true`）：标签始终面朝摄像机，位于立方体正上方 0.07m 处
- `lifetime = 300ms`：目标消失后自动淡出，无需手动删除

### 6.3 `pnp_visualizer`（三层）

| 层 | Topic | 内容 |
|----|-------|------|
| 1 | `pnp/debug_image` | 底图（灰度自动转 BGR）+ 绿色角点实心圆 + 黄色中心点 + 角点连线 + `#i NNNcm / no pnp` 标注 |
| 2 | `pnp/axes_3d` | 每个已解算装甲板的 RGB 三轴箭头（X 红 / Y 绿 / Z 蓝），轴长 0.1m，`lifetime=400ms` |
| 3 | `pnp/residuals` | JSON：`{timestamp_ns, count, armors:[{index, solved, label, x_m, y_m, z_m, dist_m, yaw_deg, pitch_deg}]}` |

`pnp/axes_3d` 的箭头由 `detail::MakeArrow()` 构造：将 +X 轴旋转到目标方向，生成 `ArrowPrimitive`（轴长 80% 轴身 + 20% 箭头）。

### 6.4 `tf_publisher`

- 写入标准 `/tf` topic（`FrameTransforms` schema），Foxglove 3D 面板自动识别并构建坐标系树。
- 从 `Eigen::Matrix4d` 提取旋转矩阵，转为 `Eigen::Quaterniond` 后写入 `FrameTransform`。
- 建议的坐标系树：
  ```
  world ──► gimbal ──► camera
  ```
  - `("world", "gimbal", T_wg)`：每帧由预测器推送
  - `("gimbal", "camera", T_gc)`：Init 时发布一次（固定外参）

### 6.5 `thread_monitor`

写入 `pipeline/nodes`（JSON），示例输出：

```jsonc
{
  "timestamp_ns": 1741392000000000000,
  "nodes": [
    {"name": "CaptureNode",  "fps": 120.3, "latency_ms": 2.1,  "drop": 0,  "alive": true},
    {"name": "DetectNode",   "fps": 85.7,  "latency_ms": 10.3, "drop": 3,  "alive": true, "warn": "drop>3"},
    {"name": "PredictNode",  "fps": 85.5,  "latency_ms": 0.8,  "drop": 0,  "alive": true},
    {"name": "SerialNode",   "fps": 85.4,  "latency_ms": 0.4,  "drop": 0,  "alive": true}
  ]
}
```

`drop_count > 0` 时附加 `"warn": "drop>N"` 字段，便于在 Foxglove JSON 面板中直观识别丢帧节点。

---

## 7. PImpl 隔离说明

`foxglove_sink.hpp` 中**零 foxglove SDK 头文件**，所有 SDK 类型只出现在 `.cpp` 文件中：

```
foxglove_sink.hpp
  依赖：<string> <vector> <functional> <memory> <cstdint>
        Eigen/Dense  nlohmann/json.hpp  opencv2/core.hpp
        interfaces/types.hpp
  不依赖：foxglove/channel.hpp  foxglove/schemas.hpp  foxglove/server.hpp

foxglove_sink.cpp / detail/*.cpp
  依赖（仅编译单元内）：foxglove SDK 全部头文件
```

这意味着：
- 链接 `mv-tool-foxglove` 的目标（如 `mv-tool-debug`、`mv-pipeline`）**不需要在编译时找到 foxglove SDK 头文件**
- 修改 foxglove SDK 用法只需重编相关 `.cpp`，不触发上游目标的重编

---

## 8. 线程安全

| 组件 | 保护方式 |
|------|---------|
| `ImagePublisher` channel map | `std::mutex mtx_` |
| `DetectionPublisher` channels | `std::mutex mtx_` |
| `PnpVisualizer` channels | `std::mutex mtx_` |
| `TfPublisher` channel | `std::mutex mtx_` |
| `ThreadMonitor` channel | `std::mutex mtx_` |
| `FoxgloveSink` param_store | `std::mutex param_mutex` |
| `FoxgloveSink` ctrl channel | `std::mutex ctrl_mtx` |
| foxglove Channel 自身 | SDK 保证线程安全 |

所有 `Publish*` 方法均可从 Pipeline 不同线程并发调用，无需外部加锁。

---

## 9. 与 `DebugSession` 集成（预留）

`docs/refractor/tool/debug/DEBUG_MODULE.md §8` 已预留了 `FoxgloveSink` 插槽。集成方式：

```cpp
// debug_session.cpp（Impl 内）
std::optional<mv::tool::FoxgloveSink> foxglove_sink_;

// DebugSession::Init() 中（根据配置决定是否启用）
if (cfg.enable_foxglove) {
    foxglove_sink_.emplace(
        mv::tool::FoxgloveSinkConfig{"MiracleVision", "0.0.0.0", 8765});
    foxglove_sink_->Start();
}

// DebugSession::Feed() 中（透传，test 程序零改动）
if (foxglove_sink_) {
    foxglove_sink_->PublishImage(raw, "camera/raw");
    foxglove_sink_->PublishDetections(detections);
    foxglove_sink_->PublishPnpResult(detections, raw);
    foxglove_sink_->PublishGimbalControl(ctrl);
}
```

---

## 10. 编译开关

| CMake 选项 | 默认值 | 说明 |
|-----------|--------|------|
| `USE_FOXGLOVE_SDK` | `ON` | 关闭时 `src/tool/foxglove/` 子目录跳过，`mv-tool-foxglove` 不生成 |

关闭时上游目标（如 `mv-tool-debug`）需自行去掉对 `mv-tool-foxglove` 的 `target_link_libraries`，或用条件包裹：

```cmake
if(USE_FOXGLOVE_SDK)
    target_link_libraries(my-target PRIVATE mv-tool-foxglove)
endif()
```

---

## 11. 编译状态

```
mv-tool-foxglove           ✓ 零警告零错误
mv-foxglove-vision-test    ✓ 零警告零错误（端到端测试可执行文件）
整体构建（make -j）         ✓ 全部目标通过，无回归
```

| 提交 | 说明 |
|------|------|
| `de27a5f` | `feat(tool): add FoxgloveSink — decoupled PImpl Foxglove visualization module` |
| `1ba728c` | `feat(tool): add has_clients gate + TerminalHUD` |
| `658d71e` | `feat(test): add mv-foxglove-vision-test + fix clangd crash` |

---

## 12. 目录结构

```
src/tool/
├── CMakeLists.txt                  # add_subdirectory(foxglove) 在 USE_FOXGLOVE_SDK=ON 时生效
└── foxglove/
    ├── CMakeLists.txt              # mv-tool-foxglove STATIC 库声明
    ├── foxglove_sink.hpp           # 对外唯一头文件（HasClients() / Publish*）
    ├── foxglove_sink.cpp           # PImpl 实现（client_count 原子计数器）
    └── detail/
        ├── utils.hpp               # 时间戳/颜色/Pose/Arrow 辅助（header-only）
        ├── image_publisher.hpp/.cpp
        ├── detection_publisher.hpp/.cpp
        ├── pnp_visualizer.hpp/.cpp
        ├── tf_publisher.hpp/.cpp
        └── thread_monitor.hpp/.cpp

src/test/
└── foxglove_vision_test.cpp        # mv-foxglove-vision-test 端到端测试

src/tool/debug/
└── terminal_hud.hpp                # TerminalHUD（header-only，赛场无网调试）
```

---

## 13. NUC ↔ 调试机网络配置

### 13.1 连接方式选择

| 场景 | 推荐方式 | 延迟 | 说明 |
|------|---------|------|------|
| 赛场正式调试 | **网线直连** | <1 ms | 无需路由器，最稳定，IP 手动配置 |
| 实验室调试 | 路由器局域网 | 1–5 ms | 方便多设备，IP 通过 DHCP 自动分配 |
| 无网线场景 | WiFi 热点（NUC 开热点，笔电连接）| 5–20 ms | 备用方案，实时图像帧率可能受影响 |

> **赛场建议**：网线直连 NUC 以太网口（`eth0` / `enp3s0`）到笔电，静态 IP，彻底避免 DHCP 超时和无线丢包。

### 13.2 网线直连 IP 配置

**NUC 端**（Ubuntu，`nmcli` 设置静态 IP）：

```bash
# 查看以太网接口名（通常 enp3s0 或 eth0）
ip link show

# 设置静态 IP（以 enp3s0 为例）
sudo nmcli con add type ethernet ifname enp3s0 con-name foxglove-link \
    ip4 192.168.10.1/24 gw4 192.168.10.1
sudo nmcli con up foxglove-link

# 验证
ip addr show enp3s0   # 应看到 inet 192.168.10.1/24
```

**调试机端**（同一网段，不同末位）：

```bash
# Ubuntu / Debian
sudo nmcli con add type ethernet ifname eth0 con-name foxglove-link \
    ip4 192.168.10.2/24 gw4 192.168.10.1
sudo nmcli con up foxglove-link

# macOS（系统设置 → 网络 → 以太网 → 手动）
# IP: 192.168.10.2   子网掩码: 255.255.255.0   路由器: 留空
```

连通性验证：

```bash
# 从调试机 ping NUC
ping 192.168.10.1

# 检测 8765 端口是否可达（NUC 程序运行后）
nc -zv 192.168.10.1 8765
```

### 13.3 NUC 防火墙开放端口

```bash
# 开放 WebSocket 端口（UFW）
sudo ufw allow 8765/tcp comment 'Foxglove WebSocket'
sudo ufw reload
sudo ufw status | grep 8765   # 确认生效

# 若使用 firewalld
sudo firewall-cmd --add-port=8765/tcp --permanent
sudo firewall-cmd --reload

# 临时关闭防火墙（调试时快速验证）
sudo ufw disable  # 验证完后务必重新开启
```

### 13.4 运行时 libfoxglove.so 路径

`libfoxglove.so` 位于 `3rdparty/foxglove/lib/`，运行可执行文件前需告知动态链接器：

```bash
# 方式 A：每次运行前设置（推荐调试阶段）
export LD_LIBRARY_PATH=/home/piesia-02/EX_MiracleVision/3rdparty/foxglove/lib:$LD_LIBRARY_PATH
./build/bin/mv-foxglove-vision-test armor.mp4

# 方式 B：写入 ~/.bashrc（永久生效）
echo 'export LD_LIBRARY_PATH=/home/piesia-02/EX_MiracleVision/3rdparty/foxglove/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc

# 方式 C：ldconfig（系统级，需 root）
echo '/home/piesia-02/EX_MiracleVision/3rdparty/foxglove/lib' | sudo tee /etc/ld.so.conf.d/foxglove.conf
sudo ldconfig
```

验证：

```bash
ldd build/bin/mv-foxglove-vision-test | grep foxglove
# 正常输出：libfoxglove.so => /path/to/3rdparty/foxglove/lib/libfoxglove.so
```

### 13.5 Foxglove Studio 连接步骤

1. 在 NUC 上启动程序：
   ```bash
   export LD_LIBRARY_PATH=.../3rdparty/foxglove/lib:$LD_LIBRARY_PATH
   ./build/bin/mv-foxglove-vision-test armor.mp4
   # 终端应出现：[FoxgloveSink] WebSocket server started
   ```

2. 在调试机打开 **Foxglove Studio**（[下载](https://foxglove.dev/download)）

3. 点击 **Open connection** → 选择 **Foxglove WebSocket**

4. 填入地址：
   ```
   ws://192.168.10.1:8765
   ```
   （替换为实际 NUC IP）

5. 连接成功后 Topics 面板会列出所有已发布 channel，可拖拽到面板：

   | 推荐面板 | 绑定 Topic |
   |---------|----------|
   | Image | `camera/raw` / `pnp/debug_image` |
   | 3D | `detections/3d` / `pnp/axes_3d` / `/tf` |
   | Image Annotations（叠加在 Image 上）| `detections/annotations` |
   | Raw Messages（JSON 查看）| `pnp/residuals` / `pipeline/nodes` / `control/gimbal` |
   | Plot | 从 `pipeline/nodes` 中提取 fps / latency_ms 字段 |

### 13.6 赛场无网线应急方案（TerminalHUD）

当完全无法使用网线时，不启动 Foxglove，仅依赖终端输出：

```bash
# configs/vision.yaml 中关闭 Foxglove，程序仍正常运行
# mv-foxglove-vision-test 无论是否有客户端均可运行
# TerminalHUD 以 200ms 刷新双行状态：

[MV] 15:42:01 | FPS:  143 | DET:  2/ARMOR | LOCK: ✓ | YAW:   -3.2° PITCH:   1.1° DIST:  4.20m
[THR] VideoDetect✓143fps
```

> TerminalHUD 无任何网络/图像编码开销，始终可用，是赛场极端环境的兜底调试手段。
