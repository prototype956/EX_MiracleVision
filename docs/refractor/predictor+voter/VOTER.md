# 决策模块（VOTER）

> **总览：** [PREDICTION_DECISION_OVERVIEW.md](PREDICTION_DECISION_OVERVIEW.md)
> **预测模块：** [PREDICTOR.md](PREDICTOR.md)

---

## 目录

1. [接口层 IVoter](#1-接口层-ivoter)
2. [SimpleVoter](#2-simplevoter)
3. [CooldownVoter](#3-cooldownvoter)
4. [MpcVoter](#4-mpcvoter)
5. [YAML 参数全表](#5-yaml-参数全表)
6. [各实现对比与选型建议](#6-各实现对比与选型建议)

---

## 1. 接口层 IVoter

**文件：** `src/interfaces/i_voter.hpp`

### 1.1 职责与边界

`IVoter` 负责"根据跟踪状态决策是否允许开火"：

- **输入：** `TrackTarget`（`IPredictor::GetTrackTarget()` 的输出）+ `GimbalControl`（含 yaw/pitch）
- **输出：** `bool`（`true` = 允许开火）

与 `IPredictor` 解耦的原因：

- 预测器只关心"目标在哪里"，不关心"该不该打"
- 开火决策涉及冷却计时、对准误差容忍、轨迹预测等独立逻辑，可单独测试和替换
- 赛前可通过 YAML 切换保守/激进策略，无需重编代码

### 1.2 调用时序

```cpp
// PredictNode::Process() 中
auto control = predictor_->Predict(detections, ts, color);
auto target  = predictor_->GetTrackTarget();
control.fire = voter_->Vote(target, control);  // ← Voter 在此"签字"
output_ch_.Push({control, target, ts, frame_id});
```

### 1.3 核心方法

| 方法 | 说明 |
|---|---|
| `Init(config)` | 从 YAML 加载参数 |
| `Vote(target, control)` | 主调用接口，维护内部计时器但不修改传入参数 |
| `Reset()` | 清除冷却计时器、连续帧计数等内部状态 |

**`Reset()` 调用时机：**
- 模式切换（`AUTO_AIM → ENERGY_BUFF`）
- 目标丢失后预测器调用 `Reset()` 时联动调用

### 1.4 线程安全约定

非线程安全，在预测线程（`PredictNode`）单线程顺序调用。

---

## 2. SimpleVoter

**文件：** `src/modules/simple_voter/`  
**工厂键：** `"simple"`

### 2.1 策略

```
Vote() = auto_fire_ && target.is_tracking
```

逻辑极简：YAML 开关 + 跟踪状态直通，无任何延迟、对准度检查或冷却机制。

### 2.2 适用场景

- **调试阶段**：验证预测器输出是否正确
- **近距离静止目标测试**：排除其他变量

### 2.3 YAML 参数

```yaml
auto_aim:
  shooter:
    auto_fire: true  # false 时 Vote() 始终返回 false（锁定开火，只调试跟踪）
```

---

## 3. CooldownVoter

**文件：** `src/modules/cooldown_voter/`  
**工厂键：** `"cooldown"`  
**实现模式：** Pimpl

### 3.1 设计动机

`SimpleVoter` 在实战中存在以下问题：
1. EKF 刚开始跟踪时尚未收敛，此时开火精度差
2. 短暂掉帧后重锁，未稳定就开火
3. 云台刚转到位但目标已偏离照门

`CooldownVoter` 增加**三层防护**：

| 防护层 | 参数 | 说明 |
|---|---|---|
| ① 最小锁定帧数 | `min_lock_frames` | 连续跟踪帧不足时禁止开火 |
| ② 对准误差阈值 | `first_tolerance_rad` | 预测点与云台方向角误差过大时禁止开火 |
| ③ 丢帧宽容窗口 | `fire_tolerate_frames` | 短暂掉帧（< N 帧）后延续开火许可 |

### 3.2 内部状态机

```
状态变量：
  consecutive_track_frames  : 连续 TRACKING 帧计数
  fire_permit               : 当前开火许可
  lost_fire_frames          : 当前非 TRACKING 帧计数
```

**每帧 Vote() 逻辑流程：**

```
is_tracking == true?
├─ YES:
│    consecutive_track_frames++
│    lost_fire_frames = 0
│    angular_err = |control.yaw - target.yaw_predicted|
│    if consecutive_track_frames >= min_lock_frames
│       AND angular_err <= first_tolerance_rad:
│         fire_permit = true
└─ NO:
     consecutive_track_frames = 0
     lost_fire_frames++
     if lost_fire_frames > fire_tolerate_frames:
       fire_permit = false

return auto_fire_ && fire_permit_
```

### 3.3 状态转换图

```
                  连续跟踪 ≥ min_lock_frames
                  且 angular_err ≤ tolerance
   [非开火] ──────────────────────────────────────────► [开火]
      ▲                                                     │
      │  lost_fire_frames > fire_tolerate_frames            │
      └─────────────────────────────────────────────────────┘
                      （短暂掉帧容忍：< fire_tolerate_frames 帧不撤销）
```

### 3.4 关键实现细节

**`angular_err` 的计算：**

```cpp
float angular_err = std::abs(
    static_cast<float>(control.yaw) - static_cast<float>(target.yaw_predicted)
);
```

`control.yaw` 是 `TrajectorySolver` 输出的弹道补偿后目标 yaw，`target.yaw_predicted` 是 EKF 预测的目标朝向 yaw，两者语义一致（均为弹道补偿后的值），差值反映当前云台的对准误差。

### 3.5 YAML 参数

```yaml
auto_aim:
  cooldown_voter:
    auto_fire:            true   # false 时始终不开火
    min_lock_frames:      5      # 至少连续跟踪多少帧才允许首次开火
    first_tolerance_rad:  0.05   # 首次开火时允许的最大对准角误差（rad ≈ 2.9°）
    fire_tolerate_frames: 3      # 短暂丢帧后仍延续开火的最大帧数
```

---

## 4. MpcVoter

**文件：** `src/modules/mpc_voter/`  
**工厂键：** `"mpc"`  
**实现模式：** Pimpl

### 4.1 设计动机

`CooldownVoter` 基于**瞬时角度误差**判断开火，对高转速目标有局限：

- "目标此刻正对云台"不代表"开火后子弹飞行期间仍能命中"
- 目标高速旋转时，即使当前误差为 0，子弹飞行 70~100ms 后目标已旋转了显著角度

`MpcVoter` 将未来 1s（Horizon = 100 步，步长 10ms）的云台运动轨迹纳入考量：

> **判据：** 若 MPC 规划出的云台轨迹在 **0.5s 后**（HALF_HORIZON 处）仍能跟上目标轨迹（误差 < `fire_thresh`），则允许开火。

### 4.2 架构层次

```
MpcVoter（IVoter）
    └── MpcPlanner（detail/mpc_planner.hpp）
          ├── TinySolver (yaw)    ← TinyMPC（ADMM 在线 MPC）
          └── TinySolver (pitch)  ← TinyMPC
```

`MpcPlanner` 持有两个**独立**的 `TinySolver`（yaw 和 pitch 解耦），每帧调用 `tiny_solve()` 执行 ADMM 迭代。

### 4.3 状态空间模型

yaw 和 pitch 使用**相同结构**的双积分器模型（独立求解）：

**状态：** $\mathbf{x} = [\theta,\ \dot\theta]^\top$，**输入：** $u = \ddot\theta$

**欧拉前向离散化（步长 $\Delta t$）：**

$$
A = \begin{bmatrix}1 & \Delta t \\ 0 & 1\end{bmatrix}, \quad
B = \begin{bmatrix}0 \\ \Delta t\end{bmatrix}
$$

**代价函数：**

$$
J = \sum_{k=0}^{N-1} \left[ Q \|\theta_k - \theta_k^{\text{ref}}\|^2 + R \|\ddot\theta_k\|^2 \right]
$$

**执行器约束：**

$$
-\ddot\theta_{\max} \leq \ddot\theta_k \leq \ddot\theta_{\max}
$$

### 4.4 参考轨迹生成

在 `Vote()` 中，通过**等速线性外推**生成 $N$ 步参考轨迹：

```cpp
for (int i = 0; i < N; ++i) {
    double t = i * dt;
    cx = target.position.x() + target.velocity.x() * t;
    cy = target.position.y() + target.velocity.y() * t;
    cz = target.position.z() + target.velocity.z() * t;
    ref_yaw[i]   = atan2(cy, cx);
    ref_pitch[i] = -atan2(cz, hypot(cx, cy));
}
```

> **已知局限：** 等速外推未考虑目标的旋转运动（EKF 状态中含有 $\dot\alpha$）。对于高速旋转的装甲车，外推误差在 0.5s 后会积累，但对于匀速平移目标效果良好。

### 4.5 MPC 求解流程（每帧 Plan()）

```
1. 以 current_yaw 为原点，转换为相对坐标（避免大角度积累误差）
   yaw_ref[i] -= current_yaw

2. 用中心差分计算参考角速度
   ref_yaw_vel[i] = (ref_yaw[i+1] - ref_yaw[i-1]) / (2·dt)

3. tiny_set_x0(yaw_solver, [current_yaw-yaw0, current_yaw_vel])
   yaw_solver->work->Xref = yaw_ref_matrix

4. tiny_solve(yaw_solver)  →  最优控制序列 u*[0..N-2]
                           →  最优状态轨迹 x*[0..N-1]

5. 对 pitch 执行相同步骤（pitch 不做相对坐标转换）

6. 开火判断：
   half_idx = N/2 + 2（kShootOffset = 2，补偿计算延迟）
   yaw_err   = ref_yaw[half_idx]   - x_yaw*(half_idx)
   pitch_err = ref_pitch[half_idx] - x_pitch*(half_idx)
   fire = hypot(yaw_err, pitch_err) < fire_thresh
```

### 4.6 TinyMPC 资源管理

`TinySolver` 通过 `tiny_setup()` 动态分配内存，**TinyMPC 库无对应的 `tiny_free()`**。`MpcPlanner` 析构时手动逐项释放：

```cpp
MpcPlanner::~MpcPlanner() {
    auto free_solver = [](TinySolver* s) {
        delete s->solution;
        delete s->cache;
        delete s->settings;
        delete s->work;
        delete s;
    };
    free_solver(yaw_solver_);
    free_solver(pitch_solver_);
}
```

---

## 5. YAML 参数全表

### SimpleVoter

```yaml
auto_aim:
  shooter:
    auto_fire: true       # true=允许开火，false=锁定（调试跟踪用）
```

### CooldownVoter

```yaml
auto_aim:
  cooldown_voter:
    auto_fire:            true   # 总开关
    min_lock_frames:      5      # 首次开火所需连续跟踪帧数
    first_tolerance_rad:  0.05   # 对准角误差阈值（rad，约 2.9°）
    fire_tolerate_frames: 3      # 短暂丢帧宽容帧数
```

### MpcVoter

```yaml
auto_aim:
  mpc_voter:
    auto_fire:        true    # 总开关
    fire_thresh:      0.05    # HALF_HORIZON 处目标-规划轨迹误差阈值（rad）
    dt:               0.01    # MPC 步长（s），10ms
    horizon:          100     # 预测时域（步数），1s
    max_iter:         10      # ADMM 最大迭代次数
    rho:              1.0     # ADMM 增广拉格朗日参数
    Q_yaw:            1.0     # yaw 角度追踪代价权重
    Q_yaw_vel:        1.0     # yaw 角速度代价权重
    Q_pitch:          1.0     # pitch 角度追踪代价权重
    Q_pitch_vel:      1.0     # pitch 角速度代价权重
    R_yaw:            1.0     # yaw 角加速度输入代价（惩罚剧烈控制）
    R_pitch:          1.0     # pitch 角加速度输入代价
    max_yaw_acc:      200.0   # 最大 yaw 角加速度约束（rad/s²）
    max_pitch_acc:    200.0   # 最大 pitch 角加速度约束（rad/s²）
```

### MpcVoter 调参建议

| 参数 | 调大效果 | 调小效果 |
|---|---|---|
| `fire_thresh` | 更激进（更容易开火） | 更保守（命中精度更高） |
| `Q_yaw` / `Q_pitch` | 对参考轨迹的追踪更紧 | 更平滑，但跟踪误差更大 |
| `R_yaw` / `R_pitch` | 控制更温和，加速度更小 | 允许更大角加速度，响应更快 |
| `max_yaw_acc` | 允许云台更快速转动 | 限制云台机动能力（防止机械冲击） |
| `horizon` | 预测更长远（计算量增大）| 仅考虑短期，对慢速目标足够 |

---

## 6. 各实现对比与选型建议

| 特性 | SimpleVoter | CooldownVoter | MpcVoter |
|---|---|---|---|
| 计算开销 | 极低 | 极低 | 中等（ADMM 每帧迭代） |
| 冷却机制 | ✗ | ✓ 帧计数 | ✓ HALF_HORIZON 轨迹预测 |
| 对准度检查 | ✗ | ✓ 角误差阈值 | ✓ 2D 误差 |
| 适应高速旋转目标 | ✗ | 一般 | ✓（预测 0.5s 后可命中） |
| 掉帧宽容 | ✗ | ✓ `fire_tolerate_frames` | ✗（每帧独立决策） |
| 适用场景 | 调试 | 正式比赛（大部分场景） | 高转速目标（前哨站/快速旋转步兵） |

**推荐策略：**
- 调试期 → `"simple"`
- 常规比赛 → `"cooldown"`（`min_lock_frames=5, first_tolerance_rad=0.05`）
- 前哨站专打模式 → `"mpc"`（调低 `fire_thresh` 至 0.03~0.04）
