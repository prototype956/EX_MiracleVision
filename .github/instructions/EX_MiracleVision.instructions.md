---
description: EX_MiracleVision 项目上下文与编码规范，适用于所有 C++ 源文件的生成、审查和修改。
applyTo: "**/*.{cpp,hpp,h,cc,cxx}"
---

# EX_MiracleVision 项目指令

## 一、项目背景

- **项目名称**：EX_MiracleVision
- **所属团队**：北京林业大学 RoboMaster 2026 视觉组（BJFU RM 2026）
- **用途**：RoboMaster 机器人自动瞄准视觉系统，包含装甲板检测、目标追踪、弹道预测、能量机关识别等功能
- **运行平台**：Ubuntu 22.04 LTS，x86_64，GCC 11.4.0+
- **语言标准**：C++17（部分允许 C++20）
- **主要依赖**：OpenCV 4.5+、Eigen3、ONNX Runtime、OpenVINO、spdlog/fmt、yaml-cpp、TBB

---

## 二、代码架构

### 2.1 两套代码共存

项目目前处于重构过渡期，存在**新旧两套代码**：

| 目录 | 状态 | 说明 |
|------|------|------|
| `base/` + `module/` + `devices/` | **旧版**（逐步淘汰） | 功能完整可运行，勿随意改动 |
| `src/` | **新版**（接口驱动，持续开发） | 所有新功能在此实现 |

**AI 生成的新代码一律放在 `src/` 目录下；除非用户明确要求，不要修改旧版代码。**

### 2.2 新版 `src/` 目录结构

```
src/
├── interfaces/      # 纯虚接口：IDetector / ISolver / IPredictor / IVoter / IShooter
├── modules/         # 接口的具体实现（armor_detector, pnp_solver, ekf_predictor…）
├── factory/         # 模板工厂注册表 Factory<Base>，按字符串键创建实现
├── core/            # 基础设施：ConfigManager（单例）、Logger
├── pipeline/        # 流水线编排：将 Detector→Solver→Predictor→Voter→Shooter 串联
├── app/             # 应用入口，组装各组件
├── fsm/             # 有限状态机（跟踪状态管理）
├── hal/             # 硬件抽象层（相机、串口）
├── tool/            # 调试工具：FoxgloveSink（可视化）、ViewRenderer（本地窗口）
└── test/            # 单元测试 / 集成测试可执行文件
```

### 2.3 命名空间

- 所有新代码使用 `namespace mv {}`
- 工具类放在 `namespace mv::tool {}`
- 旧代码命名空间各异（`basic_armor`, `uart`…），不要在新代码中使用

---

## 三、编码规范

### 3.1 格式

- **风格基准**：Google C++ Style Guide
- **缩进**：2 空格，禁止 Tab
- **行宽**：最大 100 字符
- **格式化工具**：`clang-format`（保存时自动格式化，配置见 `.clang-format`）
- **静态分析**：`clang-tidy`（配置见 `.clang-tidy`）

### 3.2 命名约定

| 类别 | 规则 | 示例 |
|------|------|------|
| 类 / 结构体 | `PascalCase` | `BasicArmorDetector` |
| 函数 / 方法 | `PascalCase` | `Init()`, `Detect()` |
| 局部变量 | `snake_case` | `frame_idx` |
| 成员变量 | `snake_case_` + 尾下划线 | `impl_`, `config_` |
| 常量 / 枚举值 | `kPascalCase` | `kMaxArmors` |
| 接口类 | `I` 前缀 | `IDetector`, `IPredictor` |
| 文件名 | `snake_case` | `basic_armor_detector.hpp` |

### 3.3 枚举

使用 `enum class`，禁止裸 `enum`：

```cpp
// ✅ 正确
enum class ArmorColor : uint8_t { RED = 0, BLUE, UNKNOWN };

// ❌ 错误
enum ArmorColor { RED, BLUE };
```

### 3.4 单位约定

- 3D 坐标：**米（m）**
- 角度：**弧度（rad）**
- 时间戳：`std::chrono::steady_clock::time_point`（单调时钟）
- 禁止混用不同单位，换算必须注释说明

---

## 四、关键设计模式

### 4.1 接口 + 实现分离

所有核心算法组件通过**纯虚接口**定义契约，实现放在 `modules/` 下：

```cpp
// interfaces/i_detector.hpp —— 只定义契约
class IDetector {
 public:
  virtual bool Init(const YAML::Node& config) = 0;
  virtual std::vector<Detection> Detect(const cv::Mat& frame,
                                        ArmorColor enemy_color) = 0;
  virtual ~IDetector() = default;
  IDetector(const IDetector&) = delete;              // 禁止拷贝
  IDetector& operator=(const IDetector&) = delete;
};

// modules/armor_detector/basic_armor_detector.hpp —— 具体实现
class BasicArmorDetector : public IDetector { ... };
```

**接口约定**：
- `Init()` 失败返回 `false`，不抛异常
- 无目标时返回空容器（`{}`），调用方无需 try-catch
- 接口方法均为非线程安全，调用方负责线程隔离

### 4.2 工厂模式（`Factory<Base>`）

通过字符串键注册和创建实现，避免 `main.cpp` 依赖具体类型：

```cpp
// 在各自的 .cpp 中注册（一次，文件作用域）
MV_REGISTER_DETECTOR("basic", BasicArmorDetector);
MV_REGISTER_DETECTOR("dnn",   DnnArmorDetector);

// 在 Pipeline / main 中按配置创建
auto detector = mv::Factory<mv::IDetector>::Create(
    cfg.Get<std::string>("detector.type"));
```

新增实现时，**只需在实现文件注册，`main.cpp` 零修改**。

### 4.3 PImpl 模式

凡是头文件中需要隐藏第三方 SDK 依赖（如 Foxglove SDK、OpenCV 大型头文件）的类，
使用 PImpl 隔离编译：

```cpp
// foxglove_sink.hpp —— 头文件只暴露接口
class FoxgloveSink {
 public:
  void Start();
  void PublishImage(const cv::Mat& img, const std::string& topic);
  // ...
 private:
  struct Impl;                     // 前向声明
  std::unique_ptr<Impl> impl_;     // 实现细节完全隐藏
};
```

- `Impl` 的定义放在对应 `.cpp` 中
- 析构函数必须在 `.cpp` 中定义（`unique_ptr<Impl>` 需要完整类型）

### 4.4 单例 ConfigManager

```cpp
auto& cfg = mv::ConfigManager::Instance();
cfg.Load("configs/vision.yaml");                       // 根命名空间
cfg.Load("configs/armor/basic_armor.yaml", "armor");   // 子命名空间

auto thresh = cfg.Get<int>("armor.light_thresh", 100); // 有默认值，不抛异常
auto fps    = cfg.GetRequired<int>("camera.fps");       // 必须存在，否则抛异常
```

---

## 五、代码注释与文档规范

### 5.1 文件头模板

每个新建 `.hpp` / `.cpp` 文件必须包含以下 Doxygen 文件头：

```cpp
/**
 * @file <文件名>.hpp
 * @brief <一句话描述该文件职责>
 *
 * 【职责边界】（可选，用于描述设计边界）
 *   说明本模块负责什么、不负责什么。
 *
 * 【实现约定】（可选）
 *   - 关键约定 1；
 *   - 关键约定 2。
 */
```

- `@file` 与 `@brief` 为**必填**
- 补充说明用 `【】` 标注中文小节（非 Doxygen 标签）

### 5.2 类与方法注释

```cpp
/**
 * @brief 类的一句话说明
 *
 * 较长的背景描述（可选）。
 *
 * @code
 *   // 典型使用示例
 *   MyClass obj;
 *   obj.Init(config);
 * @endcode
 */
class MyClass {
 public:
  /**
   * @brief 初始化（加载配置/模型）
   * @param config  YAML 配置节点
   * @return true 成功；false 失败（内部已记录日志）
   */
  virtual bool Init(const YAML::Node& config) = 0;
};
```

### 5.3 行内注释原则

- 注释解释**为什么**，而非**是什么**（代码本身即说明 what）
- 关键算法步骤、魔法数字、单位转换必须注释
- 注释语言：**中文为主，术语保留英文**

```cpp
// ✅ 好的注释：解释原因和单位
float dist_m = dist_px * pixel_size_;  // 像素距离 → 物理距离（单位：m）

// ❌ 差的注释：重复代码
float dist_m = dist_px * pixel_size_;  // 将 dist_px 乘以 pixel_size_
```

### 5.4 文档与注释的生成方式

**不要一次性为整个文件补全所有文档或注释**，应按模块/类/函数逐段生成：

1. 先生成文件头（`@file` + `@brief` + 职责边界说明）
2. 再逐个处理各个类或重要函数的 Doxygen 注释
3. 最后补充行内注释（关键算法步骤、魔法数字、单位转换）

这样每次输出量可控，便于人工审查和修改，避免一次性大段输出难以逐行检查。

---

## 六、调试与可视化工具

### 6.1 Foxglove Studio（远程可视化）

`mv::tool::FoxgloveSink` 通过 WebSocket 向 Foxglove Studio 推送调试数据，
**头文件通过 PImpl 隔离 SDK 依赖，上游代码零额外编译代价**。

```cpp
mv::tool::FoxgloveSink sink;   // 默认监听 0.0.0.0:8765
sink.Start();

// 主循环中按需调用
sink.PublishImage(frame, "camera/raw");
sink.PublishDetections(detections);
sink.PublishPnpResult(detections, frame);
sink.PublishGimbalControl(ctrl);
```

- 启用 JPEG 压缩（`config.use_jpeg = true`）可将带宽从 ~300 MB/s 降至 ~3 MB/s
- Foxglove Studio 连接地址：`ws://<设备IP>:8765`

### 6.2 本地调试窗口（ViewRenderer）

`mv::tool::ViewRenderer` 提供基于 OpenCV `imshow` 的本地多视图窗口：

```cpp
mv::tool::ViewRenderer renderer;
renderer.Init("mv-main", "mv-debug");
renderer.SetView(mv::tool::ViewMode::RESULT);  // 1–5 键切换

// 主循环中渲染
renderer.Render(raw, dbg_data, detections, ctrl, frame_idx, fps, params);
```

支持 5 种视图切换（键盘 1–5）：`RESULT` / `DIFF` / `BINARY` / `LIGHTS` / `ROI`

---

## 七、Git 工作流规范

### 7.1 提交信息格式（Conventional Commits）

```
<type>(<scope>): <简短描述>

[可选正文：说明原因或影响]
```

常用 `type`：

| type | 含义 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `refactor` | 重构（不改变行为） |
| `perf` | 性能优化 |
| `docs` | 文档变更 |
| `test` | 测试相关 |
| `chore` | 构建/依赖/配置 |

示例：
```
feat(detector): add DnnArmorDetector based on YOLOv8
fix(predictor): correct EKF state transition matrix units (rad → m)
chore(cmake): link TBB for parallel pipeline execution
```

### 7.2 提交前检查清单

1. `cmake --build build` 无编译错误
2. `clang-format` 已格式化（保存时自动执行）
3. `clang-tidy` 无新增警告
4. 新功能需在 `src/test/` 下附带测试可执行文件

---

## 八、AI 辅助规则

### 8.1 代码生成位置

- 新的实现类 → `src/modules/<模块名>/`
- 新的接口定义 → `src/interfaces/`
- 新的工具类 → `src/tool/`
- 新的测试程序 → `src/test/`
- **禁止**在 `module/`、`base/`、`devices/`（旧版目录）中新增文件，除非用户明确要求

### 8.2 生成新模块的步骤顺序

1. 确认对应接口（`IDetector` / `ISolver` / `IPredictor`…）是否已存在
2. 在 `src/modules/<name>/` 下创建 `.hpp` + `.cpp`，继承接口
3. 在 `.cpp` 文件作用域用宏注册到工厂（如 `MV_REGISTER_DETECTOR("name", ClassName)`）
4. 在 `src/modules/CMakeLists.txt` 中添加新目标

### 8.3 禁止事项

- 不要在头文件中 `#include` Foxglove SDK / 大型第三方 SDK，改用 PImpl
- 不要在接口文件中引入具体实现的头文件
- 不要在新代码中使用旧命名空间（`basic_armor::`, `uart::` 等）
- 不要在 `Init()` / `Detect()` 等接口方法中抛出异常（用返回值表达失败）
- 不要混用单位（坐标必须是米，角度必须是弧度）

### 8.4 生成代码时默认假设

- 编译器：GCC 11.4.0，标准：C++17
- 构建命令：`cmake -S . -B build && cmake --build build -j$(nproc)`
- 配置文件路径：`configs/vision.yaml`（根），子模块配置在 `configs/` 下对应子目录
- 日志：使用 `mv::Logger`（封装 spdlog），不要直接调用 `std::cout` 或 `printf`

### 8.5 与本文件冲突时的处理

每次生成代码后，如果生成的内容与本指令文件中的规范存在冲突（例如命名风格特殊、
接口约定例外、临时绕过某项限制等），**必须在输出结束后主动告知用户**：

> ⚠️ 本次生成的 `xxx` 与指令文件中「…」规范存在冲突，请决定：
> - A. 调整实现方案以符合规范
> - B. 修改指令文件以反映新约定

由用户决策，AI 不得自行忽略冲突或静默偏离规范。

---

## 九、协同调试开发规则（用户指定）

以下规则用于“重写一套调试程序”期间的人机协作，优先级高于默认工作习惯：

### 9.1 先定位模块，再决定是否写代码

- 收到需求后，先做模块映射：定位对应目录、接口、实现类与入口调用链。
- 默认先给出“应调用哪个模块、从哪里调用、调用顺序是什么”，不直接生成代码。
- 只有在用户明确下达“生成代码/开始修改”指令后，才进入代码编辑阶段。

### 9.2 对难理解模块先补注释说明

- 对用户标记为“看不懂”的模块，先补充说明性注释（优先函数级/关键步骤级），再推进功能改动。
- 注释应解释“为什么这样做、输入输出、关键约束与单位”，避免重复代码字面含义。
- 注释补充遵循“小步提交”原则，不一次性改完整个文件。

### 9.3 现有模块改动采用“指导下微调”

- 对已有模块的行为修改，默认不主动大改，不整文件重构。
- 每次改动前先说明拟修改点与影响范围，按用户反馈进行微调。
- 未经用户确认，不变更公共接口、目录结构和跨模块调用关系。

### 9.4 用户指定生成代码时，单次仅改一个函数

- 当用户明确要求生成代码时：单次编辑只修改一个函数（可含其最小必要上下文）。
- 每次修改后必须同步说明：
  1. 为什么这样改（问题根因与设计取舍）；
  2. 下一步建议改哪个函数或做哪项验证。
- 禁止单次覆盖式改写整个文件，除非用户明确批准。
