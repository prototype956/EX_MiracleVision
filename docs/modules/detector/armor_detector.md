# Armor Detector 模块说明

## 1. 模块职责

`armor_detector` 子域由 `BasicArmorDetector` 与 `RoiManager` 组成：

- `BasicArmorDetector`：负责在输入帧中提取装甲板 2D 检测结果；
- `RoiManager`：负责帧间 ROI 裁剪、坐标恢复、ROI 状态更新。

职责边界：

- detector 不感知 ROI 状态；
- ROI 管理器不承载检测算法逻辑。

## 2. 关键调用链

`RoiManager::Crop -> BasicArmorDetector::Detect -> RoiManager::RestoreAndUpdate`

语义说明：

1. `Crop` 返回 ROI 视图与偏移；
2. `Detect` 输出相对于输入视图的 `Detection`；
3. `RestoreAndUpdate` 将坐标恢复到全图，并更新下一帧 ROI。

## 3. Detection 契约（与 PnP 对齐）

角点顺序固定为：`BL, BR, TR, TL`。

- `points[0] = BL`
- `points[1] = BR`
- `points[2] = TR`
- `points[3] = TL`

该顺序与 `pnp_solver` 的世界模板一致；变更顺序必须同步更新 detector、pnp、测试与文档。

## 4. 坐标系约定

- `Detect(frame, ...)` 输出坐标属于当前输入帧坐标系；
- 若输入是 ROI 局部图，则输出为局部坐标；
- `RoiManager::RestoreAndUpdate` 负责坐标恢复为全图坐标。

`distance_to_center` 约定：

- detector 内可先按输入帧中心计算；
- 经过 `RestoreAndUpdate` 后按全图中心重算，保证下游排序一致。

## 5. ROI 状态机

`RoiManager` 维护 `State{roi_rect, lost_count}`：

- 检测成功：按检测包围盒扩展 ROI（X 方向 ×1.5，Y 方向 ×2.0）；
- 检测失败：`lost_count++`；
- `lost_count >= 5`：回退全图（清空 ROI）。

## 6. 测试入口

契约测试目标：`mv-armor-detector-contract-test`

覆盖内容：

- 空帧返回空容器；
- 角点顺序与颜色契约；
- ROI 坐标恢复；
- 连续丢失回退。

详细命令与样例见 `docs/test/armor_detector_contract_test.md`。
