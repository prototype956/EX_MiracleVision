# SimSerial 模块说明

## 目标

在仿真联调场景下，用 TCP 替代真实 UART 串口，实现 EX 与 AT 仿真器之间的串口字节流交互，并保证断连时不把 FSM 拉入 ERROR。

## 代码位置

- 实现：src/hal/serial/sim_serial.hpp
- 实现：src/hal/serial/sim_serial.cpp
- 装配：src/app/main.cpp

## 接口契约

SimSerial 实现 ISerial 全接口：

- Open(config)
  - 读取 endpoint/connect_timeout_ms/recv_timeout_ms/reconnect_interval_ms。
  - 配置合法后返回 true；若首次连接失败，进入软降级重连模式。
- Send(data, len)
  - 已连接：透传字节流到 TCP。
  - 断连：返回 true（软降级），并按重连间隔自动重连。
- Recv(buf, len, received)
  - 已连接：读取 TCP 数据。
  - 未连接或无数据：返回 false。
- IsOpen()
  - 返回逻辑打开状态（Open 成功后为 true，直到 Close）。

## 断连语义

- 设计目标是“连续调试优先”。
- 断连期间 Send 返回 true，避免 SerialNode 连续失败计数触发 SetError(2)。
- 连接恢复后自动继续收发，不需要重启主程序。

## 配置键

在 configs/vision.yaml 和 src/config/vision.yaml 的 serial 节点中新增：

- backend: "uart" | "sim"
- sim_endpoint: 默认 127.0.0.1:19091
- sim_connect_timeout_ms: 连接超时（ms）
- sim_recv_timeout_ms: 接收超时（ms）
- sim_reconnect_interval_ms: 重连间隔（ms）

## 与主流程关系

- main.cpp 根据 serial.backend 选择 UartSerial 或 SimSerial。
- Pipeline/SerialNode/RmShooter 不需要分支判断；保持原有调用链。
- 仿真模式建议组合：camera.backend=sim + serial.backend=sim。
