# 调试记录（predict_voter_test）

记录 `mv-predict-voter-test` 端到端调试过程中发现的 Bug、修复方案及残余问题。
测试视频：`video/debug/17.mp4`（2700×2160，256 帧，30fps，红方装甲板正面靠近）

---

## Session 1 — 2026-03-12  tracker 永远 LOST / PnP 结果全为 0

### 现象
- Foxglove 3D 面板中从不出现 `tracking/*` 话题的点
- `pnp/axes_3d` 的坐标始终在原点
- 日志中 `EkfTracker` 状态机停留在 `LOST`，从不转入 `DETECTING`

### 根因 1：检测颜色写死为 UNKNOWN

**文件**：`src/modules/armor_detector/basic_armor_detector.cpp`

`MakeDetection()` 内部将 `det.color` 设为 `ArmorColor::UNKNOWN`，而 `EkfTracker::Track()` 对每个检测结果执行：

```cpp
if (d.color != enemy_color) continue;   // UNKNOWN != RED/BLUE → 全部跳过
```

导致 `valid_dets` 始终为空，状态机永远在 LOST。

**修复**：在 `Detect()` 的检测循环内添加 `det.color = enemy_color`。

### 根因 2：IMG_CTR 硬编码为 1280×1024 中心

**文件**：`src/modules/armor_detector/basic_armor_detector.cpp`

`distance_to_center` 计算时使用 `IMG_CTR{640.0F, 512.0F}`，而视频分辨率为 2700×2160。

**修复**：改为 `{frame.cols * 0.5F, frame.rows * 0.5F}` 动态计算。

### 验证结果
修复后日志出现 `DETECTING → TRACKING (count=5)`，PnP 结算有效输出。

---

## Session 2 — 2026-03-12  3D 面板中 pnp/axes_3d 与 tracking/* 不重合（90° 偏转）

### 现象
- Foxglove 3D 面板中 `pnp/axes_3d`（gimbal 系）坐标轴与 `tracking/armor_positions`（world 系）球体在不同位置
- 机器人直行靠近时，预测装甲板位置随时间漂移旋转

### 根因：WorldToGimbalTF 含 R_FIX(-90°Rx) 且使用预测输出角

**文件**：`src/test/predict_voter_test.cpp`（原 `/tf` 发布逻辑）

原代码调用 `WorldToGimbalTF(ctrl.yaw, ctrl.pitch)` 发布 `world → gimbal` TF：
1. `ctrl.yaw/pitch` 是预测的**瞄准角**，不是当前云台姿态
2. `WorldToGimbalTF` 内部有 `R_FIX = Rx(-90°)`，而 EKF 的 `R_gimbal2world` 用 `SimGimbalQuaternion`（无此偏转），两者约定不一致

**修复**：直接用 `SimGimbalQuaternion` 的旋转矩阵构建 TF，去掉 `R_FIX`：

```cpp
Eigen::Matrix4d T_w_g = Eigen::Matrix4d::Identity();
T_w_g.block<3, 3>(0, 0) =
    mv::tool::SimGimbalQuaternion(ps_snap.sim_yaw_deg, ps_snap.sim_pitch_deg,
                                   ps_snap.sim_roll_deg)
        .toRotationMatrix();
sink.PublishTransform("world", "gimbal", T_w_g, ts_ns);
```

### 验证结果
修复后 3D 面板中 `pnp/axes_3d` 与 `tracking/rotation_center` 坐标重合。

---

## Session 3 — 2026-03-12  预测重投影（P0-P3 彩色叉）与实际装甲板偏差大

### 现象
- `camera/annotated` 图像上 P0 等彩色重投影标记与实际装甲板像素位置偏差较大
- 机器人未旋转，但重投影看起来有仰视/旋转感

### 尝试 A：修改 K 矩阵分辨率（已撤销）

**思路**：`vision.yaml` 中的 K 矩阵来自 1280×1024 的标定，而视频帧为 2700×2160。

代码中缩放逻辑为：
```cpp
const float sx = float(kDisplayW) / float(frame.cols);  // = 480/2700 = 0.178
repro_K_disp.at<double>(0,0) *= sx;   // fx: 1764 × 0.178 = 314  ← 错误
```

正确应当是 `480/1280 = 0.375 → fx = 661`，即 K 应填写与 `frame.cols` 相同分辨率下的值。

将 K 缩放到 2700×2160（×2.109375）后单独验证，但彼时 Session 2 的 TF 问题尚未修复，视觉效果混乱，误判为无效，遂撤销。

**当前状态**：vision.yaml 中 K 已按 2700×2160 缩放（上次会话末尾修改保留），但其余代码已撤销。

### 尝试 B：修改 R_c2g（已撤销）

**思路**：旧 `R_c2g = [[1,0,0],[0,-1,0],[0,0,1]]` 行列式为 -1（反射变换），EKF 模型期望 `xyz[1]=前/深度、xyz[2]=上/高度`，但实际 `xyz[1]=上（相机-Y翻转）、xyz[2]=前`，轴序错误。

将 `R_c2g` 改为 `[[1,0,0],[0,0,1],[0,-1,0]]`（det=+1）可以对齐轴序，同时需要同步修改：
- `pnp_solver.cpp`：`yaw = atan2(x, y)`，`pitch = atan2(z, y)`
- `predict_annotator.hpp`：`T_h = (cos,-sin,0)`，`T_v = (0,0,1)`

**用户撤销原因**：效果仍有偏差，尚不确定；撤销后重新讨论。

### 残余问题（待解决）

> **核心缺失：相机–云台手眼标定 + 真实 IMU 数据**

目前 `sim_yaw/pitch/roll` 全为 0，`R_gimbal2world = I`，相当于假设"相机始终水平正视"。现实场景中：
1. 相机安装在云台上，姿态随云台变化（Pitch 约有 5°~15° 向下倾斜）
2. 无真实 IMU 时，无法将多帧 PnP 结果变换到同一世界系下

这使得：
- EKF 收到的每帧坐标都在相机/云台系，不是惯性世界系
- 圆周运动半径估计受云台倾角污染
- 预测重投影时 `armor_positions` 到相机的变换缺少真实旋转

**结论**：视频调试的重投影精度受限于空缺的 IMU 数据，后续应先完成标定和串口通信后再验证。

---

## 已修复 Bug 汇总

| # | 文件 | 描述 | 状态 |
|---|------|------|------|
| 1 | `basic_armor_detector.cpp` | `det.color = UNKNOWN` → `det.color = enemy_color` | ✅ 已修复 |
| 2 | `basic_armor_detector.cpp` | `IMG_CTR` 硬编码 640/512 → 动态 `cols/2, rows/2` | ✅ 已修复 |
| 3 | `predict_voter_test.cpp` | TF 使用预测角 + R_FIX 导致 pnp/tracking 3D 不重合 | ✅ 已修复 |
| 4 | `vision.yaml` | K 矩阵按 1280×1024 标定但视频为 2700×2160 | ✅ 已按2.1×缩放 |

## 待确认问题

| # | 问题 | 依赖条件 |
|---|------|---------|
| A | `R_c2g` 行列式=-1（反射）导致轴序与 EKF 约定不匹配 | 需配合真实 IMU 验证 |
| B | 无 IMU 时重投影精度无法验证 | 需完成串口通信 |
| C | 相机–云台外参 `t_camera_to_gimbal`/`R_camera_to_gimbal` 未标定 | 需执行手眼标定 |
