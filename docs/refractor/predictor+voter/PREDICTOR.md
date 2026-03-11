# 预测模块（PREDICTOR）

> **总览：** [PREDICTION_DECISION_OVERVIEW.md](PREDICTION_DECISION_OVERVIEW.md)
> **决策模块：** [VOTER.md](VOTER.md)

---

## 目录

1. [接口层 IPredictor](#1-接口层-ipredictor)
2. [SimplePredictor](#2-simplepredictor)
3. [EkfPredictor 架构](#3-ekfpredictor-架构)
4. [EKF 数学推导](#4-ekf-数学推导)
5. [多板装甲车建模](#5-多板装甲车建模)
6. [坐标系变换](#6-坐标系变换)
7. [弹道求解（TrajectorySolver）](#7-弹道求解-trajectorysolver)
8. [收敛性检验](#8-收敛性检验)
9. [YAML 参数全表](#9-yaml-参数全表)

---

## 1. 接口层 IPredictor

**文件：** `src/interfaces/i_predictor.hpp`

### 1.1 职责

`IPredictor` 负责"跨帧跟踪目标并输出预判的云台角度"：

- **输入：** 本帧 `Detection[]`（含 PnP 解算后的 3D 坐标）+ 当前时间戳 + 敌方颜色
- **输出：**
  - `GimbalControl`：`tracking` 为 `true` 时包含有效的 `yaw` / `pitch`（弹道补偿后）
  - `TrackTarget`：跟踪状态快照（调试 / Voter 用）

### 1.2 核心方法

| 方法 | 说明 |
|---|---|
| `Init(config)` | 从 YAML 加载参数，实例化内部算法组件 |
| `Predict(detections, timestamp, color)` | 主调用接口，更新跟踪状态并返回云台指令 |
| `SetGimbalOrientation(q)` | 注入当前云台四元数姿态（EkfPredictor 专属，SimplePredictor 为空操作） |
| `GetTrackTarget()` | 获取上一帧的跟踪状态快照（供 Voter 和调试使用） |
| `Reset()` | 清空跟踪状态，目标丢失后重新开始 |

### 1.3 时间戳设计

使用 `std::chrono::steady_clock`（单调时钟）而非系统时钟：

- 不受 NTP 时间跳变影响，`dt` 计算稳定
- `Pipeline` 在 `Grab()` 后立即打时间戳，帧时间随 `Detection` 传入 `Predict()`

### 1.4 线程安全约定

预测器**非线程安全**，在专属的预测线程（`PredictNode`）顺序调用，无需加锁。

---

## 2. SimplePredictor

**文件：** `src/modules/simple_predictor/`  
**工厂键：** `"simple"`

### 2.1 设计目标

集成验证用版本，不含卡尔曼滤波：

- 按到图像中心距离选出"最优目标"
- 维护简化跟踪状态机
- 直接将目标的 `yaw` / `pitch` 作为 `GimbalControl` 输出（无延迟补偿）

### 2.2 状态机

```
             检测到目标
   LOST ──────────────────► DETECTING
    ▲                            │
    │  目标消失                  │  连续检测 ≥ min_detect_count 帧
    │                            ▼
    │                        TRACKING ─────────────────► TEMP_LOST
    │                            ▲   目标短暂丢失              │
    │                            │                             │
    │                            └───── 重新检测到 ────────────┘
    │
    └────────────────── TEMP_LOST 超 max_temp_lost_count 帧 ────
```

| 状态 | 含义 |
|---|---|
| `LOST` | 无目标，等待首次检测 |
| `DETECTING` | 检测到候选目标，累积帧计数中 |
| `TRACKING` | 稳定跟踪，输出有效 `GimbalControl` |
| `TEMP_LOST` | 短暂丢帧，保持上次输出，等待重新检测 |

### 2.3 YAML 参数

```yaml
auto_aim:
  tracker:
    min_detect_count:     5   # 进入 TRACKING 所需连续帧数
    max_temp_lost_count: 15   # TEMP_LOST 超此帧数则退回 LOST
```

---

## 3. EkfPredictor 架构

**文件：** `src/modules/ekf_predictor/`  
**工厂键：** `"ekf"`

### 3.1 整体架构（Pimpl 模式）

```
EkfPredictor（IPredictor 实现，Pimpl 公开接口）
    │
    └── Impl（私有数据，不暴露到头文件）
          ├── EkfTracker          跟踪状态机 + EKF 实例
          │     └── EkfTrackTarget  单目标 EKF（11 维状态向量）
          ├── TrajectorySolver    飞行时间迭代弹道求解
          ├── R_gimbal2world      坐标系旋转矩阵（每帧更新）
          └── bullet_speed        当前弹速（m/s）
```

**Pimpl 的好处：**
1. 上层（`PredictNode`、`main.cpp`）不依赖 Eigen、EKF 内部类型
2. 修改 EKF 参数结构、状态向量维度时，只需重编 `ekf_predictor.cpp`，不触发上层重编译

### 3.2 每帧 Predict() 流程

```
1. SetGimbalOrientation(q) 已在本帧 Predict() 前注入四元数
         │
         ▼
2. 将 Detection.xyz_in_gimbal 转换为世界系
   xyz_world = R_gimbal2world · xyz_gimbal
   yaw_world = yaw_gimbal + atan2(R[1,0], R[0,0])
         │
         ▼
3. EkfTracker::Track(world_dets, timestamp, enemy_color)
   → 状态机驱动 EkfTrackTarget 的 Predict() / Update()
   → 返回 optional<EkfTrackTarget>
         │
    (无目标) → GimbalControl{tracking=false}
         │
    (有目标)
         ▼
4. TrajectorySolver::Solve(target, timestamp, bullet_speed)
   → 飞行时间迭代 → yaw, pitch
         │
         ▼
5. 填充 last_track_target（供 GetTrackTarget() 使用）
6. 返回 GimbalControl{yaw, pitch, tracking=true}
```

---

## 4. EKF 数学推导

**文件：** `src/modules/ekf_predictor/detail/ekf_track_target.hpp/.cpp`

### 4.1 状态向量（11 维，世界坐标系）

$$
\mathbf{x} = \begin{bmatrix}
c_x & \dot{c}_x & c_y & \dot{c}_y & c_z & \dot{c}_z & \alpha & \dot{\alpha} & r & \Delta l & \Delta h
\end{bmatrix}^\top
$$

| 序号 | 符号 | 含义 | 单位 |
|---|---|---|---|
| 0 | $c_x$ | 旋转中心 x 坐标（世界系） | m |
| 1 | $\dot{c}_x$ | 旋转中心 x 方向速度 | m/s |
| 2 | $c_y$ | 旋转中心 y 坐标 | m |
| 3 | $\dot{c}_y$ | 旋转中心 y 方向速度 | m/s |
| 4 | $c_z$ | 旋转中心 z 坐标 | m |
| 5 | $\dot{c}_z$ | 旋转中心 z 方向速度 | m/s |
| 6 | $\alpha$ | 当前朝向装甲板旋转角（相位）| rad |
| 7 | $\dot{\alpha}$ | 旋转角速度（正值=逆时针）| rad/s |
| 8 | $r$ | 主旋转半径 | m |
| 9 | $\Delta l$ | 大小板半径差 $r_{\text{side}} - r$ | m |
| 10 | $\Delta h$ | 大小板高度差 $z_{\text{side}} - z$ | m |

> $\Delta l$、$\Delta h$ 仅对 4 板车（步兵/英雄/哨兵）有意义，前哨站和平衡步兵忽略此二项。

### 4.2 过程模型（匀速 CV 模型）

状态转移方程（非线性，用于 EKF 预测步）：

$$
f(\mathbf{x}, \Delta t) = \begin{bmatrix}
c_x + \dot{c}_x \cdot \Delta t \\
\dot{c}_x \\
c_y + \dot{c}_y \cdot \Delta t \\
\dot{c}_y \\
c_z + \dot{c}_z \cdot \Delta t \\
\dot{c}_z \\
\alpha + \dot{\alpha} \cdot \Delta t \\
\dot{\alpha} \\
r \\
\Delta l \\
\Delta h
\end{bmatrix}
$$

由于 $f$ 对 $\mathbf{x}$ 是线性的，状态转移雅可比矩阵 $F = \partial f / \partial \mathbf{x}$ 即为：

$$
F = \mathrm{blockdiag}\left(
\begin{bmatrix}1 & \Delta t \\ 0 & 1\end{bmatrix},
\begin{bmatrix}1 & \Delta t \\ 0 & 1\end{bmatrix},
\begin{bmatrix}1 & \Delta t \\ 0 & 1\end{bmatrix},
\begin{bmatrix}1 & \Delta t \\ 0 & 1\end{bmatrix},
1, 1, 1
\right)_{11\times11}
$$

### 4.3 过程噪声矩阵 Q

采用**分段白噪声（Piecewise White Noise）模型**：

对于每个"位置-速度"对 $(s, \dot{s})$，白噪声加速度 $a \sim \mathcal{N}(0, \sigma^2)$ 产生：

$$
Q_{ss} = \begin{bmatrix} \Delta t^4/4 & \Delta t^3/2 \\ \Delta t^3/2 & \Delta t^2 \end{bmatrix} \sigma^2
$$

| 状态分量 | 噪声方差 $\sigma^2$ | YAML 参数 |
|---|---|---|
| $(c_x, \dot{c}_x), (c_y, \dot{c}_y), (c_z, \dot{c}_z)$ | `process_noise_pos` | 普通车位置噪声 |
| $(\alpha, \dot{\alpha})$ | `process_noise_ang` | 普通车旋转噪声 |
| 前哨站位置 | `process_noise_outpost_pos` | 前哨站位置噪声（更小，匀速假设更准） |
| 前哨站旋转 | `process_noise_outpost_ang` | 前哨站旋转噪声（恒速旋转，极小） |
| $r, \Delta l, \Delta h$ | 0（视为常量）| — |

### 4.4 观测模型

每次检测到一块装甲板，观测量为其**3D 坐标**（世界系）：

$$
\mathbf{z} = \begin{bmatrix} x_{\text{armor}} \\ y_{\text{armor}} \\ z_{\text{armor}} \end{bmatrix}
$$

对于第 $k$ 块装甲板（4 板车，$k = 0,1,2,3$）：

$$
h_k(\mathbf{x}) = \begin{bmatrix}
c_x + r_k \cos(\alpha + k\pi/2) \\
c_y + r_k \sin(\alpha + k\pi/2) \\
c_z + h_k
\end{bmatrix}
$$

其中：
- $r_k = r$（$k$ 为偶数，主板）或 $r + \Delta l$（$k$ 为奇数，侧板）
- $h_k = 0$（$k$ 为偶数）或 $\Delta h$（$k$ 为奇数）

### 4.5 观测雅可比 H（解析求导）

$H = \partial h_k / \partial \mathbf{x}$ 的非零元素（以 4 板车主板 $k=0$ 为例）：

| 行 | 非零列 | 值 |
|---|---|---|
| 0（$x$ 方向） | $c_x=0$ | $1$ |
| 0 | $\alpha=6$ | $-r\sin\alpha$ |
| 0 | $r=8$ | $\cos\alpha$ |
| 1（$y$ 方向） | $c_y=2$ | $1$ |
| 1 | $\alpha=6$ | $r\cos\alpha$ |
| 1 | $r=8$ | $\sin\alpha$ |
| 2（$z$ 方向） | $c_z=4$ | $1$ |

> 使用解析求导（非数值差分），精度更高，速度更快。

### 4.6 EKF 更新步骤

标准扩展卡尔曼滤波迭代：

$$
\begin{aligned}
\text{预测步：}\quad & \hat{\mathbf{x}}^- = f(\hat{\mathbf{x}}, \Delta t) \\
& P^- = F P F^\top + Q \\
\\
\text{更新步：}\quad & K = P^- H^\top (H P^- H^\top + R)^{-1} \\
& \hat{\mathbf{x}} = \hat{\mathbf{x}}^- + K(\mathbf{z} - h(\hat{\mathbf{x}}^-)) \\
& P = (I - KH) P^-
\end{aligned}
$$

**观测噪声矩阵** $R = \mathrm{diag}(\sigma_x^2, \sigma_y^2, \sigma_z^2)$，从 PnP 解算精度经验设定。

---

## 5. 多板装甲车建模

**文件：** `src/modules/ekf_predictor/detail/ekf_track_target.hpp`

### 5.1 不同装甲板数的角度分布

#### 4 板车（步兵 / 英雄 / 哨兵）

```
         board 0（主，r）
              ↑ α
              │
board 3 ←──[车体]──→ board 1（侧，r+Δl）
    (α+3π/2)      (α+π/2)
              │
              ↓ α+π
         board 2（后，r）
```

大小装甲板交替排列：主板（0, 2）半径为 $r$，侧板（1, 3）半径为 $r + \Delta l$，高度偏移 $\Delta h$。

#### 3 板（前哨站）

3 块板均匀分布，相位差 $2\pi/3$，半径均为 $r$，无大小板区别。

#### 2 板（平衡步兵）

2 块板对向分布，相位差 $\pi$，半径均为 $r$。

### 5.2 装甲板跳变检测

当新观测的装甲板 ID（由 `SelectNearestArmor()` 选出）与上一帧不同时，标记 `jumped = true`。  
`EkfTracker` 收到跳变信号后，将 $\alpha$ 更新为跳变后的相位角，并重新初始化相关协方差，避免 EKF 跟踪错误的装甲板。

### 5.3 前哨站特殊处理

前哨站旋转速度较慢（约 0.8 rad/s），丢帧宽容度更高：

- `outpost_max_temp_lost_count`（默认 30 帧）> 普通车的 `max_temp_lost_count`（默认 15 帧）
- 过程噪声更小（`process_noise_outpost_pos/ang`），匀速假设更准确

---

## 6. 坐标系变换

**相关文件：** `docs/refractor/tf/COORDINATE_SYSTEM.md`（详细坐标系定义）

### 6.1 动机

EKF 在**世界系**（IMU 绝对系）建模，原因：

- 云台运动时，旋转中心在云台系中会随云台旋转移动，导致 EKF 匀速假设失效
- 世界系中旋转中心位置仅受目标运动影响，云台运动被 IMU 补偿消除

### 6.2 变换链

```
PnP 解算结果（云台系）
        │
        │  R_gimbal2world = q.normalized().toRotationMatrix()
        │                   q 由串口读取的 IMU 四元数注入
        ▼
目标坐标（世界系）
   xyz_world = R_gimbal2world · xyz_gimbal
   yaw_world = yaw_gimbal + atan2(R[1,0], R[0,0])
        │
        ▼
EkfTrackTarget::Update(detection)
```

### 6.3 SetGimbalOrientation 调用时序

```
PredictNode::Process():
    1. 从 SharedState 读取最新 IMU 四元数 q
    2. predictor_->SetGimbalOrientation(q)   ← 更新 R_gimbal2world
    3. predictor_->Predict(detections, ts, color)  ← 内部使用已更新的矩阵
```

`SetGimbalOrientation()` 在 `IPredictor` 中提供默认空操作（`SimplePredictor` 无需覆写）；`EkfPredictor` 覆写后缓存旋转矩阵。

---

## 7. 弹道求解（TrajectorySolver）

**文件：** `src/modules/ekf_predictor/detail/trajectory_solver.hpp/.cpp`

### 7.1 弹道模型

假设子弹在竖直平面做抛物线运动（不考虑空气阻力，仅重力修正）：

$$
t_{\text{fly}} \approx \frac{d}{v_0 \cos\theta}, \quad
\theta_{\text{pitch}} = \arctan\!\left(\frac{v_0 \sin\theta}{v_0 \cos\theta}\right) \approx \arctan\!\left(\frac{g \cdot d}{v_0^2}\right) + \arctan\!\left(\frac{\Delta z}{d}\right)
$$

其中 $d$ 为水平距离，$v_0$ 为初速度，$\Delta z$ 为高度差。

### 7.2 延迟补偿

总预测时间 = **系统延迟** + **飞行时间**：

$$
t_{\text{total}} = t_{\text{delay}} + t_{\text{fly}}
$$

系统延迟根据弹速分段：

| 条件 | 延迟 | YAML 参数 |
|---|---|---|
| $v_0 < v_{\text{decision}}$ | `low_speed_delay_ms` | 枪管响应慢，延迟更长 |
| $v_0 \geq v_{\text{decision}}$ | `high_speed_delay_ms` | 弹速快，飞行时间短 |

### 7.3 迭代飞行时间算法

飞行时间 $t_{\text{fly}}$ 依赖目标距离，目标距离又依赖预测时刻（$t_{\text{fly}}$ 涉及的目标位置）：

```
t_future = t_now + t_delay + t_fly_init
重复最多 max_iter 次：
  target_copy.Predict(t_future)            // 外推目标到 t_future
  aim = SelectNearestArmor(target_copy)    // 选最优装甲板
  t_fly_new = d(aim) / (v0 · cosθ)        // 重新计算飞行时间
  if |t_fly_new - t_fly_prev| < thresh:   // 收敛
      break
  t_future += (t_fly_new - t_fly_prev)    // 修正预测时刻
  t_fly_prev = t_fly_new
```

### 7.4 最优瞄准点选择

从 `ArmorXyzaList()` 的所有装甲板中选择：
1. **正对云台**：装甲板 yaw 与云台 yaw 的差角落在 `(-max_approaching_angle, max_leaving_angle)` 之间
2. **距离最近**：满足条件中选 3D 距离最小的

若无满足条件的装甲板，输出 `GimbalControl{tracking=false}`。

---

## 8. 收敛性检验

**文件：** `src/modules/ekf_predictor/detail/ekf_track_target.hpp`

### 8.1 协方差发散检测

$$
\text{diverged} = \mathrm{tr}(P) > \text{divergence\_threshold}
$$

协方差矩阵的迹是不确定度的总和。当 `tr(P)` 超过阈值（默认 $10^6$），说明 EKF 数值不稳定。`EkfTracker` 在每帧 `Update()` 后调用此方法，发散则立即重置为 `LOST`。

### 8.2 NIS 收敛检验

**NIS（Normalized Innovation Squared）**：

$$
\varepsilon = \tilde{\mathbf{z}}^\top S^{-1} \tilde{\mathbf{z}}
$$

其中 $\tilde{\mathbf{z}} = \mathbf{z} - h(\hat{\mathbf{x}}^-)$ 为新息，$S = H P^- H^\top + R$ 为新息协方差。

若 EKF 模型与实际匹配，$\varepsilon$ 服从 $\chi^2(3)$ 分布（3 维观测）。若超过阈值，记为一次"NIS 失败"。

**收敛判据（`Converged()`）：**

```
在最近 N 帧中，NIS 失败率 < 40%
```

`EkfTracker` 只有在 `target.Converged() == true` 后，才将状态从 `DETECTING` 转换到 `TRACKING`（允许开火）。

---

## 9. YAML 参数全表

```yaml
auto_aim:
  ekf_predictor:
    # ── 跟踪器状态机 ──────────────────────────────────────────
    min_detect_count:            5      # DETECTING→TRACKING 所需连续更新帧数
    max_temp_lost_count:        15      # TEMP_LOST 超此帧数重置 LOST
    outpost_max_temp_lost_count: 30     # 前哨站专用，更大的丢失容忍
    max_dt_sec:                  0.1    # dt 超过此值重置（相机掉线保护）

    # ── 初始旋转半径估计 ──────────────────────────────────────
    init_radius_small:   0.27  # 小装甲板初始半径（m）
    init_radius_big:     0.27  # 大装甲板初始半径（m）
    init_radius_outpost: 0.26  # 前哨站初始半径（m）

    # ── 过程噪声方差 σ² ────────────────────────────────────────
    process_noise_pos:         100.0   # 普通车位置（cx/cy/cz 及速度）
    process_noise_ang:         400.0   # 普通车旋转角（α 及角速度）
    process_noise_outpost_pos:  10.0   # 前哨站位置噪声（更小）
    process_noise_outpost_ang:   0.1   # 前哨站角速度噪声（恒速旋转）

    # ── 发散检测 ───────────────────────────────────────────────
    divergence_threshold: 1.0e6        # 协方差迹超过此值认为发散

    # ── 弹道求解参数（TrajectorySolver）──────────────────────
    yaw_offset_deg:        0.0         # yaw 固定偏置修正（机械安装误差）
    pitch_offset_deg:      0.0         # pitch 固定偏置修正
    low_speed_delay_ms:  100.0         # 低弹速系统延迟（ms）
    high_speed_delay_ms:  70.0         # 高弹速系统延迟（ms）
    decision_speed:       25.0         # 弹速分段切换阈值（m/s）
    max_iter:             10           # 飞行时间迭代最大次数
    iter_converge_ms:      1.0         # 迭代收敛阈值（ms）
    max_approaching_angle: 1.047       # 装甲板正对角度最大值（rad），约 60°
    max_leaving_angle:     0.349       # 装甲板离开角度最大值（rad），约 20°
    bullet_speed:         23.0         # 默认弹速（m/s），串口上报后实时覆盖

    # ── 初始协方差对角（11 维，对应状态向量顺序）───────────────
    P0_diag: [1, 1, 1, 1, 1, 1, 1, 1, 0.05, 1.0e-3, 1.0e-3]
    # 顺序：cx  dcx  cy  dcy  cz  dcz  α   dα    r     Δl     Δh
```

### 参数调优建议

| 参数 | 调大效果 | 调小效果 |
|---|---|---|
| `process_noise_pos` | 更快跟上目标机动，但估计抖动增大 | 估计更平滑，但机动跟踪滞后 |
| `process_noise_ang` | 适应快速旋转目标，但角度估计抖动 | 适合匀速旋转，但突变时收敛慢 |
| `min_detect_count` | 减少误跟踪，但首次锁定更慢 | 锁定更快，但易被虚检测触发 |
| `P0_diag[8]`（$r$ 初始方差） | EKF 更快重新估计半径 | 更相信初始半径先验值 |
| `divergence_threshold` | 更少的强制重置，但允许数值发散更久 | 更激进的重置，稳定性更强 |
