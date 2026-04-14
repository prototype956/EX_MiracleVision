# 坐标转换与安装误差调参实操指南

本文针对现场集成人员，给出如何理解坐标系统链路、识别安装误差、分阶段调参的完整流程。面向"机械安装后需要调试参数"的使用场景。

## 1. 坐标链路总览与为什么这很重要

EKF 在世界系（固定参考系）中估计目标状态，但检测器输出的是相机系坐标。不做转换会导致云台转动时滤波器误认为目标在快速移动，表现为预测点抖动、收敛失败、甚至发散。

**坐标转换链路**：

```
Detection (像素坐标)
  ↓ [PnP解算]
相机系 (xyz_camera)
  ↓ [相机→云台外参 R_c2g]
云台系 (xyz_gimbal)
  ↓ [四元数→旋转矩阵 R_gimbal_abs]
  ↓ [云台→IMU车体外参 R_g2body, 逆转置]
世界系 (xyz_world)
```

**关键输入**：
1. **四元数**（来自 IMU，每帧更新）：云台在绝对坐标系中的姿态
2. **相机-云台外参** $R_{c2g}, t_{c2g}$（标定后固定）：机械安装的相对位置
3. **云台-车体外参** $R_{g2body}$（机械购置后固定）：设计参数，通常是单位矩阵

## 2. 安装误差类型与表现

| 误差类型 | 原因 | 现场表现 | 排查方法 |
|---------|------|--------|---------|
| 四元数范数异常 | IMU 数据格式错、校准失败 | 无法追踪、NIS 持续高 | 见§3.1 |
| R_c2g 标定偏差 | 手眼标定质量差或光学畸变 | 重投影误差 > 2px、系统性偏移 | 见§3.2 |
| R_g2body 定义错 | 云台轴向与代码约定不一致 | 云台转动时预测点反向移动 | 见§3.3 |
| 四元数延迟 | 串口时戳不同步、缓冲延迟 | 云台快速转动时点跳 | 见§3.4 |
| 坐标转换顺序错 | 代码矩阵乘法顺序反了 | 所有方向都有系统性错误 | 见§3.5 |

## 3. 分阶段调参流程

### 3.1 验证四元数数据质量（硬件预集成）

**目标**：确保 IMU 发送的四元数范数接近 1，顺序正确，不倒值。

**步骤 1.1**：检查四元数顺序与格式

在 [serial_node.cpp L210-220](https://github.com/EX_MiracleVision/src/pipeline/serial_node.cpp) 中，按照下位机协议解析四元数：

```cpp
// 典型格式：[9-16] 字节为四元数 (w, x, y, z) × 10000，Little-Endian i16
double QUAT_W = data[9:11] / 10000.0;  // bytes 9-10
double QUAT_X = data[11:13] / 10000.0; // bytes 11-12
double QUAT_Y = data[13:15] / 10000.0; // bytes 13-14
double QUAT_Z = data[15:17] / 10000.0; // bytes 15-16
```

**验证方法**：在静止状态（云台不动），抓包观察：
- 四元数应接近 `(1, 0, 0, 0)`（或其他固定值）
- 如果接近 `(0, *, *, *)`，说明顺序反了（w 与其他分量互换）

**步骤 1.2**：检查四元数范数

添加日志输出：
```cpp
// 在 serial_node.cpp 中，解析后立刻验证
double Q_NORM = std::sqrt(QUAT_W*QUAT_W + QUAT_X*QUAT_X + 
                           QUAT_Y*QUAT_Y + QUAT_Z*QUAT_Z);
if (std::abs(Q_NORM - 1.0) > 0.01) {
  MV_LOG_WARN("Serial", "Quaternion norm abnormal: {:.4f}", Q_NORM);
}
```

**判定标准**：
- ✅ 范数 = 1.0 ± 0.01（正常）
- ⚠️ 范数 = 1.0 ± 0.05（可用，但精度有损）
- ❌ 范数 > 1.1 或 < 0.9（数据不可用，检查 IMU 校准）

**步骤 1.3**：验证转换的一致性

启动管线，手动转动云台，观察 Foxglove 中 `/tf` 树：
- ✅ `world` 与 `gimbal` 坐标系标签分离转动
- ❌ 重合或不动（说明四元数未生效）

如果不动，检查 `SetGimbalOrientation` 是否被调用（见 [ekf_predictor.cpp L180](https://github.com/EX_MiracleVision/src/modules/ekf_predictor/ekf_predictor.cpp#L180)）。

### 3.2 相机-云台外参标定与验证（标定阶段）

**目标**：获得高精度的 $R_{c2g}, t_{c2g}$，使重投影误差 < 1.5px。

**依赖工具**：
- `mv-calib-capture`（数据采集，需连接下位机串口）
- `mv-calib-rwhandeye`（手眼标定，输出外参）
- `mv-calib-validate`（验证标定质量）

位置：[src/tool/calibration/README.md](https://github.com/EX_MiracleVision/src/tool/calibration/README.md)

**步骤 2.1**：数据采集

```bash
# 启动采集程序，确保串口配置与实机一致
./build/bin/mv-calib-capture \
  --source-type camera \
  --camera-id 0 \
  --pose-source serial \
  --serial-config configs/auto_aim.yaml \
  --output-folder data/calib_rwhe_capture \
  --pattern-type chessboard \
  --cols 10 --rows 7 --spacing-mm 40.0
```

**操作方式**：
- 启动后，程序会等待棋盘检测
- 在不同距离（0.3m 到 2m）、不同角度（yaw±45°, pitch±15°）拍摄棋盘
- 每个有效检测会保存一张图像 + 对应的四元数姿态（自动对齐时间戳）
- 采集 15-25 张图像（越多越好，覆盖工作空间）

**采集质量检查**：
- 所有采集的四元数范数都 ≈ 1.0（否则重新采集）
- 棋盘覆盖整个图像的不同区域
- 角度覆盖云台的实际工作范围

**步骤 2.2**：执行手眼标定

```bash
./build/bin/mv-calib-rwhandeye \
  data/calib_rwhe_capture \
  --vision-path configs/auto_aim.yaml \
  --write=true  # 直接写回 vision.yaml
```

输出示例：
```
[INFO] Hand-eye calibration result:
R_camera2gimbal = [
  0.9998  -0.0125  -0.0156
  0.0134   0.9997  -0.0089
  0.0152   0.0097   0.9998
]
t_camera2gimbal = [-0.0032, 0.0015, -0.0108]  # 单位：米

Reprojection error RMS: 0.87 px (GOOD)
Calibration matrix orthogonality: 0.0008 (EXCELLENT)
```

**合格标准**：
| 指标 | 合格范围 | 不合格表现 |
| --- | --- | --- |
| 重投影 RMS | < 1.0 px | > 2.0 px → 重新采集数据，质量更好 |
| 重投影 Max | < 3.0 px | > 5.0 px → 检查 outlier，可能有棋盘检测错误 |
| det(R) | ≈ 1.0 ± 0.01 | 偏离 > 0.05 → 矩阵非正交，算法问题 |
| \|\|R^T R - I\|\|_F | < 0.01 | > 0.05 → 重新采集，覆盖范围不足 |

**步骤 2.3**：验证标定结果

```bash
./build/bin/mv-calib-validate \
  data/calib_rwhe_capture \
  --vision-path configs/auto_aim.yaml \
  --max-reproj-px 1.5
```

逐帧验证每个数据点的重投影误差，输出统计：
```
[INFO] Validation report:
  Mean reprojection error: 0.92 px ✅
  Max reprojection error: 2.1 px ✅
  Failed frames (> 1.5 px): 1 / 20 (5%) ✅
  
Overall: PASS (recommend usage)
```

如果某帧失败，检查对应的棋盘图像是否清晰、四元数是否异常。

### 3.3 云台-车体坐标系定义确认（机械设计对齐）

**目标**：确保代码中的 $R_{gimbal2body}$ 与实物安装一致，云台轴向定义对。

**参数位置**：[vision.yaml 的 calibration 节点](https://github.com/EX_MiracleVision/configs/vision.yaml)

```yaml
calibration:
  R_gimbal2imubody: [1, 0, 0, 0, 1, 0, 0, 0, 1]  # 通常是单位矩阵
```

**场景 1**：如果云台坐标系与 IMU 车体坐标系轴对齐

```yaml
R_gimbal2imubody: [1, 0, 0, 0, 1, 0, 0, 0, 1]  # I (单位矩阵)
```

**场景 2**：如果云台 pitch 轴与车体 roll 轴一致（某些设计）

需机械确认，然后在代码中设定对应的旋转矩阵（通常不常见）。

**验证方法**：
1. 启动管线，云台 Yaw 从 -90°→ 0°→ +90°
2. 观察 Foxglove 中 IMU 的三轴角速度变化
3. ✅ 应只有 Z 轴分量变化（如果 R_gimbal2body = I）
4. ❌ 如果 X 或 Y 轴也变化，说明坐标定义有问题

**调整方法**：与机械联系，确认实物云台的轴向定义，然后更新 $R_{gimbal2body}$。

### 3.4 时序同步验证（四元数与检测同步）

**目标**：确保每帧检测都与对应时刻的四元数对齐，延迟 < 50ms。

**链路检查**：

```
Camera frame @ t_k
  ↓ (通过 USB / 相机 API)
Detection 处理 @ t_k + processing_delay
  ↓ 
PredictNode 收到 Detection @ t_k + 5ms (典型)
  ↓ [SetGimbalOrientation] 读取最近的四元数
           ↓
        接收四元数 @ t_q
  ↓ 
EkfPredictor 用四元数转换坐标 @ t_k
```

**问题**：如果 $t_q$ 与 $t_k$ 差太大（> 50ms），云台快速转动时会出现"点跳"。

**验证方法**：

1. 在 [serial_node.cpp L210](https://github.com/EX_MiracleVision/src/pipeline/serial_node.cpp) 添加日志：

```cpp
auto t_received = std::chrono::steady_clock::now();
// ... 解析四元数后 ...
MV_LOG_INFO("Serial", "Quaternion received at {}, camera frame at {}",
    t_received.time_since_epoch().count(),
    detection_timestamp.time_since_epoch().count());
```

2. 启动管线，云台做快速转动（yaw 100°/s），录制日志
3. 分析日志中时间戳差值：
   - ✅ 通常在 0-30ms（正常，FIFO+ USB 延迟）
   - ⚠️ 30-50ms（可接受，但快速转动时可能有小影响）
   - ❌ > 100ms（说明串口缓冲或时间同步问题，需排查）

**调整方法**：
- 检查下位机的串口发送频率（通常 100Hz 或以上）
- 检查 [serial_node.cpp 中的 TryRecv()](https://github.com/EX_MiracleVision/src/pipeline/serial_node.cpp) 是否有缓冲积压

### 3.5 端到端验证（综合判定）

**目标**：用多个可观测指标综合确认坐标系统的正确性。

**指标 1**：重投影误差

启动完整管线，运行 [validation 工具](https://github.com/EX_MiracleVision/src/tool/calibration/):

```bash
./build/bin/mv-calib-validate \
  data/calib_rwhe_capture \
  --vision-path configs/auto_aim.yaml \
  --max-reproj-px 1.5
```

- ✅ Mean < 1.0 px
- ✅ Max < 3.0 px
- ✅ Failed frames < 10%

**指标 2**：追踪稳定性与 NIS

运行真实视频或现场测试，观察日志：

```
[EkfTracker] NIS [window_size=100]: failure_rate=15%   ✅ (< 20%)
[EkfTracker] state: "tracking" (持续出现)               ✅
[EkfTracker] diverged=false                             ✅
```

如果 NIS failure_rate > 40%，说明观测与预测不一致 → 优先检查坐标系统。

**指标 3**：世界系稳定性（Foxglove 可视化）

```bash
# 启动 Foxglove Studio
# 连接到 ws://localhost:8765
# 在 3D 面板中观看 /tracking/aim_point (世界系坐标)
```

- ✅ 云台转动时，世界系下的目标点平滑移动，不抖动
- ❌ 目标点反向移动 → R_gimbal2body 定义错
- ❌ 目标点剧烈抖动 → 四元数质量差或延迟大
- ❌ 目标点消失或跳跃 → 坐标转换顺序错或 NaN 值

**指标 4**：快速转动一致性测试

操作：云台以 100°/s（或更快）yaw 转动，同时对准静止目标

观察：
- ✅ 预测点跟随顺畅，无明显延迟或反向
- ❌ 预测点有 50-100ms 延迟 → 收紧时序同步
- ❌ 预测点反向移动 → R_gimbal2body 反了
- ❌ 预测点抖动明显 → process_noise 太大或坐标精度差

## 4. 现场排错快速清单

| 问题现象 | 可能原因 | 检查项 | 修复方向 |
|---------|---------|--------|---------|
| Foxglove 中 `gimbal` 与 `world` 标签重合不分离 | 四元数未注入或范数异常 | [serial_node.cpp L210](https://github.com/EX_MiracleVision/src/pipeline/serial_node.cpp#L210) 解析逻辑；四元数范数 | 修复四元数解析顺序或 IMU 校准 |
| 重投影误差 > 2px | 相机内参或外参标定偏 | 重新运行 `mv-calib-rwhandeye`，采集更多数据 | 重新标定或提高采集覆盖范围 |
| 追踪目标位置跳变或反向 | R_gimbal2body 定义错 | 机械图纸中云台轴向定义；验证 R_gimbal2body 矩阵 | 对照机械图纸，更正矩阵 |
| EKF 发散（NIS > 7.8 持续） | 坐标转换链路错误 | 检查矩阵乘法顺序；验证四元数范数 | 跟踪代码执行流程，对照数学文档 |
| 云台快转时预测点脱落 | 四元数延迟或缓存问题 | 串口时间戳差值；缓冲队列大小 | 提高下位机发送频率或减少处理延迟 |
| 所有打点都系统性偏向某侧 | 坐标系统微小偏差或光学畸变 | 重投影验证；yaw/pitch 偏置调整 | 小参数调整（±2° 试），或重新标定 |

## 5. 标准实操流程（按优先级）

**新设备第一次集成**的推荐操作顺序：

1. 硬件预集成（30min）
   - ✅ 四元数范数验证
   - ✅ /tf 树可视化验证

2. 标定与外参获取（60-90min）
   - ✅ 采集 20 张图像 + 姿态
   - ✅ 运行 rwhandeye 标定
   - ✅ 验证重投影 < 1.0px

3. 时序验证（15min）
   - ✅ 日志中时间差 < 50ms
   - ✅ 快速转动无异常

4. 追踪验证（30min）
   - ✅ NIS 失败率 < 20%
   - ✅ 静止目标锁定稳定
   - ✅ Foxglove 点平滑无抖

5. 参数微调（见[参数调参手册](ekf_tuning_guide.md)）
   - ✅ 追踪稳定性
   - ✅ 打点精度

**总耗时**：4-5 小时（视采集数据质量）

## 6. 进阶阅读

- EKF 参数调参：见[EKF 与轨迹关键参数调参手册](ekf_tuning_guide.md)
- 数学细节：见[EKF 数学与工程直觉详解](ekf_detailed_mathematics.md)
- 主文档：见[EKF Predictor 模块工作原理](ekf_predictor.md)
