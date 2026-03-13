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

### 需要确认的信息（与下位机队友对齐）

| 字段 | 当前状态 | 需要确认 |
|------|---------|---------|
| 帧格式（帧头、长度、校验） | 占位 `0xAA + 5字节` | 正式协议字节序 |
| IMU 数据类型 | 未接入 | 四元数 `(w,x,y,z)` 还是欧拉角 `(yaw,pitch,roll)`？精度？ |
| IMU 坐标系约定 | 未知 | 世界系定义（北/东/天 or 自定义）；与 EKF 约定对齐 |
| 下行帧格式（yaw/pitch 指令） | 临时 8字节 XOR | 正式协议 |
| 发送频率 | 未定 | IMU 上报频率（建议 ≥100Hz）|

### 开发任务

- [ ] 与下位机队友确认完整协议文档
- [ ] 实现 `SerialNode::TryRecv()` 正式解析，替换占位代码
  - 文件：`src/pipeline/serial_node.cpp`（TODO Stage 8-F）
- [ ] 实现 `RmShooter::Send()` 正式帧格式
  - 文件：`src/modules/rm_shooter/rm_shooter.cpp`
- [ ] 在 `predict_voter_test` 中接入串口替代 `SimGimbalQuaternion`，验证 IMU 四元数提供正确
- [ ] 单元测试：mock 串口帧 → 解析结果 → 四元数正确性断言

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
