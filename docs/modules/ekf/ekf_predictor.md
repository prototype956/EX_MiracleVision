# EKF Predictor 模块说明

## 1. 模块位置
- 入口：`src/modules/ekf_predictor/ekf_predictor.cpp`
- 跟踪器：`src/modules/ekf_predictor/detail/ekf_tracker.cpp`
- 目标状态：`src/modules/ekf_predictor/detail/ekf_track_target.cpp`

## 2. Card 0-5 行为对齐（EX -> SP）

### 2.1 变更点
在 `EkfTracker::SetTarget` 中，初始协方差 `P0_diag` 的分支优先级调整为：
1. `balance` 分支默认值
2. `outpost` 分支默认值
3. `base` 分支默认值
4. 普通目标时，若存在 `params.P0_diag(11)` 则使用全局配置
5. 否则使用普通目标默认值

### 2.2 设计意图
- 保留特殊目标（`balance/outpost/base`）的专用初始化语义。
- 避免全局 `P0_diag` 无差别覆盖特殊分支，导致目标类别先验被抹平。
- 与 `sp_vision_25` 的分支语义保持一致。

### 2.3 验证方式
- 新增测试：`src/test/ekf/ekf_tracker_set_target_test.cpp`
- 测试目标：`mv-ekf-tracker-set-target-test`
- 关键断言：当 `params.P0_diag` 已配置时，`OUTPOST` 目标的 `P(8,8)` 仍应使用 outpost 分支默认值（`1e-4`）。

## 3. 兼容性说明
- 本次仅调整内部初始化优先级，不修改公共接口。
- 普通目标仍可通过 `params.P0_diag` 覆盖默认值。

## 4. Card 0-6 行为对齐（Init 配置读取与透传）

### 4.1 变更点
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

### 4.2 设计意图
- 修复“传入根配置时未命中 `auto_aim.ekf_predictor`”导致的大量参数静默回退默认值问题。
- 保持向后兼容，避免历史配置文件因键名迁移直接失效。
- 初始化日志增加关键摘要，便于运行期确认参数是否生效。

### 4.3 验证方式
- 新增测试：`src/test/ekf/ekf_predictor_init_config_test.cpp`
- 测试目标：`mv-ekf-predictor-init-config-test`
- 关键断言：当传入 ROOT_CFG 且设置 `auto_aim.ekf_predictor.init_radius_outpost=0.58` 时，
	第一次预测输出的 `TrackTarget.armor_positions` 能反推出半径为 `0.58`（说明参数已透传到 tracker/target）。

### 4.4 兼容性说明
- 不修改 `IPredictor` 公共接口。
- 缺字段时保持安全回退，不因缺配置崩溃。

## 5. Card 0-11 行为对齐（配置键对齐）

### 5.1 变更点
本轮将以下配置文件中的 `auto_aim.ekf_predictor` 键集合与 `EkfPredictor::Init` 映射口径对齐：
1. `configs/vision.yaml`
2. `src/config/debug/debug_override.yaml`

对齐内容包含：
- 过程噪声分组键：`process_noise.normal.{pos,ang}`、`process_noise.outpost.{pos,ang}`。
- 过程噪声平铺兼容键：`process_noise_pos`、`process_noise_ang`、`process_noise_outpost_pos`、`process_noise_outpost_ang`。
- P0 新键：`p0_diag.default`（11 维）。
- P0 旧键兼容：`P0_diag`（当 `p0_diag.default` 缺失时回退）。
- 轨迹参数补齐：`max_iter`、`iter_converge_ms`、`max_approaching_angle`、`max_leaving_angle`。

### 5.2 设计意图
- 保证配置键与 `Init` 实际读取逻辑一一对应，避免“代码支持但配置缺失”导致隐式回退。
- 在引入分组键后继续保留平铺键，确保历史配置文件可继续运行。
- 将单位语义（`ms/s`、`rad`、11 维向量）直接写入配置旁注，降低误配风险。

### 5.3 验证方式
- YAML 语法加载验证：`configs/vision.yaml` 与 `src/config/debug/debug_override.yaml` 均可被 YAML 解析器成功加载。
- 回归测试：`mv-ekf-predictor-init-config-test` 构建并运行通过。

### 5.4 兼容性说明
- 不修改 `IPredictor` 公共接口。
- 配置读取优先级保持：分组键优先，平铺键回退；`p0_diag.default` 优先，`P0_diag` 回退。

### 5.5 P0 旧键兼容来源与退役条件

#### 5.5.1 兼容来源
- 仓库内历史配置样式：`src/config/debug/debug_override.yaml` 与旧版 `auto_aim.ekf_predictor` 写法均使用 `P0_diag`。
- 仓库外部署配置：现场设备或外部团队可能仍沿用 `P0_diag`，无法由仓库内代码自动发现。

#### 5.5.2 当前代码读取顺序
- `EkfPredictor::Init` 读取顺序为：
	1. `p0_diag.default`（新键，优先）
	2. `P0_diag`（旧键，回退）
- 代码入口：`src/modules/ekf_predictor/ekf_predictor.cpp`。

#### 5.5.3 删除 `P0_diag` 的前置条件
- 仓库内配置迁移完成：`configs/vision.yaml`、`src/config/debug/debug_override.yaml` 及其他 `ekf_predictor` 配置均切到 `p0_diag.default`。
- 外部部署迁移完成：确认发布环境配置不再依赖 `P0_diag`。
- 回归验证通过：`mv-ekf-predictor-init-config-test` 与最小回归集通过。
- 文档同步完成：配置示例和模块文档移除旧键示例及兼容说明。

#### 5.5.4 建议退役流程
- 版本 N：保留回退逻辑，但在命中 `P0_diag` 时输出 deprecate 提示。
- 版本 N+1（至少一个版本周期后）：移除 `P0_diag` 回退分支与示例键。
- 退役发布时：在变更说明中标注迁移路径（`P0_diag` -> `p0_diag.default`）。
