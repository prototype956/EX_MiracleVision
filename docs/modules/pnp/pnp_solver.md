# PnpSolver 模块说明

> 模块路径：src/modules/pnp_solver/
> 工厂键："pnp"
> 主要文件：pnp_solver.hpp / pnp_solver.cpp

## 1. 模块职责

PnpSolver 负责将装甲板 2D 角点观测转换为 3D 位姿估计：
- 输入：角点像素坐标（px）、装甲类型（SMALL/BIG）。
- 输出：云台坐标系位置 xyz（m）、yaw/pitch（rad）、重投影误差（px）。

模块边界：
- 负责：PnP 求解、外参变换、重投影误差评估。
- 不负责：目标检测、轨迹预测、射击决策。

## 2. 模块映射与调用链

实现映射：
- 接口：src/interfaces/i_solver.hpp -> ISolver
- 实现：src/modules/pnp_solver/pnp_solver.hpp/.cpp -> PnpSolver
- 装配入口：src/app/main.cpp

运行调用链：
1. main 根据配置创建并初始化 solver（工厂键 "pnp"）。
2. pipeline 调用 ISolver::Solve(Detection&)。
3. PnpSolver 执行单解 IPPE 解算并回写 detection 字段。

## 3. 坐标系与单位约定

统一单位：
- 三维坐标：m
- 角度：rad
- 像素：px

坐标系：
- 相机坐标系：right +X, down +Y, forward +Z。
- 云台坐标系：通过 R_camera_to_gimbal 与 t_camera_to_gimbal 从相机系变换得到。
- 世界坐标系：由上层姿态源（如 IMU）更新 R_gimbal2world / t_gimbal2world 后使用。

装甲世界模板（以装甲中心为原点，Z=0）：
- SMALL：宽 0.135，高 0.055。
- BIG：宽 0.230，高 0.055。
- 角点顺序：BL / BR / TR / TL。

## 4. 核心算法

### 4.1 PnP 模型

给定模板点 X_i 与像素点 u_i，求解位姿 (R, t)：

\f[
 s\,\mathbf{u}_i = \mathbf{K}(\mathbf{R}\mathbf{X}_i + \mathbf{t})
\f]

其中：
- K：相机内参矩阵（px）。
- X_i：世界模板点（m）。
- R, t：相机坐标系下位姿。

### 4.2 单解策略（solvePnP + IPPE）

当前实现采用 `solvePnP(..., SOLVEPNP_IPPE)` 单解路径：
1. 直接返回一组 `rvec/tvec`。
2. 基于 `tvec` 进行相机系到云台系变换。
3. 计算 `yaw/pitch` 与主解重投影误差。

兼容性约定：
- `has_alt_solution` 在单解策略下固定为 `false`。
- `xyz_in_gimbal_alt / reprojected_points_alt / reproj_error_alt` 每帧复位，避免沿用旧帧残留值。

### 4.3 重投影误差

RMS 误差定义：

\f[
 e_{rms}=\sqrt{\frac{1}{4}\sum_{i=1}^{4}\|\mathbf{u}_i-\hat{\mathbf{u}}_i\|^2}
\f]

可选 SJTUCost（像素差 + 边方向差）用于 yaw/pitch 评估。

## 5. 配置项（vision.yaml）

位于 calibration 节点：

```yaml
calibration:
  camera_matrix: [fx, 0, cx, 0, fy, cy, 0, 0, 1]
  distort_coeffs: [k1, k2, p1, p2, k3]

  # 可选：相机 -> 云台外参
  R_camera_to_gimbal: [r00, r01, r02, r10, r11, r12, r20, r21, r22]
  t_camera_to_gimbal: [tx, ty, tz]

  # 可选：云台 -> imubody
  R_gimbal_to_imubody: [r00, r01, r02, r10, r11, r12, r20, r21, r22]
```

装甲尺寸可由 armor 节点覆盖：
- small_half_w, big_half_w, half_h。

## 6. 关键接口

- Init(config)：加载内参与可选外参，成功后进入 initialized 状态。
- Solve(detection)：执行单解 IPPE，回写 xyz/yaw/pitch 与误差字段。
- OptimizeYaw(detection, yaw_min, yaw_max, step)：在区间内遍历 yaw，最小化重投影误差。
- ArmorReprojectionError(...)：计算给定 yaw/pitch 的重投影误差。
- WorldToPixel(...)：将世界点集合投影到像素平面。

## 7. 失败与边界条件

常见失败返回：
- Init 失败：缺失 calibration 或 camera_matrix 维度非法。
- Solve 失败：未初始化或 solvePnP 返回 false。
- 重投影失败：点在相机后方或投影结果数量异常，返回 inf 或空结果。

边界约束：
- 本模块默认非线程安全；同一实例应在单线程顺序调用 Init/Solve。
- 多实例可并行，实例间状态互不共享。

## 8. 调试建议

优先排查顺序：
1. 检查 camera_matrix / distort_coeffs 维度与数值范围。
2. 检查角点顺序是否严格为 BL/BR/TR/TL。
3. 检查外参与坐标系方向是否与实际安装一致。
4. 观察主解重投影误差与图像叠加偏差，确认是否进入告警阈值。

## 9. Foxglove 可视化规范

PnP 在 Foxglove 的可视化规范位于：
- `docs/modules/foxglove/pnp/panel-mapping-v1.md`
- `docs/modules/foxglove/pnp/single-tab-layout-v1.1.md`
- `docs/modules/foxglove/pnp/single-tab-panel-spec-v1.2.md`

## 10. 迁移说明

历史分析与重构过程文档位于 docs/refractor/modules/。
本文件作为当前模块行为与接口约束的权威说明，后续行为变化应同步更新本文件。

## 11. 单测使用

`PnpSolver` 最小单测的构建与运行说明见：
- `docs/test/pnp_solver_test_usage.md`
