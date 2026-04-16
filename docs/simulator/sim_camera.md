# sim_camera 模块说明

## 目标

sim_camera 模块用于将 at_vision_simulator 的实时 TCP 流接入 EX_MiracleVision 的相机抽象层，使现有流水线无需改动即可消费仿真图像。

## 代码落位

- 头文件: src/hal/camera/sim_camera.hpp
- 实现: src/hal/camera/sim_camera.cpp
- 入口接入: src/app/main.cpp

## 接口契约

SimCamera 实现 ICamera：

- Open(config): 建立 TCP 连接，失败返回 false。
- Grab(frame): 读取协议包，优先解析图像消息并输出 BGR cv::Mat。
- Close(): 关闭连接，幂等。
- IsOpen(): 返回当前连接状态。

失败策略与 ICamera 一致：不抛异常，调用方通过返回值处理。

## 协议要点

消息头固定 24 字节，包含 magic/version/type/seq/ts/size/crc。

图像消息 payload：

- width(u16 LE)
- height(u16 LE)
- channels(u8, 固定 3)
- encoding(u8, 固定 1 表示 jpeg)
- quality(u8)
- reserved(u8)
- jpeg bytes

校验策略：

- magic/version 不匹配：断流并等待重连。
- payload 超限：断流并等待重连。
- CRC 不匹配：丢包继续读下一包。

## 配置项

配置文件 camera 节点新增以下键：

- sim_endpoint
- sim_connect_timeout_ms
- sim_recv_timeout_ms
- sim_reconnect_interval_ms
- sim_max_payload_bytes

## 调用链

main 选择 camera.backend=sim 后创建 SimCamera，后续调用链保持不变：

CaptureNode -> DetectNode -> PredictNode -> SerialNode。

## 边界

第一迭代仅接入图像流，不处理闭环控制和上行状态。闭环控制由后续 SimSerial 阶段接入。
