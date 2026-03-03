# Stage 4：线程 Pipeline 使用指南

> 对应代码：`src/pipeline/`
> 依赖：`mv-pipeline` 静态库（链接 `mv-interfaces`, `mv-hal-camera`, `mv-hal-serial`, `mv-core`）

---

## 概述

`mv-pipeline` 将视觉处理的四个阶段拆分为独立线程，通过有界、可关闭的 `Channel<T>` 串联：

```
CaptureNode  →  DetectNode   →  PredictNode  →  SerialNode
  (ICamera)    (IDetector       (IPredictor      (IShooter
                + ISolver)       + IVoter)         + ISerial)
              frame_ch_        detect_ch_        control_ch_
              容量 2           容量 2             容量 1
```

---

## 快速上手

### 1. 准备依赖

```cpp
#include "pipeline/pipeline.hpp"
#include "factory/factory.hpp"

using namespace mv;
using namespace mv::pipeline;
```

### 2. 创建并启动 Pipeline

```cpp
// 从工厂按配置创建各模块（工厂注册宏在对应 .cpp 中）
auto camera    = Factory<hal::ICamera>::Create("mindvision");
auto detector  = Factory<IDetector>::Create("basic");
auto solver    = Factory<ISolver>::Create("pnp");
auto predictor = Factory<IPredictor>::Create("ekf");
auto voter     = Factory<IVoter>::Create("cooldown");
auto serial    = Factory<hal::ISerial>::Create("uart");
auto shooter   = Factory<IShooter>::Create("rm");

// 初始化各模块（省略错误检查，正式代码须处理 false）
auto cfg = ConfigManager::Instance();
camera->Open(cfg.Get("camera"));
serial->Open(cfg.Get("serial"));
detector->Init(cfg.Get("detector"));
solver->Init(cfg.Get("solver"));
predictor->Init(cfg.Get("predictor"));
voter->Init(cfg.Get("voter"));
shooter->Init(cfg.Get("shooter"));

// Builder 模式构建 Pipeline
auto pipeline = VisionPipeline::Builder{}
    .Camera(std::move(camera))
    .Detector(std::move(detector))
    .Solver(std::move(solver))
    .Predictor(std::move(predictor))
    .Voter(std::move(voter))
    .Serial(std::move(serial))
    .Shooter(std::move(shooter))
    // 可选：自定义通道容量
    // .FrameChannelCapacity(4)
    // .ControlChannelCapacity(2)
    .Build();  // Build() 失败时抛 std::invalid_argument

pipeline->Start();  // 启动所有工作线程
```

### 3. 运行期监控（配合 Stage 5 状态机）

```cpp
// 主循环（由状态机驱动，此处仅示例）
while (pipeline->IsRunning()) {
    if (pipeline->CheckErrors()) {
        // 某节点发生不可恢复错误 → 触发状态机 ERROR 转换
        state_machine.Transition(SystemState::ERROR);
        break;
    }

    // 诊断：检查通道积压
    if (pipeline->FrameChannelSize() >= 2) {
        MV_LOG_WARN("Main", "Frame channel near full, detect may be slow.");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
}
```

### 4. 停止 Pipeline

```cpp
pipeline->Stop();   // 按顺序停止所有节点，阻塞直到线程全部退出
// pipeline 析构时也会自动调用 Stop()
```

### 5. 重置并重新启动（模式切换）

```cpp
pipeline->Stop();
pipeline->Reset();  // 清空所有通道内的残留数据
pipeline->Start();  // 重新启动
```

---

## 核心类说明

### `Channel<T>`

```cpp
// src/pipeline/channel.hpp
Channel<FramePacket> ch{/*容量=*/4};

// 生产者（CaptureNode）
ch.Push(std::move(pkt));         // 队满时丢最旧帧

// 消费者（DetectNode）
FramePacket pkt;
if (ch.Pop(pkt, 10ms)) {         // 10ms 超时，返回 false = 超时/已关闭
    Process(pkt);
}

// 关闭（Stop 时）
ch.Shutdown();                   // 唤醒所有等待的 Pop，使之立刻返回 false

// 诊断
ch.Size()         // 当前积压数
ch.DroppedCount() // 丢弃帧数
```

### `PipelineNode`（基类）

```cpp
// src/pipeline/node.hpp
class MyNode : public PipelineNode {
public:
    MyNode() : PipelineNode("MyNode") {}
protected:
    void WorkLoop() override {
        while (!stop_requested_.load()) {
            // 带超时 Pop → 检查 stop → 处理 → Push
        }
    }
    void OnStop() override {
        input_ch_->Shutdown();  // 唤醒 Pop 等待
    }
};
```

### `VisionPipeline`（编排器）

```cpp
// src/pipeline/pipeline.hpp
pipeline->Start()                  // 启动（逆序：Serial → Predict → Detect → Capture）
pipeline->Stop()                   // 停止（正序：Capture → Detect → Predict → Serial）
pipeline->Reset()                  // 清空通道（Stop 后调用）
pipeline->CheckErrors()            // 任意节点有 error 返回 true
pipeline->IsRunning()              // 所有节点均在运行
pipeline->State().enemy_color      // 共享状态（SerialNode 反向更新）
pipeline->FrameChannelSize()       // 帧通道积压
```

---

## 数据包类型

| 类型 | 生产者 | 消费者 | 主要字段 |
|------|--------|--------|---------|
| `FramePacket` | `CaptureNode` | `DetectNode` | `frame`, `timestamp`, `frame_id` |
| `DetectPacket` | `DetectNode` | `PredictNode` | `detections`（含 PnP 结果）, `frame`, `enemy_color` |
| `ControlPacket` | `PredictNode` | `SerialNode` | `control`（含 fire 标志）, `track_target` |
| `RecvPacket` | `SerialNode` 内部 | `SharedState` | `enemy_color`, `mode`, `bullet_speed` |

---

## 接口扩展点

### 实现 `IVoter`（开火决策）

```cpp
// 最简实现：is_tracking 即开火
class SimpleVoter : public mv::IVoter {
public:
    bool Init(const YAML::Node&) override { initialized_ = true; return true; }
    bool Vote(const TrackTarget& t, const GimbalControl&) override {
        return t.is_tracking;
    }
    void Reset() override {}
    bool IsInitialized() const noexcept override { return initialized_; }
private:
    bool initialized_{false};
};
MV_REGISTER_VOTER("simple", SimpleVoter);
```

### 实现 `IShooter`（串口编码）

```cpp
// 帧格式：[0xAA][yaw_int16][pitch_int16][fire_u8][crc8][0x55]
class RmShooter : public mv::IShooter {
public:
    bool Init(const YAML::Node& cfg) override { /*加载弹道参数*/ return true; }
    bool Send(hal::ISerial& serial, const GimbalControl& ctrl) override {
        // 1. 弹道补偿修正 ctrl.pitch
        // 2. 打包字节帧
        // 3. serial.Send(buf, len)
    }
    bool IsInitialized() const noexcept override { return initialized_; }
private:
    bool initialized_{false};
};
MV_REGISTER_SHOOTER("rm", RmShooter);
```

---

## 性能调优

| 场景 | 调整方式 |
|------|----------|
| 检测慢，帧率低 | 增大 `FrameChannelCapacity`（允许更多积压，但延迟变高） |
| 串口发送满负荷 | 减小 `ControlChannelCapacity`（只保留最新 1 帧指令） |
| 掉帧过多 | 查看 `pipeline->FrameChannelSize()` 和 `Channel::DroppedCount()` |
| 节点崩溃诊断 | `node.HasError()` + `node.ErrorCode()` + 日志 |

---

## 已知限制（待 Stage 5/6 解决）

1. **工厂注册表为空**：`IVoter`/`IShooter`/`IDetector` 等尚无具体实现注册，目前只能用 Mock 进行集成测试。
2. **上行帧协议占位**：`SerialNode::TryRecv()` 使用 5 字节占位协议，正式协议需与下位机对齐后修改。
3. **无 Foxglove 节点**：`ControlPacket` 中包含 `TrackTarget`，预留了旁路 debug channel 的扩展点，Foxglove 节点在 Stage 6 实现。
4. **无状态机集成**：`CheckErrors()` 目前需外部轮询，Stage 5 状态机完成后由 FSM 统一处理。
