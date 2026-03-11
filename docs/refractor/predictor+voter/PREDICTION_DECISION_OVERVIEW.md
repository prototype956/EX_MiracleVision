# 预测与决策模块总览

> **适用读者：** 新成员 / 模块交接
> **详细文档：** [PREDICTOR.md](PREDICTOR.md) · [VOTER.md](VOTER.md)

---

## 1. 模块定位

视觉流水线的核心链路：

```
相机帧 → [DetectNode] → Detection[] → [PredictNode] → GimbalControl → [SerialNode] → 串口
                                              ↑
                            IPredictor (跟踪/预测)
                            IVoter     (开火表决)
```

- **IPredictor**：跨帧跟踪目标，输出预判的云台角度（yaw / pitch）
- **IVoter**：对 `GimbalControl.fire` 字段"签字"，决定本帧是否允许开火

两者均通过**工厂模式**注册，只需修改 `vision.yaml` 中的 `type` 字段即可无缝切换实现，无需改动 Pipeline 代码。

---

## 2. Pipeline 中的调用时序

```cpp
// PredictNode::Process() 伪代码
auto control = predictor_->Predict(detections, timestamp, enemy_color);
// ↑ 内部完成：坐标系变换 → EKF 状态更新 → TrajectorySolver 弹道求解

auto target = predictor_->GetTrackTarget();

control.fire = voter_->Vote(target, control);
// ↑ 内部完成：冷却计时 / MPC 轨迹预测 → 最终开火决策

output_channel_.Push({control, target, timestamp, frame_id});
```

---

## 3. 可插拔实现列表

### 预测器（`auto_aim.predictor.type`）

| 工厂键 | 类名 | 适用场景 |
|---|---|---|
| `"simple"` | `SimplePredictor` | 调试 / 早期集成验证，无滤波 |
| `"ekf"` | `EkfPredictor` | 正式比赛，带 EKF + 弹道补偿 |

### 投票器（`auto_aim.voter.type`）

| 工厂键 | 类名 | 适用场景 |
|---|---|---|
| `"simple"` | `SimpleVoter` | 调试，仅检查 is_tracking |
| `"cooldown"` | `CooldownVoter` | 正式比赛，带最小锁定帧数 + 冷却容忍 |
| `"mpc"` | `MpcVoter` | 高转速目标，MPC 预测 0.5s 后能否命中 |

---

## 4. YAML 切换方式

```yaml
# configs/vision.yaml
auto_aim:
  predictor:
    type: "ekf"      # 或 "simple"

  voter:
    type: "cooldown"  # 或 "simple" / "mpc"
```

修改后重启进程即生效，不需要重新编译。

---

## 5. 数据流图

```
Detection { xyz_in_gimbal, yaw_angle, number, color, ... }
     │
     │  SetGimbalOrientation(q)  [EkfPredictor 专属，每帧更新]
     │        │
     ▼        ▼
  IPredictor::Predict()
     │
     ├─ [SimplePredictor] 最优目标选择 → 直接输出 yaw/pitch
     │
     └─ [EkfPredictor]
           坐标系变换 (云台系 → 世界系)
                │
           EkfTracker::Track()
           ┌───┴──────────────────────────────────────┐
           │  LOST → DETECTING → TRACKING → TEMP_LOST │
           │     EkfTrackTarget (11 维 EKF)            │
           └──────────────────────────────────┬────────┘
                                              │
                                    TrajectorySolver::Solve()
                                    飞行时间迭代 → yaw / pitch
     │
     ▼
GimbalControl { yaw, pitch, tracking, fire=false }
     │
     ▼
  IVoter::Vote(target, control)
     │
     ├─ [SimpleVoter]    is_tracking → fire
     ├─ [CooldownVoter]  连续帧计数 + 角误差阈值 → fire
     └─ [MpcVoter]       线性外推参考轨迹 → TinyMPC → HALF_HORIZON 误差 → fire
     │
     ▼
GimbalControl { ..., fire=true/false }  →  SerialNode
```

---

## 6. 接口文件索引

| 文件 | 说明 |
|---|---|
| [`src/interfaces/i_predictor.hpp`](../src/interfaces/i_predictor.hpp) | IPredictor 抽象接口 |
| [`src/interfaces/i_voter.hpp`](../src/interfaces/i_voter.hpp) | IVoter 抽象接口 |
| [`src/interfaces/types.hpp`](../src/interfaces/types.hpp) | Detection / GimbalControl / TrackTarget 类型定义 |
| [`src/modules/ekf_predictor/`](../src/modules/ekf_predictor/) | EkfPredictor 实现 |
| [`src/modules/simple_predictor/`](../src/modules/simple_predictor/) | SimplePredictor 实现 |
| [`src/modules/cooldown_voter/`](../src/modules/cooldown_voter/) | CooldownVoter 实现 |
| [`src/modules/mpc_voter/`](../src/modules/mpc_voter/) | MpcVoter 实现 |
| [`src/modules/simple_voter/`](../src/modules/simple_voter/) | SimpleVoter 实现 |
