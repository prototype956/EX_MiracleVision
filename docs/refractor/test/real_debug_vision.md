# real_debug_vision 重写说明

## 第一步：驱动相机

目标：先打通图像输入链路，确保后续检测与预测模块有稳定帧源。

### 1. 模块定位

- 相机抽象接口：src/hal/camera/i_camera.hpp
- 工业相机实现：src/hal/camera/mindvision_camera.hpp
- OpenCV 相机/视频实现：src/hal/camera/opencv_camera.hpp
- 配置来源：configs/vision.yaml 的 camera 节点
- 当前调试入口：src/test/real_debug_vision.cpp

### 2. 调用顺序

1. 初始化日志与配置管理器。
2. 读取 vision.yaml，获取 camera.backend 与 camera 子配置。
3. 根据 backend 选择具体相机实现：
	- mindvision: 使用 MindVisionCamera；
	- 其他: 使用 OpenCvCamera，并将配置映射为 source/width/height/fps。
4. 调用 Open 完成设备初始化。
5. 主循环调用 Grab 获取图像帧。
6. 本地窗口预览，按 q/ESC 退出。
7. 调用 Close 释放资源。

### 3. 本步完成判定

- 终端日志出现“相机打开成功”。
- 窗口连续显示实时图像。
- 退出后日志显示“正常退出”，无崩溃或卡死。

### 4. 下一步建议

在相机驱动稳定后，进入“第二步：挂接检测模块（Detector）”，先只做
Detect 输入输出验证，不引入 Solver/Predictor/Serial。

## 第二步：接入传统视觉检测（模块定位与调用）

目标：在现有相机取流基础上，接入 `BasicArmorDetector`，输出检测结果数量与坐标。

### 1. 相关文件

- 检测接口定义：[src/interfaces/i_detector.hpp](src/interfaces/i_detector.hpp)
- 传统视觉实现：[src/modules/armor_detector/basic_armor_detector.hpp](src/modules/armor_detector/basic_armor_detector.hpp)
- ROI 管理器（建议配套）：[src/modules/armor_detector/roi_manager.hpp](src/modules/armor_detector/roi_manager.hpp)
- 现成参考调用（最贴近本任务）：[src/test/video_pipeline_test.cpp](src/test/video_pipeline_test.cpp)

### 2. 推荐调用顺序

1. 在主循环前创建检测器实例：`mv::modules::BasicArmorDetector detector;`
2. 用整树配置初始化：`detector.Init(root_cfg)`。
3. 读取敌方颜色：`cfg.Get<std::string>("auto_aim.enemy_color", "red")` 并转换为 `mv::ArmorColor`。
4. 每帧流程建议：
	 - `auto [cropped, offset] = roi_mgr.Crop(frame);`
	 - `auto detections = detector.Detect(cropped, enemy_color);`
	 - `roi_mgr.RestoreAndUpdate(detections, offset, frame.size());`
5. 将 `detections.size()` 接到 HUD/FPS 统计，先做可视化与计数验证。

### 3. 参考实现位置（用于对照）

- 模块初始化入口：
	[src/test/video_pipeline_test.cpp](src/test/video_pipeline_test.cpp)
- 主循环 Detect 调用：
	[src/test/video_pipeline_test.cpp](src/test/video_pipeline_test.cpp)

## 当前开发进度（与代码同步）

- 已完成：第一步相机驱动联通（读取配置、打开 MindVision 相机、实时取帧显示）。
- 已完成：基础性能观测（`MetricsTracker` + `TerminalHUD` 已接入）。
- 进行中：第二步传统视觉检测接入（当前文档已完成模块映射与调用链梳理）。
- 未开始：PnP 解算、预测、决策、串口下发联调。
