# Stage 5：FSM 状态机使用指南

> 对应代码：`src/fsm/`
> 依赖：`mv-fsm` 静态库（链接 `mv-pipeline`, `mv-core`）

---

## 概述

`mv-fsm` 分为两层：

```
StateMachine<StateEnum, Context>   ← 通用模板（header-only，零业务依赖）
        │
        ▼
VisionFSM                          ← 视觉系统专用状态机

状态流：
  IDLE ──Start()──► INIT ──稳定──► AUTO_AIM ◄──► ENERGY_BUFF（预留）
                            │            │
                       StartError    CheckErrors
                            │            │
                            ▼            ▼
                          ERROR ──冷却──► RECOVERY ──Reset──► INIT
  ※ 任意状态 ──Stop()──► IDLE
```

`VisionFSM` 持有 `VisionPipeline` 的所有权，负责驱动其 `Start()` / `Stop()` / `Reset()` 生命周期，并在 `CheckErrors()` 触发时自动进入 ERROR → RECOVERY 流程（最多 3 次）。

---

## 快速上手

### 1. 构建 Pipeline 并注入 FSM

```cpp
#include "fsm/vision_fsm.hpp"
#include "pipeline/pipeline.hpp"
#include "factory/factory.hpp"

using namespace mv;
using namespace mv::fsm;

// Stage 4：构建 Pipeline（详见 PIPELINE_USAGE.md）
auto pipeline = pipeline::VisionPipeline::Builder{}
    .Camera(Factory<hal::ICamera>::Create("mindvision"))
    .Detector(Factory<IDetector>::Create("basic"))
    .Solver(Factory<ISolver>::Create("pnp"))
    .Predictor(Factory<IPredictor>::Create("ekf"))
    .Voter(Factory<IVoter>::Create("cooldown"))
    .Serial(Factory<hal::ISerial>::Create("uart"))
    .Shooter(Factory<IShooter>::Create("rm"))
    .Build();

// Stage 5：FSM 接管 pipeline 所有权
VisionFSM fsm(std::move(pipeline));
```

### 2. 启动并运行主循环

```cpp
// IDLE → INIT → AUTO_AIM（自动流转）
fsm.Start();

while (true) {
    fsm.Update();   // 驱动一次状态机，建议 10~50ms 间隔

    // 正常退出条件（外部收到退出信号后调用 fsm.Stop()）
    if (fsm.CurrentState() == SystemState::IDLE) {
        break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
```

### 3. 停止

```cpp
fsm.Stop();    // 任意状态 → IDLE，自动停止 Pipeline，幂等
// fsm 析构时也会自动停止 Pipeline
```

### 4. 外部干预（可选）

```cpp
// 主动触发恢复（跳过 ERROR 冷却期）
fsm.Recover();

// 强制跳转到指定状态（下一帧 Update() 时生效）
fsm.RequestTransition(SystemState::IDLE);
```

---

## 核心类说明

### `StateMachine<StateEnum, Context>`（通用模板）

```cpp
// src/fsm/state_machine.hpp

StateMachine<State, MyCtx> sm;

// 注册状态处理器（必须在 Init() 之前）
sm.Register(State::IDLE, {
    .on_enter  = [](MyCtx& c) { /* 进入时 */ },
    .on_exit   = nullptr,          // 置 nullptr = 静默跳过
    .on_update = [](MyCtx& c) { /* 每帧轮询 */ },
});

// 初始化（触发初始状态的 on_enter）
sm.Init(State::IDLE, ctx);

// 主循环驱动
sm.Update(ctx);                    // 触发 on_update

// 强制状态转换：on_exit(cur) → current_ = next → on_enter(next)
sm.Transition(State::RUN, ctx);

// 查询
sm.Current()            // 当前状态枚举值
sm.IsInitialized()      // 是否已调用 Init()
sm.RegisteredCount()    // 已注册状态数
```

> **禁止重入**：不允许在 `on_enter` / `on_exit` 内部嵌套调用 `Transition()`，
> 如需链式跳转须通过 `on_update` 的 pending 机制延迟一帧。

### `VisionFSM`

```cpp
// src/fsm/vision_fsm.hpp + vision_fsm.cpp

// 生命周期
fsm.Start()                           // IDLE → INIT（随后自动 → AUTO_AIM）
fsm.Stop()                            // 任意 → IDLE（幂等，停止 Pipeline）
fsm.Update()                          // 主循环每帧调用

// 外部干预（均在主循环线程调用，非线程安全）
fsm.RequestTransition(SystemState::X) // 下一帧生效
fsm.Recover()                         // 等价于 RequestTransition(RECOVERY)

// 查询
fsm.CurrentState()                    // 当前 SystemState
fsm.IsRunning()                       // AUTO_AIM 或 ENERGY_BUFF
fsm.HasError()                        // 处于 ERROR 状态
fsm.LastErrorCode()                   // 最近错误码（0 = 无）
fsm.Pipeline()                        // 只读访问 VisionPipeline
```

### `SystemContext`（上下文字段速查）

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `pipeline` | `unique_ptr<VisionPipeline>` | 构造时注入 | Pipeline 所有权 |
| `requested_state` | `SystemState` | IDLE | Pending 转换目标 |
| `has_pending_transition` | `bool` | false | 是否有待处理转换 |
| `last_error_code` | `int` | 0 | 最近错误码 |
| `recovery_attempts` | `int` | 0 | 本次故障累计恢复次数 |
| `MAX_RECOVERY` | `int` | **3** | 最大自动恢复次数 |
| `INIT_STABILIZE_MS` | `milliseconds` | **200ms** | INIT 稳定等待时长 |
| `ERROR_WAIT_MS` | `milliseconds` | **500ms** | ERROR 冷却时长 |

---

## 状态行为一览

| 状态 | on_enter | on_update 离开条件 | on_exit |
|------|----------|-------------------|---------|
| **IDLE** | `pipeline->Stop()`（若在运行） | 无，被动等待 `Start()` | — |
| **INIT** | `pipeline->Start()`，记录时间戳 | 稳定期到 & `IsRunning()` → AUTO_AIM；`CheckErrors()` → ERROR | — |
| **AUTO_AIM** | 日志记录敌方颜色 | `CheckErrors()` → ERROR；mode=1 → ENERGY_BUFF（预留） | 输出日志 |
| **ENERGY_BUFF** | 输出日志 | `CheckErrors()` → ERROR；mode=0 → AUTO_AIM（预留） | 输出日志 |
| **ERROR** | `pipeline->Stop()`，记录时间戳 | 冷却 500ms 且次数 < 3 → RECOVERY；否则等待人工干预 | — |
| **RECOVERY** | `recovery_attempts++`，`pipeline->Reset()` | 立即 → INIT（Reset 为同步操作） | — |

---

## 状态转换规则汇总

| 触发来源 | 当前 | 目标 | 条件 |
|---------|------|------|------|
| `VisionFSM::Start()` | IDLE | INIT | 外部调用 |
| `InitOnUpdate` | INIT | AUTO_AIM | `IsRunning()` && 稳定期到 |
| `InitOnUpdate` | INIT | ERROR | `CheckErrors()` |
| `AutoAimOnUpdate` | AUTO_AIM | ERROR | `CheckErrors()` |
| `AutoAimOnUpdate` | AUTO_AIM | ENERGY_BUFF | mode=1（预留）|
| `EnergyBuffOnUpdate` | ENERGY_BUFF | ERROR | `CheckErrors()` |
| `EnergyBuffOnUpdate` | ENERGY_BUFF | AUTO_AIM | mode=0（预留）|
| `ErrorOnUpdate` | ERROR | RECOVERY | 冷却结束 && `recovery_attempts < 3` |
| `RecoveryOnUpdate` | RECOVERY | INIT | Reset 完成（下一帧）|
| `VisionFSM::Stop()` | 任意 | IDLE | 外部调用（幂等）|
| `VisionFSM::Recover()` | 任意 | RECOVERY | 外部调用 |

---

## 线程安全说明

- `VisionFSM` **不是线程安全的**，`Update()` / `Start()` / `Stop()` / `RequestTransition()` 均应在**同一主循环线程**调用；
- Pipeline 内部的多线程竞争（4 个节点子线程）由 Pipeline 自身同步机制保证，不属于 FSM 管辖；
- 若需跨线程触发状态变化，需外部加锁，或将请求投递到主循环线程队列。

---

## 扩展指南

### 新增状态（以 CALIBRATION 为例）

```cpp
// 1. vision_fsm.hpp — 枚举中添加
enum class SystemState : uint8_t {
    ...,
    CALIBRATION,   // 标定模式
};

// 2. vision_fsm.hpp — 前向声明
namespace handlers {
    void CalibrationOnEnter(SystemContext& ctx);
    void CalibrationOnUpdate(SystemContext& ctx);
}

// 3. vision_fsm.cpp — 实现回调
void CalibrationOnEnter(SystemContext& ctx) { ... }
void CalibrationOnUpdate(SystemContext& ctx) { ... }

// 4. vision_fsm.cpp — RegisterHandlers() 中注册
sm_.Register(S::CALIBRATION, H{
    .on_enter  = handlers::CalibrationOnEnter,
    .on_update = handlers::CalibrationOnUpdate,
});

// 5. SystemStateName() switch 中添加字符串
case SystemState::CALIBRATION: return "CALIBRATION";
```

### 调整恢复参数

```cpp
// 修改 SystemContext 中的常量（vision_fsm.hpp）
static constexpr int MAX_RECOVERY = 5;              // 最大恢复次数
static constexpr auto ERROR_WAIT_MS = std::chrono::milliseconds(1000);  // 冷却时间
static constexpr auto INIT_STABILIZE_MS = std::chrono::milliseconds(500); // 稳定等待
```

---

## 已知限制（待 Stage 6 解决）

1. **ENERGY_BUFF 仅为框架**：`EnergyBuffOnUpdate` 中的 mode 切换逻辑已预留注释，需对接上行协议后填入。
2. **上行 mode 字段未接入**：`SystemContext` 中暂无 mode 字段，依赖 `VisionPipeline::SharedState` 扩展后接入（当前 SharedState 只有 `enemy_color`）。
3. **错误码未细化**：`last_error_code` 目前始终为 0，需 ERROR 状态进入时从各节点读取 `ErrorCode()` 填入，可在 Stage 6 精细化。
4. **无外部信号处理**：SIGUSR1 热重载、SIGTERM 优雅退出尚未与 FSM 集成，Stage 6 在 `main.cpp` 中处理。
