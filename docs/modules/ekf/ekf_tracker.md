# Ekf Tracker 模块说明

## 1. 模块位置
- 实现：`src/modules/ekf_predictor/detail/ekf_tracker.cpp`
- 接口：`src/modules/ekf_predictor/detail/ekf_tracker.hpp`

## 2. Card 0-9 行为对齐（EX -> SP）

### 2.1 本次变更点
在 `EkfTracker::UpdateTarget` 中，将匹配条件从“仅比较 `ArmorNumber`”调整为“同时比较 `ArmorNumber + ArmorType`”。

### 2.2 设计意图
- 避免同编号但不同装甲板类型（small/big）误匹配到当前目标。
- 与 `sp_vision_25/tasks/auto_aim/tracker.cpp` 的更新口径一致。
- 降低状态机在目标切换边界场景下的误更新风险。

### 2.3 本轮状态机验证覆盖
- 大 `dt` 直接 reset 到 `LOST`。
- `outpost` 在 `TEMP_LOST` 的容忍上限高于普通目标。
- `UpdateTarget` 对错误类型检测结果返回 `false`。

### 2.4 验证方式
- 新增测试：`src/test/ekf/ekf_tracker_state_machine_test.cpp`
- 测试目标：`mv-ekf-tracker-state-machine-test`
- 关键断言：
  - `large dt` 时 `Track()` 返回 `nullopt` 且状态重置为 `LOST`；
  - `outpost` 在达到普通阈值前仍保持 `TEMP_LOST`；
  - 同编号不同类型检测不会触发更新。

### 2.5 当前数据源约束与比赛场景影响
- 目前传统检测链路中，`ArmorNumber` 多为 `UNKNOWN`，`ArmorType` 通常可用。
- 在 `ArmorNumber + ArmorType` 匹配口径下，编号缺失时仍可利用类型过滤降低误匹配，但无法提供完整 ID 级区分能力。
- 在仅联盟赛（无前哨站）场景，该风险相对对抗赛更低；但在同类型目标并行、交错或遮挡重现时，仍存在误关联上限。
- 若后续接入稳定编号识别，该匹配策略收益会进一步提升。
