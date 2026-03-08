# Debug 调试工具库（`mv-tool-debug`）

> 路径：`src/tool/debug/`
> CMake 目标：`mv-tool-debug`（STATIC 库）

---

## 1. 设计目标

将离线调试所需的全部基础设施（窗口管理、参数旋钮、视图切换、FPS 统计、参数持久化）集中封装，让测试程序只包含视觉业务逻辑，**不直接依赖 OpenCV highgui 或 yaml-cpp**。

```
test/video_pipeline_test.cpp
        │  #include "tool/debug/debug_session.hpp"   <- 唯一调试头文件
        ▼
  ┌───────────────────────────────┐
  │       DebugSession (Facade)   │  Pimpl 隔离
  │  ┌──────────┐  ┌──────────┐  │
  │  │ParamTuner│  │ViewRender│  │  各自 Pimpl
  │  └──────────┘  └──────────┘  │
  │  ┌────────────────────────┐  │
  │  │   MetricsTracker       │  │  header-only
  │  └────────────────────────┘  │
  └───────────────────────────────┘
```

---

## 2. 文件一览

| 文件 | 作用 |
|------|------|
| `metrics_tracker.hpp` | Header-only。FPS 滑动平均 + 帧统计 |
| `param_tuner.hpp/.cpp` | Pimpl。OpenCV Trackbar 封装 + YAML 写回 |
| `view_renderer.hpp/.cpp` | Pimpl。双窗口渲染 + 4 种视图模式 + HUD |
| `debug_session.hpp/.cpp` | Pimpl Facade。聚合以上三个组件；管理按键绑定 |
| `CMakeLists.txt` | 声明 `mv-tool-debug` 静态库 |

---

## 3. 快速上手

### 3.1 在测试程序中引入

```cpp
#include "tool/debug/debug_session.hpp"
// 无需包含 opencv/highgui、yaml-cpp 等
```

链接（CMakeLists.txt）：

```cmake
target_link_libraries(my-test PRIVATE mv-tool-debug)
```

### 3.2 典型 `main()` 骨架

```cpp
// 1. 初始化算法模块（略）
mv::modules::BasicArmorDetector detector;
detector.Init(cfg);
detector.EnableDebug(true);   // 必须开启，否则 GetDebugData() 返回空图

// 2. 创建 DebugSession
mv::tool::DebugSession dbg;
dbg.Init({
    .main_window  = "mv-video-test",          // 主视图窗口标题
    .debug_window = "mv-video-debug",         // debug 辅助窗口标题
    .save_yaml    = "configs/debug_override.yaml",  // 参数持久化路径
    .fps_window   = 30,                       // FPS 滑动平均窗口帧数
});

// 3. 注册可调参数（Trackbar 与 Params 结构双向绑定）
auto params = detector.GetParams();

dbg.AddParam({"Thresh         ", "light_thresh",
              params.light_thresh, 255,
              [&params](int v) { params.light_thresh = v; },
              [&params]        { return double(params.light_thresh); }});

dbg.AddParam({"MaxAngle x10   ", "max_light_angle",
              int(params.max_light_angle * 10), 900,
              [&params](int v) { params.max_light_angle = float(v) / 10.f; },
              [&params]        { return double(params.max_light_angle); }});
// ... 其余参数类似 ...

// 4. 绑定自定义按键（q/ESC/空格/1–4 已内置）
dbg.BindKey('s', [&dbg] { dbg.SaveParams(); });

// 视频循环播放（仅文件源，摄像头流无需此逻辑）
bool loop_video = is_file_source;   // 视频文件时默认开启
dbg.BindKey('l', [&loop_video] { loop_video = !loop_video; });

// 切换识别颜色（c 键）
mv::ArmorColor enemy_color = mv::ArmorColor::RED;  // 或从配置读取
dbg.BindKey('c', [&enemy_color] {
    enemy_color = (enemy_color == mv::ArmorColor::RED)
        ? mv::ArmorColor::BLUE : mv::ArmorColor::RED;
});

// 5. 主循环
cv::Mat frame;
while (true) {
    auto [quit, paused] = dbg.Poll();
    if (quit)   break;
    if (paused) continue;

    dbg.ApplyParams();           // Trackbar → params
    detector.SetParams(params);  // params → 检测器

    camera.Grab(frame);

    // 视频循环：Grab 失败时重新 Open（摄像头流不受影响）
    if (frame.empty() && loop_video) {
        camera.Close();
        camera.Open(cam_cfg);
        predictor.Init(root_cfg);  // 重置跨帧状态
        continue;
    }

    auto t   = std::chrono::steady_clock::now();
    auto det = detector.Detect(frame, enemy_color);
    for (auto& d : det) solver.Solve(d);
    auto ctrl = predictor.Predict(det, t, enemy_color);

    // status 字符串显示在 HUD 第 3 行（橙色）
    const std::string status =
        std::string("Enemy: ") +
        (enemy_color == mv::ArmorColor::RED ? "RED" : "BLUE") +
        "  [c]toggle";
    dbg.TickFrame(!det.empty(), int(det.size()));
    dbg.Feed(frame, detector.GetDebugData(), det, ctrl, detector.GetParams(), status);
}

// 6. 收尾
dbg.PrintStats();            // 终端打印帧率/命中率统计
camera.Close();
// cv::destroyAllWindows() 由 DebugSession 析构自动调用
```

---

## 4. API 速查

### `DebugSession::Config`

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `main_window` | `"mv-video-test"` | 主视图窗口标题 |
| `debug_window` | `"mv-video-debug"` | 辅助窗口标题（Trackbar 附着于此） |
| `save_yaml` | `""` | `SaveParams()` 写入的 YAML 路径；空字符串时不写入 |
| `fps_window` | `30` | FPS 滑动平均的帧数窗口 |

### `DebugSession` 主要方法

| 方法 | 说明 |
|------|------|
| `Init(Config)` | 创建窗口（960×540 / 640×480 初始大小，可拖拽调整）、初始化子组件 |
| `AddParam(ParamDesc)` | 注册一个 Trackbar 参数 |
| `BindKey(int key, fn)` | 注册自定义按键（覆盖内置时生效） |
| `Poll()` → `{quit, paused}` | 等待 1 ms 按键，分发绑定动作；**每帧调用一次** |
| `ApplyParams()` | 将所有 Trackbar 当前值通过 `apply` 回调推送到外部 Params |
| `TickFrame(bool, int)` | 向 MetricsTracker 报告本帧检测结果 |
| `Feed(raw, dbg, dets, ctrl, params[, status])` | 渲染主/debug 窗口；`status` 可选，传入后显示在 HUD 第 3 行（橙色）|
| `SaveParams()` | 将当前参数写入 `Config::save_yaml`（YAML section = `"armor_detector"`） |
| `PrintStats()` | 终端输出总帧数 / 检测率 / 平均 FPS |

### `ParamDesc` 字段

```cpp
struct ParamDesc {
    std::string label;               // Trackbar 显示文字（建议统一宽度）
    std::string yaml_key;            // YAML 保存时的字段名
    int         init_val;            // 初始整数值
    int         max_val;             // Trackbar 最大值
    std::function<void(int)> apply;  // int → Params 字段
    std::function<double()>  get_val;// Params 字段 → double（用于 YAML 写回）
};
```

### 内置按键

| 按键 | 动作 |
|------|------|
| `q` / `ESC` | 退出主循环（`Poll().quit == true`） |
| 空格 | 切换暂停/继续 |
| `1` | 视图：最终检测结果（原图 + 四角点）|
| `2` | 视图：通道差分图（diff）|
| `3` | 视图：二值化图（binary）|
| `4` | 视图：灯条可视化图（lights）|

### `mv-video-test` 自定义按键

| 按键 | 动作 |
|------|------|
| `s` | 将当前 Trackbar 参数写入 `debug_override.yaml` |
| `l` | 切换视频循环播放开/关（仅对视频文件有效；默认**开启**）|
| `c` | 切换识别颜色 RED ↔ BLUE，HUD 第 3 行实时反映，终端打印日志 |

---

## 5. 视图说明

| 编号 | `ViewMode` | 内容 |
|------|------------|------|
| 1 | `RESULT` | 原始帧 + 检测四角框 + 置信度/角度标签 + HUD |
| 2 | `DIFF` | `B-G` 通道差分图（灰度→BGR），高亮红/蓝光源区域 |
| 3 | `BINARY` | 二值化后的掩码图，用于判断灯条是否被正确提取 |
| 4 | `LIGHTS` | 原图叠加灯条轮廓（每根灯条通过筛选后绘制黄色旋转矩形）|

主窗口每种视图均叠加 HUD：
- **左上第 1 行**（黄色）：帧号 / FPS / 当前视图名 / 按键提示
- **左上第 2 行**：跟踪状态（绿色 TRACKING + 角度，或蓝色 LOST）
- **左上第 3 行**（橙色）：`Feed()` 传入的 `status` 字符串，如当前识别颜色（为空时不渲染）
- **右上**：当前检测器参数摘要

debug 辅助窗口始终显示二值化图 + 参数单行摘要，同时承载全部 Trackbar。

---

## 6. 视频循环播放

调试视频往往较短，开启循环可反复观察同一段素材并实时调整参数。

**行为：**
- 输入为**视频文件**时 `loop_video` 默认 `true`，播放末尾自动从头循环
- 输入为**摄像头实时流**时 `loop_video` 始终 `false`，流断开时正常退出
- 按 `l` 键随时切换开/关，终端打印当前状态

**实现要点：**
```cpp
if (!camera.Grab(frame) || frame.empty()) {
    if (loop_video && is_file_source) {
        camera.Close();
        camera.Open(cam_cfg);     // VideoCapture 重新定位到帧 0
        predictor.Init(root_cfg); // 清除跨帧跟踪历史，防止状态污染
        continue;
    }
    break;  // 非循环模式或摄像头断开 → 正常退出
}
```

---

## 7. 参数持久化

按 `s`（或调用 `dbg.SaveParams()`）时，所有参数以当前浮点值写入 `Config::save_yaml`，YAML section 默认为 `armor_detector`：

```yaml
# configs/debug/debug_override.yaml（自动生成/更新）
armor_detector:
  light_thresh: 168
  max_light_angle: 35.0
  min_armor_ratio: 1.2
  max_armor_ratio: 4.8
  max_angle_diff: 7.5
```

> **路径**：`configs/debug/debug_override.yaml`（与 `vision.yaml` 等正式配置同在 `configs/` 下，
> 子目录 `debug/` 由 `ParamTuner::SaveTo()` 内部调用 `std::filesystem::create_directories` 自动创建）。

> **注意**：此文件不会修改 `vision.yaml`，需手动将调优结果回写到正式配置。

---

## 8. 扩展 / Foxglove 预留

`DebugSession::Impl` 预留了 `FoxgloveSink` 插槽：

```
未来扩展：在 Impl 内添加 FoxgloveSink sink_；
Feed() 中同时调用 sink_.Publish(raw, detections, ctrl)；
test 程序侧代码**零改动**。
```

---

## 9. 目录结构

```
src/tool/
├── CMakeLists.txt          # add_subdirectory(debug)；未来增模块在此添加
└── debug/
    ├── CMakeLists.txt      # mv-tool-debug STATIC 库
    ├── metrics_tracker.hpp # header-only FPS 统计
    ├── param_tuner.hpp     # Pimpl 头文件
    ├── param_tuner.cpp
    ├── view_renderer.hpp   # Pimpl 头文件
    ├── view_renderer.cpp
    ├── debug_session.hpp   # Facade Pimpl 头文件
    └── debug_session.cpp
```

其他工具模块（如 `record/`、`replay/`）可独立添加子目录，`src/tool/CMakeLists.txt` 中用 `add_subdirectory` 引入，相互不干扰。
