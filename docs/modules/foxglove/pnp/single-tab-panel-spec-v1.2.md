# PnP 单选项卡面板规格 V1.2

## 全局规则
- 调试模式：启用全部面板。
- 比赛模式：默认关闭全部 PnP 可视化。
- `reproj_error` 默认阈值：黄线 3px，红线 5px。

## A. 顶栏主看板

### 1. Yaw
- 标题：`Yaw`
- 字段：`yaw_angle`
- 单位：`deg`（内部可保留 `rad`）
- 颜色规则：
  - 绿：变化率 < 20 deg/s
  - 黄：20-60 deg/s
  - 红：> 60 deg/s
- 触发条件：每帧更新；`is_solved=false` 灰化

### 2. Pitch
- 标题：`Pitch`
- 字段：`pitch_angle`
- 单位：`deg`
- 颜色规则：
  - 绿：变化率 < 15 deg/s
  - 黄：15-40 deg/s
  - 红：> 40 deg/s
- 触发条件：每帧更新；`is_solved=false` 灰化

### 3. Distance
- 标题：`Distance`
- 字段：`distance_m`
- 单位：`m`
- 颜色规则：
  - 绿：相邻帧变化 < 0.3m
  - 黄：0.3-0.8m
  - 红：> 0.8m
- 触发条件：每帧更新；丢目标时短时保留并标记 stale

### 4. Reprojection Error
- 标题：`Reprojection Error`
- 字段：`reproj_error`
- 单位：`px`
- 颜色规则：
  - 绿：< 3.0px
  - 黄：3.0-5.0px
  - 红：>= 5.0px
- 触发条件：每帧更新；连续红色 5 帧触发几何层升频

### 5. Solve Status
- 标题：`Solve Status`
- 字段：`is_solved`
- 单位：`bool`
- 颜色规则：
  - 绿：true
  - 红：false
- 触发条件：每帧更新；连续 false 超过 5 帧自动展开诊断区

### 6. Target Meta
- 标题：`Target Meta`
- 字段：`armor_type`、`target_id`
- 单位：`enum/int`
- 颜色规则：
  - 绿：稳定
  - 黄：1 秒内跳变 1 次
  - 红：1 秒内跳变 >= 2 次
- 触发条件：每帧更新；切换时短时高亮

## B. 中层几何区

### 1. Reprojection Overlay
- 标题：`Reprojection Overlay`
- 字段：`points`、`reprojected_points`
- 单位：`px`
- 颜色规则：
  - 原始角点：绿
  - 重投影点：青
  - 误差线：按 3px/5px 阈值分级
- 触发条件：15-20Hz；告警时突发升频

### 2. Pose 3D
- 标题：`Pose 3D (Gimbal)`
- 字段：`xyz_in_gimbal`、`axis_marker`
- 单位：`m`
- 颜色规则：轨迹按速度分级；失解灰化
- 触发条件：15-20Hz；深度突变时突发升频

### 3. Trajectory
- 标题：`Trajectory Short History`
- 字段：`xyz_history`
- 单位：`m`
- 颜色规则：平滑绿、突变黄、断裂红
- 触发条件：随几何层刷新滚动更新

## C. 底层诊断区

### 1. Solve Latency
- 标题：`Solve Latency`
- 字段：`solve_latency_ms`
- 单位：`ms`
- 颜色规则：
  - 绿：< 2ms
  - 黄：2-5ms
  - 红：> 5ms
- 触发条件：每帧记录，1 秒窗口统计

### 2. Failure Reason
- 标题：`Failure Reason`
- 字段：`fail_reason_code`、`fail_count`
- 单位：`enum/count`
- 颜色规则：按失败频率分级
- 触发条件：解算失败时更新

### 3. Target Count
- 标题：`Target Count`
- 字段：`active_target_count`
- 单位：`count`
- 颜色规则：
  - 绿：1
  - 黄：2-3
  - 红：0
- 触发条件：每帧更新；0 持续 0.5 秒告警

### 4. Config Snapshot
- 标题：`Config Snapshot`
- 字段：`camera_matrix_hash`、`extrinsic_hash`、`debug_mode`
- 单位：`hash/bool`
- 颜色规则：稳定绿、变化红
- 触发条件：启动时发送；配置变更时补发
