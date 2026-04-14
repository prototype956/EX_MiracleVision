# EKF Predictor 模块工作原理（面向新成员）

本文只回答三个问题：这个模块在系统中做什么、每一帧如何流动、EKF 数学在代码中落在哪些位置。

## 1. 模块在系统中的角色

EX 项目将自动瞄准链路拆成三层职责：`IPredictor` 负责“预测目标下一时刻状态并给出瞄准角”，`IVoter` 负责“是否允许开火”，`IShooter` 负责“把控制量编码并发送到下位机”。这样拆分的收益是，算法策略、开火策略、通信协议可以独立演进，不会相互拖拽。

从流水线角度看，`PredictNode` 把检测结果交给 `EkfPredictor`，随后再调用 `Voter` 设置 `fire`，最终由 `SerialNode` 调用 `Shooter` 发包。也就是说，预测模块并不直接开火，它只提供“是否在稳定跟踪、当前应指向哪里”的依据。

## 2. 一帧数据是怎么跑完的

先从检测结果出发。检测器给出装甲板观测（位置、类别、颜色等），`PredictNode` 会先注入当前云台姿态四元数，然后进入 `EkfPredictor::Predict`。

`EkfPredictor` 内部先做坐标变换（云台系到世界系），再交给 `EkfTracker` 执行状态机：在 `LOST/DETECTING/TRACKING/TEMP_LOST` 之间切换，并决定是“初始化目标”还是“更新目标”。目标对象由 `EkfTrackTarget` 持有 EKF 状态，完成预测和观测更新。

当状态可用时，`TrajectorySolver` 根据目标状态、子弹速度和延迟补偿给出 `yaw/pitch`，形成 `GimbalControl`。这份控制量再由 `Voter` 判断 `fire`，最终交给 `Shooter` 下发。

## 3. 为什么一定要先做坐标变换

检测坐标默认在云台坐标系中。如果不转换到稳定参考系，云台自身旋转会被滤波器误认为目标在快速移动，从而造成速度项、角速度项漂移，表现为抖动、误切目标、发散概率上升。

所以 `EkfPredictor` 在进入 tracker 前先做 `R_gimbal2world` 旋转。这个步骤不是“可选优化”，而是让运动模型成立的前提。

## 4. Tracker 状态机在解决什么问题

很多新人会把 tracker 理解成“找到目标就更新，找不到就清空”的 if-else。实际不是这样。状态机的价值是把“短暂误检、瞬时丢帧、真实丢失”区分开，避免系统在边缘条件下反复抖动。

`DETECTING` 用来防止单帧误检直接进入稳定跟踪，`TEMP_LOST` 用来容忍短时间遮挡或识别间歇，超过阈值才回到 `LOST`。同时，较大 `dt` 会触发保护性重置，避免相机异常停顿后沿用过期状态。

## 5. EKF 在这里到底算了什么

### 5.1 状态向量（11 维）

`EkfTrackTarget` 使用 11 维状态近似目标运动：

- 旋转中心位置与速度：`cx, dcx, cy, dcy, cz, dcz`
- 装甲板角度与角速度：`yaw, vyaw`
- 几何参数：`r, delta_r, delta_h`

直观上可以理解为：滤波器同时估计“目标中心怎么动”和“装甲板绕中心怎么转”。

### 5.2 预测步骤

预测阶段使用常速模型（短时间近似匀速）推进状态，并叠加过程噪声：

$$
\hat{x}^{-}=f(\hat{x}), \quad P^{-}=FPF^T+Q
$$

其中 `F` 是状态转移雅可比，`Q` 表示对“模型不完美”的容忍度。参数越大，滤波器越相信新观测；参数越小，越相信历史状态。

### 5.3 更新步骤

更新阶段把预测状态投影到观测空间，与真实观测做对比修正：

$$
K=P^{-}H^T(HP^{-}H^T+R)^{-1}
$$

$$
\hat{x}=\hat{x}^{-}+K\bigl(z-h(\hat{x}^{-})\bigr)
$$

`h(x)` 是非线性观测函数，`H` 是其雅可比。这里雅可比的作用不是“算下一时刻”，而是“在当前估计点做局部线性化”，让线性代数更新可用。

### 5.4 角度归一化

角度变量跨过 $\pm\pi$ 边界时，如果不做归一化，数值上会出现“接近但看起来很远”的假误差。代码中对状态和残差都做了 `limit_rad`，用于避免创新量异常放大。

## 6. 轨迹解算与开火边界

`TrajectorySolver` 的职责是把“目标状态”转换成“可执行瞄准角”，包括延迟补偿和飞行时间迭代。它解决的是“怎么打得准”，不是“该不该开火”。

`Voter` 负责“该不该开火”，`Shooter` 负责“如何发送控制协议”。这条边界在 EX 中是显式接口约束，便于单独替换策略。

## 7. 概念到源码的映射

| 概念 | 主要文件 | 作用 |
| --- | --- | --- |
| 预测节点编排 | `src/pipeline/predict_node.cpp` | 调 predictor、voter 并组装输出包 |
| Predictor 接口 | `src/interfaces/i_predictor.hpp` | 统一预测模块输入输出契约 |
| EKF predictor 入口 | `src/modules/ekf_predictor/ekf_predictor.cpp` | 坐标变换 + tracker/solver 调度 |
| 跟踪状态机 | `src/modules/ekf_predictor/detail/ekf_tracker.cpp` | LOST/DETECTING/TRACKING/TEMP_LOST 迁移 |
| EKF 目标模型 | `src/modules/ekf_predictor/detail/ekf_track_target.cpp` | 状态定义、F/Q、h/H、更新 |
| 轨迹求解 | `src/modules/ekf_predictor/detail/trajectory_solver.cpp` | 选瞄准点与弹道迭代 |
| Voter 接口 | `src/interfaces/i_voter.hpp` | 开火决策契约 |
| Shooter 接口/实现 | `src/interfaces/i_shooter.hpp`, `src/modules/rm_shooter/*` | 控制帧编码与发送 |

## 8. EX 与 SP 的最小对照（只保留理解必需项）

两套工程的 EKF 核心思想一致，都是“目标状态估计 + 观测校正 + 轨迹补偿”。最大差异在工程分层：SP 更容易在模块内部串联决策，EX 将 predictor、voter、shooter 明确解耦并挂在统一接口下，便于替换实现。

因此从 SP 迁移到 EX 时，最重要的心智切换是：预测模块只输出“跟踪状态和瞄准建议”，开火语义在 voter，通信语义在 shooter。

## 9. 常见误解

误解一：雅可比就是“下一时刻雅可比”。
修正：雅可比是当前线性化点的局部导数，用于本次更新，不是未来真值。

误解二：tracker 只是简单筛选器。
修正：tracker 是时间序列上的稳定器，负责抗抖和状态生存期管理。

误解三：调大噪声一定更稳。
修正：噪声参数是“信历史 vs 信观测”的平衡旋钮，过大过小都可能导致抖动或迟钝。

## 10. 快速排障索引

如果现象是“总丢跟踪”，先看状态机阈值和 `dt` 重置条件。

如果现象是“角度跳变或抖动”，先看角度归一化与观测噪声配置。

如果现象是“预测点稳定但开火异常”，优先排查 voter 条件链与 shooter 下发链路，而不是先改 EKF。

## 11. 建议阅读顺序

先读第 2 节（数据流）建立全局图，再读第 4 节（状态机）理解为什么要多状态，再读第 5 节（EKF 数学）对齐公式与代码，最后按第 10 节做定位排障。

## 12. 扩展专题文档

根据学习目标和工作场景，我们提供三份专题深度指南：

### 题 A：EKF 数学与工程直觉详解

**适合对象**：想理解公式推导、非线性观测模型、角度归一化技巧的读者

**文档链接**：[ekf_detailed_mathematics.md](ekf_detailed_mathematics.md)

**核心内容**：
- 11 维状态向量的物理含义（位置、速度、角度、几何参数）
- 预测步骤的 F 矩阵与 Q 过程噪声
- 观测函数 h 与雅可比 H 的计算
- 角度 ±π 边界处理（一个常见陷阱）
- NIS 一致性检测与发散识别
- 5 个常见误解答疑

**阅读耗时**：40-60 分钟

---

### 题 B：EKF 与轨迹关键参数调参手册

**适合对象**：现场调试工程师、想快速找到参数问题根源的读者

**文档链接**：[ekf_tuning_guide.md](ekf_tuning_guide.md)

**核心内容**：
- 40+ 键名 → 代码字段 → 工作点的完整映射表
- 症状-参数对应表（总掉线、抖动、打点偏差 → 改什么参数）
- 三阶段调参顺序：追踪稳定 → EKF 收敛 → 弹道补偿
- 快速诊断决策树（按现象定位问题）
- 禁忌区清单（不建议首先改的参数）
- 验收与回归检查清单

**阅读耗时**：30-45 分钟（视熟悉度）

**推荐场景**：系统丢跟、打点偏差、新赛场快速适应

---

### 题 C：坐标转换与安装误差调参实操指南

**适合对象**：硬件集成工程师、现场安装调试人员、对坐标系统感到困惑的读者

**文档链接**：[coordinate_transformation_tuning.md](coordinate_transformation_tuning.md)

**核心内容**：
- 四级链路的完整讲解（像素 → 相机系 → 云台系 → 世界系）
- 五类安装误差类型 × 现场表现 × 排查方法
- 分阶段操作流程（四元数验证 → 手眼标定 → 时序同步 → 端到端验证）
- 标定工具使用（mv-calib-capture、mv-calib-rwhandeye、mv-calib-validate）
- 重投影误差、四元数范数、NIS 等多个可观测指标
- 现场排错快速清单与实操顺序（4-5 小时）

**阅读耗时**：50-70 分钟

**推荐场景**：新设备首次集成、坐标错乱、重投影误差大、跟踪点跳变

---

### 三份文档之间的关系

```
本文档 (ekf_predictor.md)
  ├─ 第 5 节 EKF 数学 (概览)
  │    └─ 题 A (ekf_detailed_mathematics.md) - 深入推导
  │         └─ 用于理解题 B、题 C 中的参数语义
  │
  ├─ 第 7 节 轨迹求解 (概览)
  │    └─ 题 B (ekf_tuning_guide.md) - 参数调参
  │         └─ 用于现场快速定位与修改
  │
  ├─ 第 3 节 坐标变换 (简述)
  │    └─ 题 C (coordinate_transformation_tuning.md) - 实操步骤
  │         └─ 用于安装后的验证与微调
  │
  └─ 第 10 节 快速排障 (索引)
       ├─ 追踪问题 → 题 B 中度2
       ├─ 打点偏离 → 题 B 中度4 + 题 C 中度4
       └─ 坐标跳变 → 题 C 中度3
```

**选择阅读策略**：

| 我想了解... | 推荐 |
|-----------|------|
| 从零理解整个模块 | 先本文 11 节 → 题 A（数学） → 题 B（参数） |
| 快速解决打点偏离 | 直接题 B §5 决策树 |
| 新设备集成指导 | 题 C §5 标准实操流程 |
| 现场掉线排障 | 题 B §5 决策树 |
| 理解为什么这样设计 | 题 A + 本文附录（历史对齐） |

---

## 附录：历史行为对齐记录

以下内容保留用于追踪此前 EX 与 SP 的行为对齐过程。

### A. Card 0-5 行为对齐（EX -> SP）

#### A.1 变更点
在 `EkfTracker::SetTarget` 中，初始协方差 `P0_diag` 的分支优先级调整为：
1. `balance` 分支默认值
2. `outpost` 分支默认值
3. `base` 分支默认值
4. 普通目标时，若存在 `params.P0_diag(11)` 则使用全局配置
5. 否则使用普通目标默认值

#### A.2 设计意图
- 保留特殊目标（`balance/outpost/base`）的专用初始化语义。
- 避免全局 `P0_diag` 无差别覆盖特殊分支，导致目标类别先验被抹平。
- 与 `sp_vision_25` 的分支语义保持一致。

#### A.3 验证方式
- 新增测试：`src/test/ekf/ekf_tracker_set_target_test.cpp`
- 测试目标：`mv-ekf-tracker-set-target-test`
- 关键断言：当 `params.P0_diag` 已配置时，`OUTPOST` 目标的 `P(8,8)` 仍应使用 outpost 分支默认值（`1e-4`）。

#### A.4 兼容性说明
- 本次仅调整内部初始化优先级，不修改公共接口。
- 普通目标仍可通过 `params.P0_diag` 覆盖默认值。

### B. Card 0-6 行为对齐（Init 配置读取与透传）

#### B.1 变更点
在 `EkfPredictor::Init` 中，配置读取节点改为按以下顺序解析：
1. `root.auto_aim.ekf_predictor`（主流程 `main` 传入 ROOT_CFG 时命中）
2. `auto_aim.ekf_predictor`（传入 `auto_aim` 子树时命中）
3. `ekf_predictor`（直接传入 predictor 子树时命中）

并补充了两类参数读取兼容：
- 旧平铺键：`process_noise_pos` / `process_noise_ang` / `process_noise_outpost_pos` / `process_noise_outpost_ang`
- 新分组键：`process_noise.normal.{pos,ang}` 与 `process_noise.outpost.{pos,ang}`

`P0` 读取兼容：
- `p0_diag.default`（优先）
- `P0_diag`（回退）

#### B.2 设计意图
- 修复“传入根配置时未命中 `auto_aim.ekf_predictor`”导致的大量参数静默回退默认值问题。
- 保持向后兼容，避免历史配置文件因键名迁移直接失效。
- 初始化日志增加关键摘要，便于运行期确认参数是否生效。

#### B.3 验证方式
- 新增测试：`src/test/ekf/ekf_predictor_init_config_test.cpp`
- 测试目标：`mv-ekf-predictor-init-config-test`
- 关键断言：当传入 ROOT_CFG 且设置 `auto_aim.ekf_predictor.init_radius_outpost=0.58` 时，
  第一次预测输出的 `TrackTarget.armor_positions` 能反推出半径为 `0.58`（说明参数已透传到 tracker/target）。

#### B.4 兼容性说明
- 不修改 `IPredictor` 公共接口。
- 缺字段时保持安全回退，不因缺配置崩溃。

### C. Card 0-11 行为对齐（配置键对齐）

#### C.1 变更点
本轮将以下配置文件中的 `auto_aim.ekf_predictor` 键集合与 `EkfPredictor::Init` 映射口径对齐：
1. `configs/vision.yaml`
2. `src/config/debug/debug_override.yaml`

对齐内容包含：
- 过程噪声分组键：`process_noise.normal.{pos,ang}`、`process_noise.outpost.{pos,ang}`。
- 过程噪声平铺兼容键：`process_noise_pos`、`process_noise_ang`、`process_noise_outpost_pos`、`process_noise_outpost_ang`。
- P0 新键：`p0_diag.default`（11 维）。
- P0 旧键兼容：`P0_diag`（当 `p0_diag.default` 缺失时回退）。
- 轨迹参数补齐：`max_iter`、`iter_converge_ms`、`max_approaching_angle`、`max_leaving_angle`。

#### C.2 设计意图
- 保证配置键与 `Init` 实际读取逻辑一一对应，避免“代码支持但配置缺失”导致隐式回退。
- 在引入分组键后继续保留平铺键，确保历史配置文件可继续运行。
- 将单位语义（`ms/s`、`rad`、11 维向量）直接写入配置旁注，降低误配风险。

#### C.3 验证方式
- YAML 语法加载验证：`configs/vision.yaml` 与 `src/config/debug/debug_override.yaml` 均可被 YAML 解析器成功加载。
- 回归测试：`mv-ekf-predictor-init-config-test` 构建并运行通过。

#### C.4 兼容性说明
- 不修改 `IPredictor` 公共接口。
- 配置读取优先级保持：分组键优先，平铺键回退；`p0_diag.default` 优先，`P0_diag` 回退。

#### C.5 P0 旧键兼容来源与退役条件

##### C.5.1 兼容来源
- 仓库内历史配置样式：`src/config/debug/debug_override.yaml` 与旧版 `auto_aim.ekf_predictor` 写法均使用 `P0_diag`。
- 仓库外部署配置：现场设备或外部团队可能仍沿用 `P0_diag`，无法由仓库内代码自动发现。

##### C.5.2 当前代码读取顺序
- `EkfPredictor::Init` 读取顺序为：
  1. `p0_diag.default`（新键，优先）
  2. `P0_diag`（旧键，回退）
- 代码入口：`src/modules/ekf_predictor/ekf_predictor.cpp`。

##### C.5.3 删除 `P0_diag` 的前置条件
- 仓库内配置迁移完成：`configs/vision.yaml`、`src/config/debug/debug_override.yaml` 及其他 `ekf_predictor` 配置均切到 `p0_diag.default`。
- 外部部署迁移完成：确认发布环境配置不再依赖 `P0_diag`。
- 回归验证通过：`mv-ekf-predictor-init-config-test` 与最小回归集通过。
- 文档同步完成：配置示例和模块文档移除旧键示例及兼容说明。

##### C.5.4 建议退役流程
- 版本 N：保留回退逻辑，但在命中 `P0_diag` 时输出 deprecate 提示。
- 版本 N+1（至少一个版本周期后）：移除 `P0_diag` 回退分支与示例键。
- 退役发布时：在变更说明中标注迁移路径（`P0_diag` -> `p0_diag.default`）。
