# Stage 3 — 接口层与工厂系统使用指南

> 文档版本：2026-03-02
> 对应提交：`8ee585a` 起

---

## 一、为什么需要接口层与工厂？

### 旧代码的困境

```cpp
// main.cpp（旧）
// 每次新增检测器都要改 main，违反开闭原则
if (mode == "basic") {
    auto det = std::make_unique<basic_armor::Armor>(camera_cfg, armor_cfg);
} else if (mode == "dnn") {
    auto det = std::make_unique<DNN_armor::DNN_Detect_CV>(model_path);
}
// detector 的类型签名完全不同，无法多态替换
```

| 问题 | 旧代码 | 新设计 |
|------|--------|--------|
| 算法替换 | 修改 main.cpp + 适配新接口 | 只改 YAML 配置 `type: "dnn"` |
| 单元测试 | 必须真实相机 + 真实算法 | 注入 `MockDetector` 即可 |
| 模块间依赖 | 检测器头文件渗透到 Pipeline | 只依赖 `IDetector` 接口 |
| 数据类型统一 | 各模块自定义结构体 | 统一 `Detection`、`GimbalControl` |

---

## 二、目录结构

```
src/
├── interfaces/
│   ├── types.hpp          ← 跨层共享数据类型（零业务逻辑）
│   ├── i_detector.hpp     ← IDetector 纯虚接口
│   ├── i_solver.hpp       ← ISolver 纯虚接口
│   ├── i_predictor.hpp    ← IPredictor 纯虚接口
│   └── CMakeLists.txt     ← mv-interfaces INTERFACE 库
└── factory/
    ├── factory.hpp        ← Factory<Base> 模板注册表 + 注册宏
    └── CMakeLists.txt     ← mv-factory INTERFACE 库
```

---

## 三、共享数据类型（types.hpp）

`types.hpp` 是唯一跨层传递的数据契约，所有模块只依赖它，不互相 include。

### 基础枚举

```cpp
enum class ArmorColor  : uint8_t { RED, BLUE, UNKNOWN };
enum class ArmorType   : uint8_t { SMALL, BIG };
enum class ArmorNumber : uint8_t { ONE, TWO, THREE, FOUR, FIVE,
                                   SENTRY, OUTPOST, BASE, UNKNOWN };
```

> **命名约定**：枚举常量遵循项目 `.clang-tidy` 规则，使用 `UPPER_CASE`。

### Detection（检测结果）

IDetector 输出，ISolver 就地填充。

```cpp
mv::Detection det;

// 检测器填充（2D）
det.color      = mv::ArmorColor::RED;
det.type       = mv::ArmorType::SMALL;
det.number     = mv::ArmorNumber::THREE;
det.points     = { /* 4 个角点，顺序：左下、右下、右上、左上 */ };
det.box        = cv::Rect2f{...};
det.confidence = 0.95F;

// 解算器填充（3D）— 由 ISolver::Solve() 写入
// det.is_solved        → true
// det.xyz_in_gimbal    → Eigen::Vector3d（云台坐标系，单位 m）
// det.yaw_angle        → 偏角（rad）
// det.pitch_angle      → 俯仰角（rad）

// 辅助方法
cv::Point2f center = det.Center();  // 四角点像素中心
```

### GimbalControl（云台指令）

IPredictor 输出，直接送串口序列化。

```cpp
mv::GimbalControl ctrl = predictor->Predict(detections, t, enemy_color);

if (ctrl.tracking) {
    // ctrl.yaw / ctrl.pitch / ctrl.distance 有效
    serial->Send(...);
}
if (ctrl.fire) { /* 请求开火 */ }
```

### TrackTarget（跟踪状态）

`GetTrackTarget()` 返回的状态有两类消费者，**目前只实现了可视化，Voter/Shooter 尚待实现**：

| 消费者 | 读取字段 | 用途 | 状态 |
|--------|----------|------|------|
| **Voter**（待实现）| `is_tracking` / `tracker_state` / `number` / `color` | 结合击打优先级和冷却时间，决策是否允许开火，输出 `fire` 信号给 Shooter | ❌ 未实现 |
| **Shooter**（待实现）| `yaw_predicted` / `pitch_predicted` / `distance` | 接收 Voter 的开火指令，叠加弹道补偿后编码写入串口 | ❌ 未实现 |
| **Foxglove 可视化** | `position` / `velocity` / `yaw_predicted` / `tracker_state` | 上位机 UI 绘制跟踪轨迹和预测落点 | ✅ 可接入 |

```cpp
mv::TrackTarget target = predictor->GetTrackTarget();
// target.tracker_state : "lost" / "detecting" / "tracking" / "temp_lost"
// target.is_tracking   : bool
// target.position      : Eigen::Vector3d（云台坐标系，m）
// target.velocity      : Eigen::Vector3d（m/s）
// target.yaw_predicted : rad（已含飞行时间补偿）

// TODO: 传给 Voter 决策开火
// TODO: Voter 输出 fire=true 时交给 Shooter 编码写串口
```

> **实现提示**：Voter 和 Shooter 应作为独立接口（`IVoter` / `IShooter`）实现，
> 通过工厂注册，数据来源是 `GetTrackTarget()` 而不是重新传递 `Detection` 列表。

---

## 四、业务接口说明

### IDetector — 目标检测器

```
输入：cv::Mat（相机帧）+ ArmorColor（敌方颜色）
输出：std::vector<Detection>（只含 2D 信息，is_solved = false）
```

```cpp
#include "interfaces/i_detector.hpp"

// 创建（通过工厂，见第五节）
auto detector = mv::Factory<mv::IDetector>::Create("basic");

// 初始化
YAML::Node det_cfg = cfg.GetNode("detector");
if (!detector->Init(det_cfg)) { /* 日志 + 退出 */ }

// 每帧调用
cv::Mat frame;
camera->Grab(frame);
auto detections = detector->Detect(frame, mv::ArmorColor::RED);
```

**接口约定**：
- `Init()` 失败返回 `false`，不抛异常；可重试
- `Detect()` 无目标返回空 `vector`，不需要 try-catch
- 非线程安全，在专属检测线程中顺序调用

### ISolver — PnP 位姿解算器

```
输入：Detection（含 4 个像素角点）
输出：就地填充 Detection.xyz_in_gimbal / yaw_angle / pitch_angle
```

```cpp
#include "interfaces/i_solver.hpp"

auto solver = mv::Factory<mv::ISolver>::Create("pnp");
solver->Init(cfg.GetNode("solver"));  // 需要相机内参

for (auto& det : detections) {
    if (solver->Solve(det)) {
        // det.is_solved == true
        // det.xyz_in_gimbal 已填充
    }
}
```

**坐标系约定**：
- 相机坐标系（OpenCV 标准）：右 +X，下 +Y，前 +Z
- 云台坐标系（输出，RoboMaster 惯例）：右 +X，上 +Y，前 +Z
- 解算器内部完成坐标转换，调用方无感知

### IPredictor — 跟踪与预测器

```
输入：std::vector<Detection>（已解算）+ 时间戳 + 敌方颜色
输出：GimbalControl（云台指令）
```

```cpp
#include "interfaces/i_predictor.hpp"

auto predictor = mv::Factory<mv::IPredictor>::Create("ekf");
predictor->Init(cfg.GetNode("predictor"));

// 每帧调用（时间戳在 Grab() 后立即打）
auto ts = std::chrono::steady_clock::now();
mv::GimbalControl ctrl = predictor->Predict(detections, ts, enemy_color);

// 模式切换时重置
predictor->Reset();
```

**时间戳设计**：使用 `steady_clock`（单调时钟），不受系统时间跳变影响，EKF 的 `dt` 计算稳定。

---

## 五、工厂系统（factory.hpp）

### 原理

```
注册阶段（main() 之前，通过全局静态初始化完成）
  BasicArmorDetector.cpp → MV_REGISTER_DETECTOR("basic", BasicArmorDetector)

使用阶段（main() 中）
  Factory<IDetector>::Create("basic")  → new BasicArmorDetector()
```

利用 C++ 全局静态变量在 `main()` 之前初始化的特性，`Register()` 调用在程序启动时自动完成，`main()` 只需调用 `Create()`。

### 注册一个新实现

```cpp
// BasicArmorDetector.cpp 的顶部（文件作用域，在 namespace 之外）
#include "factory/factory.hpp"
#include "interfaces/i_detector.hpp"

// 注册宏 —— 程序启动时自动执行，main() 无需修改
MV_REGISTER_DETECTOR("basic", BasicArmorDetector)
```

其他接口的专属宏：

| 宏 | 用途 |
|----|------|
| `MV_REGISTER_DETECTOR(key, Type)` | 注册 `IDetector` 实现 |
| `MV_REGISTER_SOLVER(key, Type)` | 注册 `ISolver` 实现 |
| `MV_REGISTER_PREDICTOR(key, Type)` | 注册 `IPredictor` 实现 |
| `MV_REGISTER_CAMERA(key, Type)` | 注册 `ICamera` 实现（HAL） |
| `MV_REGISTER_SERIAL(key, Type)` | 注册 `ISerial` 实现（HAL） |

### 在 main() / Pipeline 中按配置创建

```cpp
#include "factory/factory.hpp"
#include "interfaces/i_detector.hpp"
#include "interfaces/i_solver.hpp"
#include "interfaces/i_predictor.hpp"

auto& cfg = mv::ConfigManager::Instance();

// 从 YAML 读 type，Create 找不到 key 返回 nullptr
auto detector = mv::Factory<mv::IDetector>::Create(
    cfg.Get<std::string>("detector.type", "basic"));
if (!detector) {
    MV_LOG_ERROR("Main", "未知 detector 类型: {}，已注册: {}",
                 cfg.Get<std::string>("detector.type", "basic"),
                 fmt::join(mv::Factory<mv::IDetector>::Keys(), ", "));
    return 1;
}

auto solver    = mv::Factory<mv::ISolver>::Create(
    cfg.Get<std::string>("solver.type", "pnp"));
auto predictor = mv::Factory<mv::IPredictor>::Create(
    cfg.Get<std::string>("predictor.type", "ekf"));
```

对应 `configs/vision.yaml`：

```yaml
detector:
  type: "basic"     # 改为 "dnn" 即切换到深度学习检测器，main.cpp 零修改
  # ...（具体参数）

solver:
  type: "pnp"
  # ...

predictor:
  type: "ekf"
  # ...
```

### 查询已注册的实现

```cpp
// 启动时打印，便于诊断
for (auto& key : mv::Factory<mv::IDetector>::Keys()) {
    MV_LOG_INFO("Main", "已注册检测器: {}", key);
}

// 检查某个 key 是否存在
if (!mv::Factory<mv::IDetector>::Has("dnn")) {
    MV_LOG_WARN("Main", "dnn 检测器未注册（可能未编译 USE_ONNXRUNTIME）");
}
```

---

## 六、完整流水线示意（三层串联）

```
camera->Grab(frame)         ← ICamera（HAL）
    │
    ▼  时间戳打在这里
detector->Detect(frame)     ← IDetector
    │  std::vector<Detection>（is_solved=false）
    ▼
for det: solver->Solve(det) ← ISolver（就地填充 xyz）
    │  std::vector<Detection>（is_solved=true）
    ▼
predictor->Predict(dets,ts) ← IPredictor（EKF 跟踪）
    │  GimbalControl
    ▼
serial->Send(encode(ctrl))  ← ISerial（HAL）
```

每个箭头处的接口类型固定，可以独立替换任意一层的实现而不影响其他层。

---

## 七、CMake 接入

```cmake
# 只需链接对应的 INTERFACE 库
target_link_libraries(your_target
    PRIVATE
        mv-interfaces   # Detection / GimbalControl / IDetector / ISolver / IPredictor
        mv-factory      # Factory<> 模板 + MV_REGISTER_* 宏
)
```

`mv-interfaces` 和 `mv-factory` 均为纯头文件 INTERFACE 库，链接后自动传播：
- `src/` include 路径
- `mv-core`（Logger + ConfigManager）
- `opencv_core`、`yaml-cpp`、`Eigen3::Eigen`
- `mv-hal-camera`、`mv-hal-serial`

---

## 八、Mock 示例（单元测试）

```cpp
// test_pipeline.cpp — 不需要真实硬件或算法
class MockDetector : public mv::IDetector {
 public:
  bool Init(const YAML::Node&) override { return true; }
  std::vector<mv::Detection> Detect(const cv::Mat&,
                                     mv::ArmorColor) override {
    mv::Detection det;
    det.color  = mv::ArmorColor::RED;
    det.type   = mv::ArmorType::SMALL;
    det.number = mv::ArmorNumber::THREE;
    det.confidence = 1.0F;
    return {det};
  }
  bool IsInitialized() const noexcept override { return true; }
};

// 注册 mock 实现（测试文件顶部）
MV_REGISTER_DETECTOR("mock", MockDetector)

// 测试函数
void TestPipeline() {
    auto det = mv::Factory<mv::IDetector>::Create("mock");
    auto results = det->Detect({}, mv::ArmorColor::RED);
    assert(results.size() == 1);
    assert(results[0].number == mv::ArmorNumber::THREE);
}
```
