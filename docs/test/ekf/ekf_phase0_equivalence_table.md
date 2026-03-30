# EKF Phase 0 行为等价对照表（Card 0-1 ~ 0-12）

## 1. 对照目标

本文档用于收口 Phase 0。每个 Card 给出“对齐结论 + 证据入口 + 当前状态”，用于对应 Gate-G / Gate-H / Gate-I 的审查证据。

## 2. 对照表

| Card | 对齐对象 | 对齐结论 | 证据入口 | 状态 |
|---|---|---|---|---|
| 0-1 | `EkfTrackTarget::Update` 观测口径 | 已切换到 ypd+yaw 观测链，角度分量限幅 | `mv-ekf-track-target-update-test` 运行通过 | 完成 |
| 0-2 | `EkfTrackTarget::HJacobian` 维度语义 | Jacobian 与 4 维观测链一致（4x11） | `mv-ekf-track-target-update-test`（含维度相关断言） | 完成 |
| 0-3 | `SelectNearestArmor` tie-break 稳定性 | 并列误差场景优先保持 `last_id`，抑制切换抖动 | `mv-ekf-track-target-update-test` | 完成 |
| 0-4 | `Predict(double)` 噪声注入 | 普通/前哨站噪声来源由参数注入，非硬编码 | `mv-ekf-track-target-update-test` | 完成 |
| 0-5 | `EkfTracker::SetTarget` P0 优先级 | 分支优先级固定，outpost/base/balance 不被全局 P0 覆盖 | `mv-ekf-tracker-set-target-test` | 完成 |
| 0-6 | `EkfPredictor::Init` 配置路径映射 | ROOT_CFG / auto_aim 子树 / predictor 子树三路径可用 | `mv-ekf-predictor-init-config-test` | 完成 |
| 0-7 | `ChooseAimPoint` lock_id 防抖 | 双候选锁定、单候选释放语义与 SP 口径一致 | `mv-trajectory-solver-choose-aim-point-test` | 完成 |
| 0-8 | `Solve` 迭代时序与边界 | future-based 收敛时序对齐；`fire` 仍由 Voter 决策 | `mv-trajectory-solver-solve-iter-test` | 完成 |
| 0-9 | `Track/StateMachine` 迁移语义 | 大 dt reset、outpost TEMP_LOST 容忍、同号异型拒更 | `mv-ekf-tracker-state-machine-test` | 完成 |
| 0-10 | 头文件注释契约 | `ekf_track_target.hpp`、`trajectory_solver.hpp` 注释与实现一致 | 文档审查 + 头文件变更记录 | 完成 |
| 0-11 | 配置键对齐 | 分组/平铺兼容键与 P0 新旧键并存可用 | YAML 加载 + `mv-ekf-predictor-init-config-test` | 完成 |
| 0-12 | 测试矩阵落地 | Card 0-1~0-11 一一映射，含失败场景退化行为 | `docs/test/ekf/ekf_predictor_test.md` | 完成 |

## 3. 本轮回归证据（2026-03-31）

已执行最小回归集并全部通过：

```bash
cmake --build build --target \
  mv-ekf-track-target-update-test \
  mv-ekf-tracker-set-target-test \
  mv-ekf-predictor-init-config-test \
  mv-trajectory-solver-choose-aim-point-test \
  mv-trajectory-solver-solve-iter-test \
  mv-ekf-tracker-state-machine-test -j$(nproc)

./build/src/test/mv-ekf-track-target-update-test
./build/src/test/mv-ekf-tracker-set-target-test
./build/src/test/mv-ekf-predictor-init-config-test
./build/src/test/mv-trajectory-solver-choose-aim-point-test
./build/src/test/mv-trajectory-solver-solve-iter-test
./build/src/test/mv-ekf-tracker-state-machine-test
```

对应结果：6/6 PASS，退出码 0。

## 4. Gate 对应关系

- Gate-G（状态机语义、注释口径、配置键映射、测试矩阵闭环）：已满足。
- Gate-H（失败分支具备日志与可预期返回）：已在测试矩阵“失败场景与退化行为矩阵”覆盖。
- Gate-I（Card 0-1~0-12 具备执行条目与证据）：已满足。
