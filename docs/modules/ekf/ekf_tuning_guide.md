# EKF 与轨迹关键参数调参手册

本文针对现场调试人员，给出每个关键参数的作用、如何判定当前值是否合适、以及分阶段调参的建议顺序。

## 1. 参数地图（配置键 → 代码字段 → 工作点）

所有参数均由 `[vision.yaml](configs/vision.yaml)` 的 `auto_aim.ekf_predictor` 节点配置，加载逻辑在 [ekf_predictor.cpp L60-100](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/ekf_predictor.cpp)。

### 1.1 追踪状态机参数

| YAML 键名 | C++ 字段 | 默认值 | 单位 | 影响行为 | 看代码 |
|-----------|---------|-------|------|---------|--------|
| `min_detect_count` | `min_detect_count` | 5 | 帧 | DETECTING→TRACKING 的转换速度 | [ekf_tracker.cpp L214](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L214) |
| `max_detecting_lost_count` | `max_detecting_lost_count` | 2 | 帧 | DETECTING 单次漏检容忍度 | [ekf_tracker.cpp L226](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L226) |
| `max_temp_lost_count` | `max_temp_lost_count` | 15 | 帧 | TRACKING 掉线后的保留时间 | [ekf_tracker.cpp L254](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L254) |
| `outpost_max_temp_lost_count` | `outpost_max_temp_lost_count` | 30 | 帧 | 前哨站同上（更长） | [ekf_tracker.cpp L240](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L240) |
| `max_dt_sec` | `max_dt_sec` | 0.1 | 秒 | 相机掉线时的硬重置阈值 | [ekf_tracker.cpp L65](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L65) |
| `init_radius_small` | `init_radius_small` | 0.27 | 米 | 小装甲板初始化半径 | [ekf_tracker.cpp L153](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L153) |
| `init_radius_big` | `init_radius_big` | 0.27 | 米 | 大装甲板初始化半径 | [ekf_tracker.cpp L153](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L153) |
| `init_radius_outpost` | `init_radius_outpost` | 0.26 | 米 | 前哨站初始化半径 | [ekf_tracker.cpp L153](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L153) |

### 1.2 过程噪声与初始协方差

| YAML 路径 | C++ 字段 | 默认值 | 单位 | 影响行为 | 看代码 |
|-----------|---------|-------|------|---------|--------|
| `process_noise.normal.pos` | `process_noise_pos` | 100.0 | m²/s⁴ | 位置快速变化的容忍度 | [ekf_track_target.cpp L120](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp#L120) |
| `process_noise.normal.ang` | `process_noise_ang` | 400.0 | rad²/s⁴ | 角度（小陀螺）快速变化 | [ekf_track_target.cpp L120](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp#L120) |
| `process_noise.outpost.pos` | `process_noise_outpost_pos` | 10.0 | m²/s⁴ | 前哨站位置（缓变） | [ekf_track_target.cpp L116](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp#L116) |
| `process_noise.outpost.ang` | `process_noise_outpost_ang` | 0.1 | rad²/s⁴ | 前哨站转速（匀速） | [ekf_track_target.cpp L116](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_track_target.cpp#L116) |
| `p0_diag.default` | `P0_diag (vector<11>)` | [1,1,1,1,1,1,1,1,0.05,0.001,0.001] | 各维单位的平方 | 初始状态不确定度 | [ekf_tracker.cpp L160](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/ekf_tracker.cpp#L160) |

**P0_diag 的 11 个分量对应**：
- P0_diag[0-5]：位置与速度的初始方差（单位 m² 与 m²/s²）
- P0_diag[6-7]：角度与角速度（单位 rad² 与 rad²/s²）
- P0_diag[8-10]：几何参数（单位 m，通常很小）

### 1.3 弹道与瞄准参数

| YAML 键名 | C++ 字段 | 默认值 | 单位 | 影响行为 | 看代码 |
|-----------|---------|-------|------|---------|--------|
| `yaw_offset_deg` | `yaw_offset_rad` | 0.0 | 度 | yaw 固定偏置矫正 | [trajectory_solver.cpp 末 50 行](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp) |
| `pitch_offset_deg` | `pitch_offset_rad` | 0.0 | 度 | pitch 固定偏置矫正 | [trajectory_solver.cpp 末 50 行](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp) |
| `low_speed_delay_ms` | `low_speed_delay_ms` | 100.0 | 毫秒 | 低弹速下的枪管响应延迟 | [trajectory_solver.cpp L89](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp#L89) |
| `high_speed_delay_ms` | `high_speed_delay_ms` | 70.0 | 毫秒 | 高弹速下的延迟 | [trajectory_solver.cpp L91](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp#L91) |
| `decision_speed` | `decision_speed` | 25.0 | m/s | 低速→高速延迟的切换点 | [trajectory_solver.cpp L89](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp#L89) |
| `max_iter` | `max_iter` | 10 | 次 | 飞行时间迭代上限（通常不调） | [trajectory_solver.cpp L123](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp#L123) |
| `iter_converge_ms` | `iter_converge_ms` | 1.0 | 毫秒 | 迭代收敛判定阈值（通常不调） | [trajectory_solver.cpp L145](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp#L145) |
| `max_approaching_angle` | `max_approaching_angle` | 1.047 | 弧度 | 正向装甲板角度范围（≈60°） | [trajectory_solver.cpp ChooseAimPoint](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp) |
| `max_leaving_angle` | `max_leaving_angle` | 0.349 | 弧度 | 背向装甲板角度范围（≈20°） | [trajectory_solver.cpp ChooseAimPoint](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/detail/trajectory_solver.cpp) |

## 2. 症状与参数的对应关系

### 追踪稳定性问题 → 状态机参数

| 症状 | 关键参数 | 调整方向 | 依据 |
|------|---------|---------|------|
| **DETECTING 阶段过长，锁定较慢** | ↑ `min_detect_count` | 减小（5→3） | 减少需要的有效帧数，加快进入 TRACKING |
| **误入 TRACKING 又快速掉线**（噪声一帧导入） | ↓ `min_detect_count` | 增大（5→8） | 增加门限，防止单次误检进入稳定模式 |
| **短暂遮挡（< 500ms）直接掉线**（容忍度不足） | ↓ `max_temp_lost_count` | 增大（15→25） | 增加 TEMP_LOST 允许的帧数，容忍短暂失检 |
| **掉线后很难恢复跟踪**（优先级或初始化问题） | - | 优先检查检测器输出，而非参数 | 问题根源通常不在参数 |
| **相机掉线后恢复不了**（超时保护失效） | `max_dt_sec` | 调大（0.1→0.2），观察现象是否改善 | 通常不推荐改，这是硬件保护 |

### EKF 收敛与抖动问题 → 噪声参数

| 症状 | 关键参数 | 调整方向 | 物理含义 |
|------|---------|---------|---------|
| **前 3 帧内收敛太慢，预测大幅滞后** | ↑ `process_noise_*` 或 ↓ `P0_diag` | 减小 process_noise（100→50）；增大 P0_diag 速度项 | 降低过程噪声→信任历史状态更多→收敛依赖前期观测多 |
| **快速收敛但抖动明显（小陀螺时尤甚）** | ↓ `process_noise_*` 或 ↑ `P0_diag` | 增大 process_noise（100→200）；减小 P0_diag | 提高过程噪声→信任新观测更多→容易跟随噪声 |
| **角度估计波动大**（小陀螺或吊打） | process_noise_ang | 减小（400→200）或维持 | 降低角加速度方差→滤波器认为转速变化慢→平滑化估计 |
| **静止目标预测偏差大但最后稳定** | - | 观察数帧后是否自动收敛 | 通常是初始化问题或坐标系统问题（非参数问题） |

### 打点偏离问题 → 延迟与偏置参数

| 症状 | 关键参数 | 调整方向 | 校验方式 |
|------|---------|---------|---------|
| **低弹速下恒向偏晚** | ↓ `low_speed_delay_ms` | 减小（100→80） | 靶场 10m、弹速 < 20 m/s，连续 10 发，观察偏向是否改善 |
| **高弹速下恒向偏早** | ↑ `high_speed_delay_ms` | 增大（70→90） | 靶场 10m、弹速 > 28 m/s，同上 |
| **整体向某侧偏离（yaw 方向）** | `yaw_offset_deg` | 向对侧调整 | 靶场 5m 静止 30 发，记录平均偏向 → 修正值 = 偏向量 |
| **整体向上/下偏离（pitch 方向）** | `pitch_offset_deg` | 向对侧调整 | 同上，多距离（5/10/15m）重复测试保证一致性 |
| **弹速依赖的非线性偏差**（low 偏晚、high 偏早） | decision_speed | 微调分界点（±5 m/s 试） | 若弹速范围宽（15-35 m/s），可能需要分段参数（高级） |

## 3. 推荐调参顺序与验收标准

### 阶段 1：确保追踪稳定性（第一次能锁定并保持）
**目标**：能稳定进入 TRACKING 状态，丢跟率 < 20%  
**耗时**：10-20 分钟（静止摄像对静止目标）  
**顺序**：
1. min_detect_count = 5（默认），观察日志输出 "DETECTING → TRACKING"
   - 如果大量 "DETECTING miss"，说明检测器有问题，先不调参
   - 如果转换太慢（> 1.5秒），改为 3
2. max_temp_lost_count = 15（默认），人工遮挡目标，观察恢复时间
   - 如果 > 500ms 仍无法恢复，改为 25
   - 如果频繁误触 TEMP_LOST，改为 10

**验收标准**：
```
[EkfTracker] state: "tracking" (日志频繁出现)
[EkfTracker] lost_count < 5 per 100 frames (丢跟少于 5%)
```

### 阶段 2：微调 EKF 收敛与平滑（静止目标打点）
**目标**：前 3 帧收敛，无多余抖动，静止时位置标准差 < 2cm  
**耗时**：15-30 分钟  
**工具**：Foxglove 可视化 + 靶纸打点

**步骤**：
1. 启动完整管线，对准静止靶纸，运行 10秒 → 录制日志与 Foxglove 消息
2. 离线分析：
   - 展开 `TrackTarget.position` 的时间序列
   - 计算前 3 帧与之后 10 帧的预测位置偏差（pixel）
   - 如果前 3 帧偏差 > 100 px，调参
3. 微调参数：
   ```
   if 前期偏差大 and 快速收敛:
       process_noise_pos: 100 → 50  // 信任历史更多
   else if 收敛快但跳跃明显 (std > 5px):
       process_noise_pos: 100 → 200  // 信任观测更多
   ```
4. 每次调整后，重新跑一遍步骤 1， compare 偏差变化

**验收标准**：
```
预测误差 RMS < 2 cm @ 5m 距离
前 3 帧的标准差 < 前 10 帧的标准差 × 1.5
NIS 失败率 < 20%
```

### 阶段 3：弹道补偿校准（靶场距离梯度）
**目标**：不同弹速、距离重复命中率 > 80%  
**耗时**：40-60 分钟（现场靶场、多距离）  
**工具**：靶纸、弹速测量工具（或从串口读实时弹速）

**分步骤**：
1. **标定弹速基准**
   - 发射 5 发并用速度枪测定，取平均 $v_0$（m/s）
   - 在配置中确认该值用于 `bullet_speed` 参数
   ```yaml
   # 临时写死用于调试：
   decision_speed: 25.0  # 如果 v0=23, 就设 23
   ```

2. **低、中、高弹速各 10 发测试**
   - 5m 距离，低速（< 22 m/s）：注意是否恒向偏晚、偏早或抖动
   - 15m 距离，中速（22-28 m/s）：观察打点位置
   - 10m 距离，高速（> 28 m/s）：记录是否偏早
   
3. **调整延迟参数**
   ```
   if 所有低速都偏晚超过 10cm:
       low_speed_delay_ms: 100 → 80
   if 所有高速都偏早超过 10cm:
       high_speed_delay_ms: 70 → 90
   ```

4. **固定偏置校准**
   - 在同一距离与角度连续 20 发
   - 记录打点中心偏离靶心的均值
   - 计算修正值：
   ```
   yaw_offset = -mean_yaw_error
   pitch_offset = -mean_pitch_error
   ```

**验收标准**：
```
5m / 10m / 15m 各距离命中率 ≥ 80%
打点散布 < 5cm @ 10m
系统性偏差 < 2cm（无需further微调）
```

### 阶段 4：高速运动精细化（复赛场景）
**目标**：圆周转动、摇摆目标的稳定跟踪与命中  
**耗时**：30-45 分钟（现场机械运动或视频回放）

**监控指标**：
- 装甲板跳变频率：每 100 帧 < 2 次（过频表示选择不稳定）
- 预测提前量（yaw_predicted - true_yaw）方差：< 0.1 rad
- TRACKING 状态占比 > 95%

**若指标不达标**：
- 跳变频：检查装甲板类型定义，优先检查硬件而非参数
- 预测方差大：可用 process_noise_ang 微调，但通常是坐标系统问题
- 丢跟：回到阶段 1 重新调整状态机参数

## 4. ⚠️ 禁忌区：不建议首先调的参数

| 参数 | 原因 | 何时例外 |
|------|------|---------|
| `P0_diag` | 依赖目标类型，全局调整影响初始化质量；前哨站有专用值 | 接到新装甲车型、高度差的实物时才考虑重新测量 |
| `process_noise_outpost_*` | 前哨站转速恒定且已优化，错误调整导致发散 | 赛场前哨站转速明显变化（需实测新转速）时 |
| `init_radius_small/big/outpost` | 工程机械参数，随机调破坏初始化质量 | 机械更新或收到新样品车时才更新 |
| `max_iter` / `iter_converge_ms` | 与弹道收敛相关，非追踪参数，改动无益 | 仅在弹道验证阶段观察，生产不调 |
| `max_detecting_lost_count` | 仅影响 DETECTING 阶段的单次漏检容忍，与主追踪无关 | 极少遇到需调的场景 |

## 5. 快速诊断决策树

```
❌ 无法进入追踪状态
  ├─ 日志无 "匹配成功" → 检测器问题，不是参数
  ├─ 日志频繁 "DETECTING miss"
  │  └─ 原因：目标高速或检测不稳定
  │  └─ 调整：min_detect_count 5→3，加快转换
  └─ 日志显示 update_count ≥ 3，但 Converged() = false
     └─ 原因：radius diverged 或坐标系统错误
     └─ 调整：检查 init_radius 与后续坐标链路

❌ 频繁掉线 (TRACKING → TEMP_LOST)
  ├─ 场景是瞬间遮挡 (< 30ms)
  │  └─ 调整：max_temp_lost_count 15→25
  ├─ 场景是装甲板跳变 (hardware)
  │  └─ 调整：process_noise_ang 400→300（降低敏感性）
  └─ 场景是目标消失 (无法改善)
     └─ 接受，这是正常丢跟

❌ 打点偏离 > 10cm @ 10m
  ├─ 打点方向恒定（系统性偏移）
  │  └─ 调整：yaw_offset_deg / pitch_offset_deg
  ├─ 打点随弹速变化（延迟问题）
  │  └─ 调整：low_speed_delay_ms / high_speed_delay_ms
  └─ 打点随距离变化（弹道模型问题）
     └─ 超出参数范围，需要算法改进

❌ 快速转动时位置跳跃
  ├─ 预测点稳定但转向错（装甲板选择错）
  │  └─ 不这参数无法改善，检查机械或检测
  ├─ 预测点本身跳跃（坐标系统问题）
  │  └─ 优先检查四元数与坐标转换，见坐标文档
  └─ NIS 持续高 > 7.8（EKF 发散）
     └─ 重新初始化或检查输入数据质量
```

## 6. 验收与回归检查清单

提交任何参数改动前，请完整验证以下项目：

- [ ] 状态机稳定性（TRACKING 时间占比 > 95%）
- [ ] EKF 收敛性（前 3 帧误差 < 100px，NIS 失败率 < 20%）
- [ ] 弹道精度（5/10/15m 多距离命中率 > 80%）
- [ ] 高速运动（圆周转动、摇摆打点散布 < 10cm）
- [ ] 掉线恢复能力（短暂遮挡 500ms 内恢复）
- [ ] 日志一致性（无异常 diverged/NIS 告警）
- [ ] 配置与代码一致性（YAML 键名与代码读取逻辑对应）

## 7. 进阶阅读

- 详细数学原理：见[EKF 数学与直觉详解](ekf_detailed_mathematics.md)
- 坐标转换与安装调参：见[坐标转换调参指南](coordinate_transformation_tuning.md)
- 主文档与索引：见[EKF Predictor 模块工作原理](ekf_predictor.md)
