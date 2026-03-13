# 开发路线图（2026赛季）

> 最后更新：2026-03-12  
> 当前状态：Stage 8-E 完成，Stage 8-F 阻塞（等待下位机串口协议）

---

## 总览

```
Phase 1 ─ 串口协议 + IMU 接入      ⬜ 待开始（当前阻塞点）
Phase 2 ─ 手眼标定                 ⬜ 待开始（依赖 Phase 1 硬件在手）
Phase 3 ─ 端到端调试验证            ⬜ 待开始（依赖 Phase 1 + 2）
Phase 4 ─ 旧代码清理 + Pipeline 集成⬜ 待开始（可与 Phase 2/3 并行）
Phase 5 ─ 比赛调参 + 压测           ⬜ 待开始（依赖 Phase 3 完成）
```

---

## Phase 1 — 串口协议 + IMU 接入

**目标**：`SerialNode` 能正确解析下位机上行数据，填充 `SharedState::gimbal_quat`，`EkfPredictor` 获取真实云台姿态。

### 协议规范（v1.0，已确认）

协议定义位于 `src/hal/serial/rm_protocol.hpp`。

| 参数 | 约定 |
|------|------|
| 物理层 | UART 921600 bps，8N1 |
| 字节序 | Little-Endian |
| 帧完整性 | CRC16-CCITT（poly=0x1021, init=0xFFFF） |
| 角度/四元数精度 | × 10000 整数，精度约 0.1 mrad |
| 四元数坐标系 | 与 sp_vision_25/io/cboard 一致：q 表示 IMU 世界系 → 云台本体系旋转 |
| 开火触发 | 脉冲触发（fire=1 持续 1 帧，其余帧发 0）|

**下行帧（上位机 → MCU）**：15 字节，帧头 `0xAA 0x0F`，帧尾 `0x0D`  
字段：`seq` / `detected` / `shoot` / `yaw×1e4` / `pitch×1e4` / `distance×100` / `crc16`

**上行帧（MCU → 上位机）**：28 字节，帧头 `0xAA 0xFF`，帧尾 `0x0D`  
字段：`seq` / `color` / `mode` / `robot_id` / `bullet_speed×100` / `q_wxyz×1e4` / `yaw×1e4` / `pitch×1e4` / `yaw_vel×1e4` / `pitch_vel×1e4` / `crc16`

### 需要确认的信息（与下位机队友对齐）

| 字段 | 当前状态 | 需要确认 |
|------|---------|---------|
| 帧格式 | ✅ 已按 v1.0 实现 | 下位机按此规范实现即可 |
| IMU 数据类型 | ✅ 四元数 (w,x,y,z) × 10000 LE | 下位机确认 IMU 输出精度是否满足 |
| IMU 坐标系约定 | ✅ 与 sp_vision_25 一致 | 下位机确认 q 坐标系方向与约定匹配 |
| 下行帧格式 | ✅ 已按 v1.0 实现 | 下位机按此规范实现即可 |
| 上报频率 | ✅ 要求 ≥ 200 Hz | 下位机确认硬件能否达到 |

### 开发任务

- [x] 协议规范定义 `src/hal/serial/rm_protocol.hpp`（CRC16、帧常量、UpFrame/DownFrame 结构体）
- [x] `RmShooter::Send()` 按 v1.0 格式组帧发送，角度 × 10000，CRC16，序列号
- [x] `SerialNode::TryRecv()` 实现搜帧头 + 定长读 + CRC16 校验 + 四元数注入 `state_.SetGimbalQuat()`
- [ ] 与下位机队友对齐，下位机按 `rm_protocol.hpp` 实现收发
- [ ] 在 `predict_voter_test` 中接入真实串口替代 `SimGimbalQuaternion`，验证 IMU 四元数正确
- [ ] 单元测试：构造合法/非法上行帧字节流 → `TryRecv()` 解析结果断言

### 完成标志

- Foxglove 3D 面板 `world` 与 `gimbal` 帧随云台转动而分离（证明 IMU 数据在生效）
- `tracking/rotation_center` 在云台转动时保持相对世界系静止

---

## Phase 2 — 手眼标定

**目标**：获取准确的 `R_camera_to_gimbal`（3×3）和 `t_camera_to_gimbal`（3×1），填入 `configs/vision.yaml`。

### 背景

当前 `R_c2g = [[1,0,0],[0,-1,0],[0,0,1]]`（行列式=-1，为反射变换，非真实物理旋转），`t_c2g = [0,0,0]`，均为近似值。这导致：

- EKF 输入的 `xyz_in_gimbal` 轴序疑似与模型约定不符（待真实 IMU 后验证）
- 相机安装倾角（Pitch ~5°~15°向下）未被补偿，EKF 圆周运动平面受污染

### 标定方法（推荐）

使用已有的 `sp_vision_25/calibration/calibrate_handeye.cpp`，或重新编写：

1. **棋盘格标定**：先用 `calibrate_camera.cpp` 重新确认内参 K（在实际相机分辨率下）
2. **手眼标定**：将棋盘格放在不同云台角度各拍一组，用 OpenCV `calibrateHandEye()` 求解 `R_c2g`、`t_c2g`

### 需要的硬件

- [ ] 相机 + 云台整机组装完成，可供标定
- [ ] 标定板（棋盘格，建议 7×9 @ 25mm）
- [ ] 真实 IMU 数据（Phase 1 完成后）

### 开发任务

- [ ] 确认实际相机输出分辨率（目前调试视频 2700×2160，实机分辨率待确认）
- [ ] 在实际分辨率下重新标定内参，更新 `vision.yaml` 中 `camera_matrix` 和 `distort_coeffs`
- [ ] 执行手眼标定，将结果写入 `vision.yaml`：
  ```yaml
  calibration:
    R_camera_to_gimbal: [r00, r01, ..., r22]  # 行优先 3×3，det=+1
    t_camera_to_gimbal: [tx, ty, tz]           # 单位 m
  ```
- [ ] 同步验证 EKF 的 `R_c2g` 轴序问题（见 `docs/DEBUG_SESSIONS.md` 待确认问题 A/B）

### 完成标志

- `det(R_camera_to_gimbal) == +1.0`（真旋转矩阵）
- PnP 残差 `reproj_error < 1.5 px`（Foxglove `pnp/residuals` 话题）

---

## Phase 3 — 端到端调试验证

**前置条件**：Phase 1 + Phase 2 均已完成

**目标**：在真实硬件上验证完整链路，预测重投影与实际装甲板像素对齐，开火角度误差 < 0.5°。

### 开发任务

- [ ] 用真实相机 + 真实 IMU 重跑 `predict_voter_test`（或 `mv-vision-main`）
- [ ] 在 Foxglove 确认以下全部通过：
  - `pnp/axes_3d` 与 `tracking/rotation_center` 在 3D 面板中重合
  - `camera/annotated` 中 P0-P3 彩色叉与实际装甲板像素对齐（误差 < 10px）
  - 装甲板旋转时 EKF 圆周半径稳定（`tracking/target_state` 中 `r` 字段收敛）
- [ ] 验证 `R_c2g` 轴序：在 `xyz_in_gimbal` 正确后，确认 `yaw/pitch` 计算公式
- [ ] 调整 EKF 协方差参数（`process_noise_pos/ang`）使跟踪稳定不丢失
- [ ] 验证 `CooldownVoter` 开火逻辑，`voter/decision` 话题在装甲板对准时正确输出 `fire=true`

### 完成标志

- 装甲板旋转一周，EKF 全程跟踪不丢失（`TRACKING` 状态保持）
- 预测重投影与实际装甲板视觉上可见对齐
- 开火指令在对准时正确触发，无误触发

---

## Phase 4 — 旧代码清理 + Pipeline 正式集成

**可与 Phase 2/3 并行进行**

### 开发任务

- [ ] 清理旧代码目录 `base/`、`devices/`、`module/`（确认 `mv-vision-main` 稳定后删除）
- [ ] 将 `FoxgloveSink` 接入 `VisionPipeline`（见 PROGRESS.md Stage 7 待完成项）
  - `DetectNode::Run()` 调 `PublishDetections` + `PublishPnpResult`
  - `VisionPipeline` 定时汇聚调 `PublishThreadMetrics`
- [ ] `ENERGY_BUFF` 状态接入下位机 `mode` 字段（`vision_fsm.cpp`）
- [ ] 补充单元测试：EkfTrackTarget / TrajectorySolver / CooldownVoter

---

## Phase 5 — 比赛调参 + 压测

**前置条件**：Phase 3 完成

### 开发任务

- [ ] 在标准赛场环境（7m 识别距离）下录制调参视频
- [ ] 按 `docs/DEBUG_SESSIONS.md` 的参数配置表系统调参
- [ ] 压测：8小时连续运行不崩溃、不内存泄漏（`valgrind --leak-check=full`）
- [ ] 验证哨兵/步兵/英雄不同机型装甲板数量切换正确（`armor_num = 2/4`）
- [ ] 确认 `MpcVoter` 是否替换 `CooldownVoter`（需 TinyMPC 调参）

---

## 关键依赖关系

```
下位机协议文档
    └──► Phase 1（串口 + IMU）
              └──► Phase 3（端到端验证）
                        └──► Phase 5（调参压测）

硬件整机在手
    └──► Phase 2（手眼标定）
              └──► Phase 3（端到端验证）

Phase 3 完成
    └──► Phase 4 可以安全删除旧代码
```

---

## 缺失数据/待获取清单

| 数据 | 用途 | 获取方式 | 责任人 |
|------|------|---------|--------|
| 下位机串口协议文档 | Stage 8-F、Phase 1 | 与下位机队友对齐 | - |
| IMU 坐标系约定 | EKF 世界系对齐 | 与下位机队友确认 | - |
| 实机相机内参 K（实际工作分辨率） | PnP、重投影 | 棋盘格标定 | - |
| `R_camera_to_gimbal`（手眼标定） | PnP→云台坐标变换 | 手眼标定 | - |
| `t_camera_to_gimbal`（相机安装偏移） | PnP→云台坐标变换 | 机械测量 / 标定 | - |
| 装甲板实物尺寸确认 | PnP 世界点、重投影框 | 测量或官方规格 | - |
