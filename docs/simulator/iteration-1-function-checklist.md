# Iteration 1 Function-Level Checklist

本清单对应 Milestone B（实时输入，不闭环），按函数级改动顺序执行。每个步骤只改一个函数或一个最小函数组，便于回滚与审查。

## 阶段 0：文档冻结

1. 冻结协议字段与单位（本目录 `stream-protocol-spec.md`）。
2. 冻结模块边界与配置口径（本目录 `integration-overview.md`）。

## 阶段 1：AT 推流最小实现

1. 修改 `main()`
   - 文件：`/home/nuc/at_vision_simulator/src/main.rs`
   - 改动：注册 SimBridge 插件。

2. 实现 `SimBridgePlugin::build()`
   - 文件：`/home/nuc/at_vision_simulator/src/sim_bridge/mod.rs`（新增）
   - 改动：创建桥接资源、启动监听系统。

3. 实现图像发送函数（建议命名 `publish_image_frame`）
   - 文件：`/home/nuc/at_vision_simulator/src/sim_bridge/mod.rs`
   - 改动：编码消息头 + 0x01 payload 并发送。

4. 在 `receive_image_from_buffer()` 增加调用
   - 文件：`/home/nuc/at_vision_simulator/src/ros2/capture.rs`
   - 改动：在现有发布路径旁路调用 `publish_image_frame`。

5. 实现位姿发送函数（建议命名 `publish_pose_snapshot`）
   - 文件：`/home/nuc/at_vision_simulator/src/sim_bridge/mod.rs`
   - 改动：编码消息头 + 0x02 payload 并发送。

6. 在 `capture_rune()` 增加调用
   - 文件：`/home/nuc/at_vision_simulator/src/ros2/plugin.rs`
   - 改动：旁路调用 `publish_pose_snapshot`。

## 阶段 2：EX 收流最小实现

1. 实现 `SimCamera::Open()`
   - 文件：`src/hal/camera/sim_camera.cpp`（新增）
   - 改动：解析 YAML、建立 TCP 连接、初始化状态。

2. 实现 `SimCamera::Grab()`
   - 文件：`src/hal/camera/sim_camera.cpp`
   - 改动：接收包头、校验、解析 0x01、JPEG 解码输出 `cv::Mat`。

3. 实现 `SimCamera::Close()`
   - 文件：`src/hal/camera/sim_camera.cpp`
   - 改动：关闭 socket、清理资源、保证幂等。

4. 实现 `SimCamera::IsOpen()`
   - 文件：`src/hal/camera/sim_camera.cpp`
   - 改动：返回当前连接状态。

5. 修改 `main()` 相机后端分支
   - 文件：`src/app/main.cpp`
   - 改动：支持 `camera.backend=sim`，实例化 SimCamera。

6. 修改相机 CMake 目标
   - 文件：`src/hal/camera/CMakeLists.txt`
   - 改动：接入 `sim_camera.cpp`。

## 阶段 3：联调验证

1. AT 启动并监听端口。
2. EX 启动并连接，确认 `CaptureNode` 持续取帧。
3. 检查 Detect/PnP/Predict 输出稳定。
4. 记录端到端延迟与丢帧率。

## 第一迭代完成判据

- EX 在 sim 相机模式下连续运行 10 分钟无崩溃。
- 有稳定帧输入，算法链路持续运行。
- 未引入 ROS2 依赖，未改旧目录，未改公共接口。
