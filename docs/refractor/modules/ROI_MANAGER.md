# RoiManager：帧间 ROI 状态机

> 路径：`src/modules/armor_detector/roi_manager.hpp`
> 实现：Header-only（无对应 .cpp）

---

## 1. 设计动机

传统做法将 ROI 状态机内嵌于检测器，导致：

- 检测器带帧间状态，`Detect()` 不再是幂等函数；
- 调试时难以单独测试检测逻辑；
- `DebugData` 需要额外存储 `roi_offset`/`frame_size` 字段供渲染层使用。

`RoiManager` 将 ROI 职责完全解耦：检测器变为**纯无状态函数**，每帧接受任意子图作为输入，返回相对于该子图的坐标；ROI 的裁剪、坐标还原、`lost_count` 计数全部由 `RoiManager` 负责。

---

## 2. 工作流程

```
┌─────────────────────────────────────────────────────────────┐
│                         主循环                              │
│                                                             │
│  ① roi_mgr.Crop(frame)                                     │
│       → 按上一帧 ROI 裁剪；若无历史 ROI 返回完整帧         │
│       → 返回 CropResult { view, offset }                  │
│                                                             │
│  ② detector.Detect(view, color)                            │
│       → 纯检测，输入任意子图，输出局部坐标                  │
│                                                             │
│  ③ roi_mgr.RestoreAndUpdate(dets, offset, frame.size())    │
│       → dets 坐标就地修改为全图坐标                         │
│       → 计算新 ROI（成功帧扩展，失败帧累计 lost_count）    │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. ROI 扩展策略

| 条件 | 行为 |
|------|------|
| 本帧检测成功 | 以所有目标包围盒为基础，水平扩展 ×1.5 bbox_w，垂直扩展 ×2.0 bbox_h，作为下一帧 ROI |
| 本帧检测失败 | `lost_count++`；若 `lost_count >= kMaxLost (=5)` 则清空 ROI，回退全图 |
| 外部调用 `Reset()` | 立即清空 ROI，下一帧使用全图（场景：切换识别颜色） |

ROI 与帧边界的交集在 `Crop()` 内部取 `& FRAME_RECT` 保证不越界。

---

## 4. API 速查

```cpp
namespace mv::modules {

class RoiManager {
public:
    struct CropResult {
        cv::Mat view;              // ROI 视图（frame 的子 Mat，不分配内存）
        cv::Point2i offset{0, 0}; // 本帧 ROI 左上角在原始帧中的坐标
    };

    // ① 裁剪：按历史 ROI 返回子图；无 ROI 时返回完整帧
    [[nodiscard]] CropResult Crop(const cv::Mat& frame) const;

    // ③ 还原坐标并更新 ROI 状态（就地修改 detections）
    void RestoreAndUpdate(std::vector<Detection>& detections,
                          const cv::Point2i& offset,
                          const cv::Size& frame_size);

    // 立即清空 ROI（切换颜色、异常跳变等场景）
    void Reset() noexcept;

    // 获取当前活跃 ROI（area()==0 表示全图）
    [[nodiscard]] cv::Rect2i GetRoiRect() const noexcept;

    // ROI 是否激活
    [[nodiscard]] bool IsActive() const noexcept;
};

} // namespace mv::modules
```

---

## 5. 与 DebugSession / ViewRenderer 的协作

`RoiManager::GetRoiRect()` 返回的矩形通过 `DebugSession::Feed()` 的 `roi_rect` 参数传入渲染器，在 **5 键（`ViewMode::ROI`）**视图下以青色矩形叠加在原始帧上：

- ROI 激活时：绘制青色矩形 + 标签 `"ROI"` + 当前帧检测结果框
- ROI 未激活时：显示文字 `"ROI: FULL FRAME"`

```cpp
// 主循环中
dbg.Feed(frame, detector.GetDebugData(), detections, ctrl,
         detector.GetParams(), status,
         roi_mgr.GetRoiRect());   // ← 传入 ROI 矩形
```

---

## 6. 帧率收益参考

启用 ROI 后，二值化（`MakeBinary`）和 `findContours` 处理的像素数量随 ROI 面积缩减而等比下降：

| 场景 | ROI 面积 / 全图面积 | 预期耗时比例 |
|------|---------------------|--------------|
| 近距目标（装甲板占 ~1/8 画面）| ~1/4（含扩展边距）| ~25% |
| 中距目标（装甲板占 ~1/16 画面）| ~1/8 | ~12% |
| 目标丢失（连续 5 帧后）| 回退 100% | 100% |

---

## 7. 注意事项

- `Crop()` 返回的 `view` 是 `frame` 的**浅拷贝（子 Mat）**，生命周期与 `frame` 绑定，不要在 `frame` 超出作用域后使用 `view`。
- `RestoreAndUpdate()` 会**就地修改** `detections`，调用后 `dets` 中坐标已是全图坐标，可直接传给 `PnpSolver`、`Predictor`。
- `dist_to_center` 在 `RestoreAndUpdate()` 中**自动重新计算**（基于全图坐标），无需外部处理。
