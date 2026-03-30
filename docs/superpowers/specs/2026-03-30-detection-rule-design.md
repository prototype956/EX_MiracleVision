# Detection 模块新规则落地设计（注释 / 文档 / 单测）

## 1. 背景与目标

本设计用于将 `.github/instructions` 中与 detection 相关的规则落地为可执行工件，
覆盖三条线：

1. 接口与实现注释规范化；
2. 模块文档同步化；
3. 契约单元测试可验证化。

本次目标是合规与可追溯，不做算法升级。

## 2. 范围与边界

包含范围：

- `src/interfaces/i_detector.hpp`
- `src/interfaces/types.hpp`
- `src/modules/armor_detector/`
- `src/test/armor_detector_contract_test.cpp`
- `src/test/CMakeLists.txt`
- `docs/modules/detector/armor_detector.md`
- `docs/test/armor_detector_contract_test.md`
- `docs/superpowers/specs/2026-03-30-detection-rule-design.md`

排除范围：

- 旧目录 `base/`、`module/`、`devices/`
- detection 之外模块行为改动
- 公共接口签名变更
- 检测算法策略升级

## 3. 调用链基线

`RoiManager::Crop -> IDetector::Detect -> RoiManager::RestoreAndUpdate -> ISolver::Solve`

约束：

- detector 仅处理输入帧坐标系；
- ROI 管理层负责局部/全图坐标切换；
- 坐标恢复逻辑不下沉到 detector。

## 4. 规则映射（R1-R5）

### R1 架构边界

- 规则：新实现放在 `src/`，不扩写旧目录。
- 落地：仅修改 `src/interfaces/`、`src/modules/armor_detector/`、`src/test/` 与 docs 分区。

### R2 接口契约

- 规则：`Init()` 失败返回 false；`Detect()` 无目标返回空容器；线程安全显式声明。
- 落地：在 `i_detector.hpp` 与 `types.hpp` 中补充契约注释。

### R3 协同调试小步改动

- 规则：按调用链说明先行，小步可回滚。
- 落地：本次实现按“接口注释 -> ROI 微调 -> 测试 -> 文档”推进。

### R4 质量门禁

- 规则：至少覆盖正常路径 + 边界/失败路径。
- 落地：新增 `mv-armor-detector-contract-test`，覆盖空帧、角点顺序、ROI 恢复、丢失回退。

### R5 文档同步与 PR 证据

- 规则：代码改动必须同步文档，并在 PR 提供映射。
- 落地：新增模块文档与测试文档，补充 CMake 快速参考条目。

## 5. 已确认结论

### 5.1 ROI 管理层职责

- 仅负责：裁剪输入视图、坐标恢复、ROI 状态更新；
- 不负责：检测算法、PnP 求解、预测决策。

### 5.2 角点顺序契约

- detector 与 pnp 统一使用 `BL, BR, TR, TL`；
- 顺序变更属于跨模块行为变更，必须同步更新 detector/pnp/test/docs。

## 6. 测试矩阵

新增测试文件：`src/test/armor_detector_contract_test.cpp`

| 用例 | 覆盖规则 | 目的 |
| --- | --- | --- |
| `TestDetectEmptyFrame` | R2 | 验证空帧输入返回空容器 |
| `TestDetectCornerOrderAndColor` | R2, R4 | 验证角点顺序契约与颜色过滤语义 |
| `TestRoiRestoreAndDistanceCenter` | R1, R2 | 验证 ROI 坐标恢复与全图中心距离计算 |
| `TestRoiLostFallback` | R1, R4 | 验证连续丢失回退到全图 |

## 7. 实施结果（本次）

### 7.1 接口与注释

- `i_detector.hpp`：补充线程安全与 Detect 坐标/角点契约注释。
- `types.hpp`：固定 `Detection::points` 顺序说明，明确 ROI/全图坐标语义。

### 7.2 ROI 行为一致性

- `roi_manager.hpp`：`RestoreAndUpdate()` 使用 `frame_size` 动态计算全图中心，
  不再依赖硬编码分辨率。

### 7.3 测试接入

- 新增可执行 `mv-armor-detector-contract-test` 并接入 `src/test/CMakeLists.txt`。

## 8. 验证记录

执行命令：

1. `cmake -S . -B build -DUSE_MINDVISION_SDK=OFF`
2. `cmake --build build --target mv-armor-detector-contract-test -j$(nproc)`
3. `./build/src/test/mv-armor-detector-contract-test`

结果：

- 构建通过；
- 新测试通过（输出 `[PASS] armor_detector_contract_test`）。

备注：worktree 环境下缺少 MindVision SDK 动态库，故本次验证采用 `USE_MINDVISION_SDK=OFF`。

## 9. 风险与回退

风险：

- 合成图像测试对参数敏感，若后续阈值变化可能需同步调整测试样本。

回退策略：

- 每阶段独立提交，若出现行为回归，优先回退最近单函数改动。

## 10. 非目标

- 不引入新的 detector 算法实现；
- 不变更 IDetector 公共签名；
- 不改旧目录实现。

## 11. 代码改动 -> 文档改动映射（PR 用）

1. 接口契约注释
- 代码：`src/interfaces/i_detector.hpp`、`src/interfaces/types.hpp`
- 文档：`docs/modules/detector/armor_detector.md`
- 变更点：Init/Detect 失败语义、线程安全、角点顺序与坐标语义。

2. ROI 行为一致性
- 代码：`src/modules/armor_detector/roi_manager.hpp`
- 文档：`docs/modules/detector/armor_detector.md`
- 变更点：RestoreAndUpdate 使用 frame_size 计算全图中心，统一 distance_to_center 语义。

3. 契约测试
- 代码：`src/test/armor_detector_contract_test.cpp`、`src/test/CMakeLists.txt`
- 文档：`docs/test/armor_detector_contract_test.md`
- 变更点：新增空帧、角点顺序、ROI 恢复、丢失回退四类契约用例与测试目标。

4. 设计规格
- 代码：本次实现对应 detection 子域改动
- 文档：`docs/superpowers/specs/2026-03-30-detection-rule-design.md`
- 变更点：R1-R5 规则映射、测试矩阵、验证命令、风险与回退策略。
