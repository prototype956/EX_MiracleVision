# Trajectory Solver 模块说明

## 1. 模块位置
- 实现：`src/modules/ekf_predictor/detail/trajectory_solver.cpp`
- 接口：`src/modules/ekf_predictor/detail/trajectory_solver.hpp`

## 2. 职责边界
- 输入：`EkfTrackTarget` 当前状态 + 时间戳 + 弹速。
- 输出：`GimbalControl` 的 `yaw/pitch/distance/tracking`。
- 不负责开火决策：`fire` 保持由 Voter 决定。

## 3. Card 0-7 行为对齐（EX -> SP）

### 3.1 变更点
在 `TrajectorySolver::ChooseAimPoint` 中加入 `lock_id_` 防抖语义：
1. 非小陀螺分支先筛出可射击候选 `id_list`。
2. 当候选数 `>1` 时：
   - 若当前未锁定或锁定 id 失效，按角误差更小者进入锁定。
   - 若锁定 id 仍在候选中，保持锁定，不切换。
3. 当候选数 `==1` 时：退出锁定（`lock_id_=-1`），返回唯一候选。

### 3.2 设计意图
- 抑制两块装甲板都接近可击打边界时的帧间来回翻转。
- 对齐 `sp_vision_25/tasks/auto_aim/aimer.cpp` 的双候选锁定策略。

### 3.3 验证方式
- 新增测试：`src/test/ekf/trajectory_solver_choose_aim_point_test.cpp`
- 测试目标：`mv-trajectory-solver-choose-aim-point-test`
- 关键断言：双候选窗口内，两次选择在输入轻微变化下保持同一 id。

## 4. 兼容性说明
- 本次仅影响 `TrajectorySolver` 内部选板策略，不修改公共接口。
- 失败路径语义保持不变：无候选时返回 `valid=false`。

## 5. Card 0-8 执行结果（迭代时序与失败回退）

### 5.1 现状结论
`TrajectorySolver::Solve` 当前实现已满足 Card 0-8 的目标语义：
1. 初始不可解与迭代不可解分支分别记录日志并返回 `tracking=false`。
2. 收敛阈值触发时立即 `break`；达到最大迭代时输出最后有效结果。
3. 接口边界保持：`fire=false`，开火决策仍由 Voter 负责。

### 5.2 本轮新增验证
- 新增测试：`src/test/ekf/trajectory_solver_solve_iter_test.cpp`
- 测试目标：`mv-trajectory-solver-solve-iter-test`
- 关键断言：
   - `Solve` 输出 `tracking=true` 时 `fire` 仍为 `false`；
   - 输出 `pitch` 与 SP 口径的 future-based 迭代时序计算一致。
