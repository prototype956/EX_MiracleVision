# EX-AT Integration Overview (No ROS2)

本文档说明第一迭代接入方案的模块边界、调用链和改动范围。目标是让 EX 在不引入 ROS2 的前提下，实时消费 AT 仿真数据并跑通现有视觉流水线。

## 1. 目标与边界

第一迭代目标：

- AT 实时输出图像和位姿。
- EX 使用新后端 SimCamera 接入输入。
- EX 维持原有 Pipeline：Capture -> Detect -> Predict -> Serial。

第一迭代明确不做：

- 不做控制闭环回注。
- 不改 EX 旧目录 `base/`, `module/`, `devices/`。
- 不改公共接口 `IDetector/ISolver/IPredictor/IVoter/IShooter`。

## 2. 模块映射

EX 路径：

- 入口：[src/app/main.cpp](../../src/app/main.cpp)
- 相机接口：[src/hal/camera/i_camera.hpp](../../src/hal/camera/i_camera.hpp)
- 采集节点：[src/pipeline/capture_node.cpp](../../src/pipeline/capture_node.cpp)

AT 路径：

- 主入口：`/home/nuc/at_vision_simulator/src/main.rs`
- 图像可复用点：`/home/nuc/at_vision_simulator/src/ros2/capture.rs`
- 位姿可复用点：`/home/nuc/at_vision_simulator/src/ros2/plugin.rs`

## 3. 调用链

AT 侧：

1. 渲染获得图像缓冲。
2. SimBridge 读取图像并打包为 0x01。
3. SimBridge 读取位姿并打包为 0x02。
4. 通过 TCP 推送给 EX。

EX 侧：

1. `main()` 根据 `camera.backend=sim` 创建 SimCamera。
2. `CaptureNode::WorkLoop()` 调用 `camera->Grab(frame)`。
3. SimCamera 解包并输出 `cv::Mat`。
4. 下游 Detect/PnP/Predict 保持不变。

## 4. 配置建议

建议在 EX profile 中增加以下参数：

```yaml
camera:
  backend: sim
  sim_endpoint: 127.0.0.1:19090
  sim_connect_timeout_ms: 2000
  sim_reconnect_interval_ms: 500
  sim_max_payload_bytes: 8388608
```

## 5. 里程碑与升级路径

Milestone B（本轮）：实时输入联调。

Milestone A（下一轮）：在 EX 新增 SimSerial，在 AT 增加控制接收与状态反馈，形成闭环。

升级时保持原则：优先新增消息类型，不破坏已运行的图像与位姿链路。
