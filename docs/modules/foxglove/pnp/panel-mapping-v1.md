# PnP 参数到面板映射 V1

## 目标
在不增加复杂视觉资产的前提下，将 PnP 关键参数映射到最合适的 Foxglove 面板，实现“快速判断 + 快速定位”。

## 核心层级

### 1. 关键数值层（主看板）
- `yaw_angle` -> Plot
- `pitch_angle` -> Plot
- `distance_m` -> Plot
- `reproj_error` -> Plot + Stat
- `is_solved` -> Indicator
- `target_meta`（type/id） -> Table

### 2. 几何层
- `reprojected_points` + `points` -> Image Overlay
- `xyz_in_gimbal` -> 3D
- `axes_3d` -> 3D Scene

### 3. 诊断层
- `solve_latency_ms` -> Plot
- `fail_reason` -> Log/Table
- `active_target_count` -> Stat
- `config_snapshot` -> Raw Message

## 运行策略
- 数值层：全帧率。
- 几何层：15-20Hz，异常时升至 25-30Hz（1-2 秒）。
- 诊断层：按事件触发或低频刷新。
