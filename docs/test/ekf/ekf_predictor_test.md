# EKF Predictor 对齐验证矩阵（Card 0-1 ~ 0-11）

## 1. 文档目标

本文档用于将 Phase 0 的 Card 0-1 到 0-11 变更落到可执行验证条目，确保每个改动至少有一个可复现实验入口，并同时包含 SP 对照项与 EX 断言项。

## 2. 执行前置

- 仓库根目录：`/home/nuc/EX_MiracleVision`
- 已完成配置生成与依赖安装
- 推荐先执行一次目标构建：

```bash
cmake --build build -j$(nproc)
```

## 3. 验证矩阵（Card 0-1 ~ 0-11）

| Card | 代码位置 | 输入场景 | 执行步骤 | SP 对照项 | EX 断言项（通过标准） | 证据入口 |
|---|---|---|---|---|---|---|
| 0-1 | `src/modules/ekf_predictor/detail/ekf_track_target.cpp::Update` | 固定 xyz，仅改变 yaw/pitch | 1) `cmake --build build --target mv-ekf-track-target-update-test -j$(nproc)` 2) `./build/src/test/mv-ekf-track-target-update-test` | `sp_vision_25/tasks/auto_aim/target.cpp::update/update_ypda` 的 ypd+yaw 观测语义 | 观测更新使用 4 维口径；角度差分限幅；测试输出 `[PASS] ekf_track_target_update_test` | `mv-ekf-track-target-update-test` |
| 0-2 | `src/modules/ekf_predictor/detail/ekf_track_target.cpp::HJacobian` | 静态目标多次更新，验证 Jacobian 与观测维一致 | 同 Card 0-1 测试运行 | SP `h_jacobian` 的 ypd+yaw 链式导数口径 | H 行数与观测维一致（4x11）；无维度错误、无 NaN/Inf；测试通过 | `mv-ekf-track-target-update-test` |
| 0-3 | `src/modules/ekf_predictor/detail/ekf_track_target.cpp::SelectNearestArmor` | 构造角误差并列候选 | 同 Card 0-1 测试运行 | SP 候选角误差最小与稳定 tie-break 语义 | 并列误差时优先保持 `last_id`，减少抖动；测试通过 | `mv-ekf-track-target-update-test` |
| 0-4 | `src/modules/ekf_predictor/detail/ekf_track_target.cpp::Predict(double)` | outpost 目标注入不同角噪声 | 同 Card 0-1 测试运行 | SP 普通/前哨站噪声分支值口径 | 噪声参数来自注入字段而非硬编码；不同噪声导致协方差增长差异可观测；测试通过 | `mv-ekf-track-target-update-test` |
| 0-5 | `src/modules/ekf_predictor/detail/ekf_tracker.cpp::SetTarget` | 全局 P0 已配置 + outpost 检测输入 | 1) `cmake --build build --target mv-ekf-tracker-set-target-test -j$(nproc)` 2) `./build/src/test/mv-ekf-tracker-set-target-test` | SP `set_target` 的分支优先级（balance/outpost/base/default） | outpost 分支不被全局 P0 覆盖；`P(8,8)` 命中 outpost 预期值；测试通过 | `mv-ekf-tracker-set-target-test` |
| 0-6 | `src/modules/ekf_predictor/ekf_predictor.cpp::Init` | 传入 ROOT_CFG，设置 `auto_aim.ekf_predictor.init_radius_outpost` | 1) `cmake --build build --target mv-ekf-predictor-init-config-test -j$(nproc)` 2) `./build/src/test/mv-ekf-predictor-init-config-test` | SP 配置映射到 tracker/target 的有效透传语义 | 根路径配置可命中；半径透传生效；测试输出 `[PASS] ekf_predictor_init_config_test` | `mv-ekf-predictor-init-config-test` |
| 0-7 | `src/modules/ekf_predictor/detail/trajectory_solver.cpp::ChooseAimPoint` | 双候选近角切换序列 | 1) `cmake --build build --target mv-trajectory-solver-choose-aim-point-test -j$(nproc)` 2) `./build/src/test/mv-trajectory-solver-choose-aim-point-test` | SP `aimer::choose_aim_point` 的 lock_id 防抖语义 | 双候选窗口内保持锁定，不出现帧间翻转；测试通过 | `mv-trajectory-solver-choose-aim-point-test` |
| 0-8 | `src/modules/ekf_predictor/detail/trajectory_solver.cpp::Solve` | future-based 迭代时序 + 可解弹道 | 1) `cmake --build build --target mv-trajectory-solver-solve-iter-test -j$(nproc)` 2) `./build/src/test/mv-trajectory-solver-solve-iter-test` | SP `aimer::aim` 的 future+iter 收敛时序 | 收敛语义正确；tracking 有效时 `fire=false` 边界不变；测试通过 | `mv-trajectory-solver-solve-iter-test` |
| 0-9 | `src/modules/ekf_predictor/detail/ekf_tracker.cpp::Track/StateMachine` | 大 dt、outpost TEMP_LOST、同号异型检测 | 1) `cmake --build build --target mv-ekf-tracker-state-machine-test -j$(nproc)` 2) `./build/src/test/mv-ekf-tracker-state-machine-test` | SP `tracker::track/state_machine` 的状态迁移语义 | 大 dt 直接 reset；outpost 容忍上限高于普通目标；同号异型不更新；测试通过 | `mv-ekf-tracker-state-machine-test` |
| 0-10 | `src/modules/ekf_predictor/detail/ekf_track_target.hpp`、`src/modules/ekf_predictor/detail/trajectory_solver.hpp` | 头文件注释与实现口径一致性检查 | 1) 人工审查注释块 2) 可选 `rg "xyz_in_world|recent_nis_failures|云台当前 yaw 最近" src/modules/ekf_predictor/detail/*.hpp` | SP 对齐后的语义说明应可独立指导调用 | 注释不再残留旧口径（xyz 观测等）；锁定策略、失败语义与实现一致 | 头文件审查记录 |
| 0-11 | `configs/vision.yaml`、`src/config/debug/debug_override.yaml` | 分组/平铺键并存，P0 新旧键并存 | 1) YAML 加载校验（`python3 -c` + `yaml.safe_load`） 2) 运行 `mv-ekf-predictor-init-config-test` | SP 目标是“按类型读到有效参数”，并允许过渡期兼容 | YAML 可加载；`p0_diag.default` 优先、`P0_diag` 回退；init 回归通过 | YAML 解析输出 + `mv-ekf-predictor-init-config-test` |

## 4. 失败场景与退化行为矩阵

| 失败场景 | 触发方式 | 预期退化行为 | 检查点 |
|---|---|---|---|
| 无候选装甲板 | `ChooseAimPoint` 候选过滤后为空 | 返回 `valid=false`，上层 `Solve` 返回 `tracking=false` | 日志含 `No valid aim point` 语义 |
| 初始弹道不可解 | `SolveTrajectory` 判别式 < 0（initial） | 直接返回 `tracking=false`，不输出 fire | 日志含 `Unsolvable initial trajectory` |
| 迭代中弹道不可解 | iter 过程中 `SolveTrajectory` 不可解 | 返回 `tracking=false`，停止后续迭代 | 日志含 `Unsolvable at iter` |
| 配置缺键 | 删除部分 `auto_aim.ekf_predictor` 键 | 走默认值回退，不崩溃 | Init 日志输出默认摘要 |
| P0 新键缺失 | 缺失 `p0_diag.default` 但保留 `P0_diag` | 回退读取 `P0_diag` | Init 结果可用，回归通过 |
| P0 维度错误 | `p0_diag.default` 或 `P0_diag` 非 11 维 | 跳过错误向量，保持默认或分支值，不崩溃 | 运行无崩溃，行为可复现 |
| 跟踪时间跨度异常 | `dt > max_dt_sec` | 目标重置为 LOST，返回空跟踪 | `mv-ekf-tracker-state-machine-test` 通过 |

## 5. 最小回归执行集

建议每次合并前至少执行以下命令：

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

## 6. 验收判定

- Card 0-1 到 0-11 每项均有可执行验证入口。
- 每项均同时包含 SP 对照项与 EX 断言项。
- 失败场景具备可预期退化行为与检查点。
- 文档内无 TBD/空条目，可直接作为回归执行清单。
