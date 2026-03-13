# Calibration 标定工具模块（`mv-tool-calibration-*`）

> 路径：`src/tool/calibration/`
> CMake 目标：
> - `mv-tool-calibration-common`（STATIC 公共库）
> - `mv-calib-capture`（采集工具）
> - `mv-calib-camera`（内参标定）
> - `mv-calib-rwhandeye`（RobotWorld-HandEye 外参标定）
> - `mv-calib-validate`（标定结果校验）

---

## 1. 设计目标

将标定流程拆成「采集 → 内参 → 外参」三个独立可执行工具，统一通过 `vision.yaml` 的 `calibration` 节点交换数据，形成可复现、可审计、可脚本化的离线流程。

```
采集阶段                    标定阶段                           校验阶段                   消费阶段
┌─────────────────┐       ┌───────────────────────────┐     ┌─────────────────────┐
│ mv-calib-capture│       │ mv-calib-camera           │     │ src/modules/pnp_solver│
│  i.jpg + i.txt   │ ───► │ 写 camera_matrix / dist   │ ─►  │ 读取 calibration 节点 │
└─────────────────┘       └───────────────────────────┘     └─────────────────────┘
           │
           └──────────────► ┌───────────────────────────┐
                             │ mv-calib-rwhandeye       │
                             │ 写 R_camera_to_gimbal    │
                             │ 写 t_camera_to_gimbal    │
                             └───────────────────────────┘
                                               │
                                               └──────────► ┌─────────────────────────────┐
                                                            │ mv-calib-validate           │
                                                            │ det/正交性/重投影统计        │
                                                            └─────────────────────────────┘
```

---

## 2. 文件一览

| 文件 | 作用 |
|------|------|
| `calibration_io.hpp/.cpp` | 公共 I/O 与图案识别：板型解析、角点检测、`calibration` 节点读写 |
| `capture_calib_data.cpp` | 图像+姿态采集（输出 `i.jpg` 与 `i.txt`） |
| `calibrate_camera.cpp` | 内参标定：输出 `camera_matrix` 与 `distort_coeffs` |
| `calibrate_robotworld_handeye.cpp` | 外参标定主流程：输出 `R_camera_to_gimbal` 与 `t_camera_to_gimbal` |
| `validate_calibration.cpp` | 结果校验：`det(R)`、正交性、重投影统计 |
| `CMakeLists.txt` | 声明公共库和三个可执行目标 |
| `README.md` | 最小操作手册（快速命令） |

---

## 3. 配置约定（唯一数据源）

所有写回统一落到：`src/config/vision.yaml`

```yaml
calibration:
  camera_matrix: [fx, 0, cx, 0, fy, cy, 0, 0, 1]
  distort_coeffs: [k1, k2, p1, p2, k3]
  R_gimbal_to_imu: [r00, ..., r22]            # 可选，默认单位阵
  R_camera_to_gimbal: [r00, ..., r22]
  t_camera_to_gimbal: [tx, ty, tz]            # 单位 m
```

约定说明：
- `camera_matrix`、`distort_coeffs` 由 `mv-calib-camera` 写入。
- `R_camera_to_gimbal`、`t_camera_to_gimbal` 由 `mv-calib-rwhandeye` 写入。
- 写回策略是增量更新，不覆盖 `calibration` 之外的节点。

---

## 4. 快速上手

### 4.1 编译

```bash
cmake -S . -B build
cmake --build build --target mv-calib-capture mv-calib-camera mv-calib-rwhandeye mv-calib-validate -j
```

### 4.2 采集（支持棋盘格 / 圆点板，串口实时姿态同步）

```bash
./build/bin/mv-calib-capture \
  --source-type camera \
  --camera-id 0 \
  --pose-source serial \
  --serial-config src/config/vision.yaml \
  --serial-node serial \
  --serial-max-age-ms 30 \
  --output-folder data/calib_capture \
  --pattern-type chessboard \
  --cols 10 --rows 7 --spacing-mm 40.0
```

键位：
- `s`：保存当前帧（同步写 `i.jpg` 和 `i.txt`）
- `q`：退出

离线姿态输入（可选）：

```bash
./build/bin/mv-calib-capture \
  --source-type video \
  --video-path input.mp4 \
  --pose-source file \
  --quat-file poses.txt \
  --output-folder data/calib_capture
```

`poses.txt` 每行格式：`w x y z`

### 4.3 内参标定

```bash
./build/bin/mv-calib-camera \
  data/calib_capture \
  --vision-path src/config/vision.yaml \
  --pattern-type chessboard \
  --cols 10 --rows 7 --spacing-mm 40.0 \
  --write=true
```

输出：
- 终端打印平均重投影误差（px）
- 写回 `camera_matrix` 和 `distort_coeffs`

### 4.4 外参标定（RobotWorld-HandEye 主流程）

```bash
./build/bin/mv-calib-rwhandeye \
  data/calib_capture \
  --vision-path src/config/vision.yaml \
  --pattern-type chessboard \
  --cols 10 --rows 7 --spacing-mm 40.0 \
  --write=true
```

输出：
- 终端打印 `det(R_camera_to_gimbal)`
- 写回 `R_camera_to_gimbal` 与 `t_camera_to_gimbal`（m）

### 4.5 结果校验（闭环）

```bash
./build/bin/mv-calib-validate \
  data/calib_capture \
  --vision-path src/config/vision.yaml \
  --pattern-type chessboard \
  --cols 10 --rows 7 --spacing-mm 40.0 \
  --max-reproj-px 1.5 \
  --det-eps 0.05 \
  --ortho-eps 0.05
```

---

## 5. CLI 参数速查

### `mv-calib-capture`

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--pose-source` | `serial` / `file` / `identity` | `serial` |
| `--source-type` | `camera` 或 `video` | `camera` |
| `--camera-id` | 相机索引（source-type=camera） | `0` |
| `--video-path` | 视频路径（source-type=video） | 空 |
| `--output-folder` | 输出目录 | `data/calib_capture` |
| `--quat-file` | 离线姿态文件（wxyz） | 空 |
| `--serial-config` | 串口配置 YAML 路径 | `src/config/vision.yaml` |
| `--serial-node` | YAML 中串口节点键名 | `serial` |
| `--serial-max-age-ms` | 图像-姿态最大时间差（ms） | `30` |
| `--pattern-type` | `chessboard` / `circles` | `chessboard` |
| `--cols` / `--rows` | 板型尺寸 | `10` / `7` |
| `--spacing-mm` | 间距（mm） | `40.0` |

### `mv-calib-camera`

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `input-folder` | 标定图像目录 | `data/calib_capture` |
| `--vision-path` | EX 配置路径 | `src/config/vision.yaml` |
| `--pattern-type` | `chessboard` / `circles` | `chessboard` |
| `--cols` / `--rows` | 板型尺寸 | `10` / `7` |
| `--spacing-mm` | 间距（mm） | `40.0` |
| `--fix-k3` | 是否固定 `k3` | `true` |
| `--write` | 是否写回配置 | `true` |

### `mv-calib-rwhandeye`

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `input-folder` | `i.jpg + i.txt` 目录 | `data/calib_capture` |
| `--vision-path` | EX 配置路径 | `src/config/vision.yaml` |
| `--pattern-type` | `chessboard` / `circles` | `chessboard` |
| `--cols` / `--rows` | 板型尺寸 | `10` / `7` |
| `--spacing-mm` | 间距（mm） | `40.0` |
| `--write` | 是否写回配置 | `true` |

### `mv-calib-validate`

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `input-folder` | 校验图像目录 | `data/calib_capture` |
| `--vision-path` | EX 配置路径 | `src/config/vision.yaml` |
| `--pattern-type` | `chessboard` / `circles` | `chessboard` |
| `--cols` / `--rows` | 板型尺寸 | `10` / `7` |
| `--spacing-mm` | 间距（mm） | `40.0` |
| `--max-reproj-px` | 重投影均值阈值（px） | `1.5` |
| `--det-eps` | `|det(R)-1|` 阈值 | `0.05` |
| `--ortho-eps` | `||R^T R - I||_F` 阈值 | `0.05` |

---

## 6. 算法与单位约定

### 6.1 图案识别

- 棋盘格：`findChessboardCorners + cornerSubPix`
- 圆点板：`findCirclesGrid (SYMMETRIC_GRID)`

### 6.1.1 串口姿态同步

- `mv-calib-capture` 在 `pose-source=serial` 下解析 MCU 上行帧（`0xAA 0xFF`）。
- 从上行四元数字段（`q_w/q_x/q_y/q_z`，缩放 1/10000）恢复姿态。
- 保存时按当前图像时间戳选择最近姿态，若超过 `serial-max-age-ms` 则跳过该次保存。

### 6.2 内参

- 使用 `calibrateCamera` 解算。
- 重投影误差以逐点欧氏距离均值（px）输出。

### 6.3 外参（RobotWorld-HandEye）

- 每组样本：
  1) 图像角点 + 内参 -> `solvePnP`（IPPE）
  2) 四元数 -> `R_world_to_gimbal`
  3) 输入 `calibrateRobotWorldHandEye`
- 输出 `R_gimbal_to_camera`、`t_gimbal_to_camera` 后反解为 `camera -> gimbal`。
- 平移单位：模板点是 mm，故先得到 mm，再统一换算到 m 写回。

### 6.4 校验指标

- 旋转矩阵：`det(R_camera_to_gimbal)` 应接近 `+1`；
- 正交性：`||R^T R - I||_F` 应足够小；
- 重投影：在采集样本集上统计 mean / median / max。

---

## 7. 验收建议

- 内参阶段：
  - 有效样本数 `>= 10`
  - 平均重投影误差建议 `< 1.5 px`
- 外参阶段：
  - 有效样本数 `>= 6`
  - `det(R_camera_to_gimbal)` 接近 `+1`
  - `t_camera_to_gimbal` 数量级与机械安装尺寸一致（cm 级）
- 校验阶段：
  - `mv-calib-validate` 返回码为 `0`（PASS）

---

## 8. 与重构架构关系

当前标定模块定位为「离线工具链」，因此优先保证：
- 命令行流程清晰
- 配置单一数据源
- 结果可复现

在后续出现多算法并存（例如 charuco / fisheye / 多手眼方法）且需要运行时选择时，可再引入工厂式扩展点；在需要稳定对外 API 且隔离重依赖时，再评估 PImpl 化。

---

## 9. 目录结构

```
docs/refractor/tool/
├── debug/
│   └── DEBUG_MODULE.md
├── foxglove/
│   └── FOXGLOVE_MODULE.md
└── calibration/
    └── CALIBRATION_MODULE.md
```
