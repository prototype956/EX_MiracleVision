# EX-AT Stream Protocol Spec (Milestone B)

本文档定义 EX_MiracleVision 与 at_vision_simulator 在第一迭代使用的 TCP 流协议。第一迭代只覆盖实时输入（图像 + 位姿），不覆盖闭环控制。

## 1. 连接模型

- 传输协议：TCP。
- 拓扑：AT 作为服务端，EX 作为客户端。
- 默认地址：127.0.0.1:19090。
- 失败策略：EX 连接失败后按固定间隔重试；AT 断开后继续监听新连接。

## 2. 通用包头

所有消息采用统一包头，后接变长 payload。

```text
magic[2]   = 0x53 0x4D            // 'S' 'M'
version[1] = 0x01
msg_type[1]
seq[4]     = little-endian uint32
ts_ns[8]   = little-endian uint64 // 发送端单调时钟纳秒
size[4]    = little-endian uint32 // payload 长度
crc32[4]   = little-endian uint32 // 仅校验 payload
```

- 包头总长度：24 字节。
- `seq` 按发送顺序递增，回绕后从 0 继续。
- `ts_ns` 在发送端写入，用于端到端延迟估计。

## 3. 消息类型

- 0x01: JPEG 图像帧。
- 0x02: 位姿快照。
- 0x03: 心跳（无 payload）。

第一迭代只要求 0x01 和 0x02 必须可用。

## 4. 0x01 图像帧 payload

```text
width[2]     = uint16
height[2]    = uint16
channels[1]  = uint8  // 固定 3
encoding[1]  = uint8  // 1=jpeg
quality[1]   = uint8  // 1..100
reserved[1]  = uint8
jpeg_bytes[n]
```

- 解码目标：BGR, CV_8UC3（EX 侧统一为 OpenCV 输入格式）。
- 建议发送频率：30 或 60 FPS。

## 5. 0x02 位姿快照 payload

单位与坐标系必须明确，避免闭环阶段出现方向漂移。

```text
frame_name_len[1]
frame_name[k]                 // 建议固定 "gimbal"
px[4], py[4], pz[4]           // float32, 单位 m
qx[4], qy[4], qz[4], qw[4]    // float32, 归一化四元数
vx[4], vy[4], vz[4]           // float32, 单位 m/s, 可选先填 0
wx[4], wy[4], wz[4]           // float32, 单位 rad/s, 可选先填 0
```

- `p*` 与 `q*` 是第一迭代最小必填项。
- 若速度项暂不可得，可发送 0 但保留字段长度，保持协议稳定。

## 6. 时间与延迟统计

EX 侧建议同时记录两种时间：

- `send_ts_ns`：来自包头 `ts_ns`。
- `recv_ts_steady`：本地 `steady_clock::now()`。

延迟统计采用：

```text
delay_ms = (recv_ts_ns - send_ts_ns) / 1e6
```

若跨机运行导致时钟不同步，仍可用 `seq` 计算抖动与丢包。

## 7. 错误处理

- 包头 magic/version 不匹配：丢弃并重新同步。
- `size` 超过上限（建议 8MB）：丢弃并记录错误。
- CRC32 不通过：丢弃并累计计数。
- JPEG 解码失败：当帧失败，不中断连接。

## 8. 第一迭代冻结项

以下内容在第一迭代中不改动：

- 包头格式与字段顺序。
- `msg_type` 编号。
- 图像编码默认值（JPEG）。
- 位姿单位（m, rad）。

后续闭环阶段新增控制流时，只追加新 `msg_type`，不破坏既有消息。
