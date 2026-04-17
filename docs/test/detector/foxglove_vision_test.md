# foxglove_vision_test 使用说明

## 1. 测试目标

`mv-foxglove-vision-test` 用于验证检测链路在 Foxglove 可视化下的在线行为，覆盖：

- 图像输入（OpenCV 输入与 SimCamera 输入）；
- 检测/解算/预测输出的可视化发布；
- 基于 `ArmorDetectorParamManager` 的 `armor.*` 参数热调；
- 参数覆盖文件 `src/config/debug/debug_override.yaml` 的加载与保存。

本程序是联调辅助入口，不替代在线主跑入口 `mv-vision-main`。

## 2. 构建命令

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build --target mv-foxglove-vision-test -j$(nproc)
```

## 3. 运行命令

### 3.1 OpenCV 输入（摄像头）

```bash
./build/src/test/mv-foxglove-vision-test 0 red 8765
```

### 3.2 OpenCV 输入（视频文件）

```bash
./build/src/test/mv-foxglove-vision-test /path/to/video.mp4 blue 8765
```

### 3.3 SimCamera 输入（默认 endpoint 来自 vision.yaml）

```bash
./build/src/test/mv-foxglove-vision-test sim blue 8765
```

### 3.4 SimCamera 输入（命令行覆盖 endpoint）

```bash
./build/src/test/mv-foxglove-vision-test sim:127.0.0.1:19090 blue 8765
```

## 4. 参数热调与保存

- `enemy_color` 由 Foxglove 参数面板实时切换。
- `armor.*` 参数由 `ArmorDetectorParamManager` 接管并实时应用到检测器。
- 程序退出时会将当前 detector 参数保存到：

```text
src/config/debug/debug_override.yaml
```

## 5. 常见问题

1. `无法打开 OpenCv 图像源`
- 原因：设备索引无效或当前环境无 `/dev/video*`。
- 处理：改用视频文件输入或 SimCamera 输入。

2. `无法打开 SimCamera`
- 原因：仿真端未启动或 endpoint 不匹配。
- 处理：先启动 `at_vision_simulator`，再确认 `camera.sim_endpoint` 或 `sim:host:port`。

3. Foxglove 无画面
- 原因：未连接到正确地址或端口。
- 处理：Foxglove Studio 连接 `ws://<host>:8765`（或你传入的端口）。

## 6. 维护约束

- 若修改 `sim` CLI 参数语义，需同步更新本文件的运行示例。
- 若调整 `armor.*` 参数键名，需同步更新参数面板说明和保存行为。
