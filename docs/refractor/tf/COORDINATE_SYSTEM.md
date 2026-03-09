# MiracleVision 坐标系规范

> 版本：2026-03-09 | 状态：**包含已知问题与修复方案**

---

## 一、系统中的坐标帧

### 1.1 相机帧（`camera`）

OpenCV 标准惯例，在相机标定和 PnP 解算中使用。

```
      Z（深度·前）
     /
    /
   O ——→ X（右）
   |
   ↓
   Y（下）
```

| 轴 | 方向 | 说明 |
|---|---|---|
| X | 朝右（像素列增加方向） | 水平方向 |
| Y | 朝下（像素行增加方向） | 竖直方向（正值=下方） |
| Z | 朝前（远离镜头） | 深度，即目标距离 |

---

### 1.2 云台帧（`gimbal`）

PnP 解算结果 `det.xyz_in_gimbal` 的坐标系，由 `R_camera2gimbal` 从相机帧变换而来。

**在未配置外参（`R_camera_to_gimbal`）或外参为默认 Y 轴翻转时**，云台帧与相机帧**轴向一致**（仅 Y 被翻转，使 Y 向上为正）：

| 轴 | 方向 | 代码依据 |
|---|---|---|
| X | 朝右 | `yaw = atan2(x, z)` |
| Y | 朝下（相机惯例）或朝上（Y 翻转后） | `pitch = atan2(-y, z)`，-y=上 |
| Z | 朝前（正值=目标在前方） | z 为分母=深度 |

> **关键**：无论是否翻转 Y，**Z 轴始终朝前**（正深度方向）。

**yaw/pitch 角计算确认（`pnp_solver.cpp`）：**
```cpp
detection.yaw_angle   = std::atan2(xyz_in_gimbal.x(), xyz_in_gimbal.z());
//  ^ atan2(右, 前) —— 水平偏角，右正左负

detection.pitch_angle = std::atan2(-xyz_in_gimbal.y(), xyz_in_gimbal.z());
//  ^ atan2(上, 前) —— 俯仰角，上正下负，-y 修正 Y 朝下
```

---

### 1.3 世界帧（`world`）

Foxglove 3D 面板默认的渲染坐标系，采用 **Z-up 右手系**（与 ROS REP-103 兼容）。

```
       Z（上）
       |
       |
       O ——→ X（前）
      /
     /
    Y（左）
```

| 轴 | 方向 |
|---|---|
| X | 朝前（场地纵深） |
| Y | 朝左 |
| Z | 朝上 |

---

## 二、坐标帧树

```
world (Z-up)
  │
  │ world → gimbal
  │ [PublishTransform("world", "gimbal", GimbalTransform(yaw, pitch))]
  │
  └─► gimbal (Z-forward, OpenCV 惯例)
         │
         │ gimbal → camera
         │ [PublishTransform("gimbal", "camera", R_c2g^T)]（静态，Init 时发布一次）
         │
         └─► camera (Z-forward)
```

---

## 三、已知问题：3D 可视化方向错误

### 3.1 现象

Foxglove 3D 面板中，装甲板目标**从正上方俯冲而来**，而实际上目标是**在地平面沿 Z 轴（前方）接近**。

### 3.2 根本原因

`GimbalTransform()` 在 yaw=0, pitch=0 时返回 **Identity 矩阵**，导致：

```
gimbal 帧 ≡ world 帧（在零偏角时）
```

而这两个帧的 Z 轴**方向完全相反**：

| 帧 | Z 轴方向 |
|---|---|
| `gimbal` | 朝前（深度，OpenCV 惯例） |
| `world` | 朝上（Z-up 惯例） |

**数值示例：**
```
装甲板在 gimbal 帧：position = (x=0, y=0, z=5.0)  ← 正前方 5 米
Foxglove 渲染时：   world 坐标 = (0, 0, 5.0)       ← 正上方 5 米（世界 Z=上）
```

### 3.3 `GimbalTransform` 代码分析

```cpp
// foxglove_vision_test.cpp
Eigen::Matrix4d GimbalTransform(double yaw_rad, double pitch_rad) {
  T.block<3,3>(0,0) =
    AngleAxisd(yaw_rad,  UnitZ) *   // 绕 世界Z（上） 偏航
    AngleAxisd(-pitch_rad, UnitY);  // 绕 世界Y（左） 俯仰
  return T;
}
```

该函数**隐含了 world 坐标系中 X=前、Y=左、Z=上 的假设**，但 gimbal 帧实际
是 X=右、Y=下、Z=前（OpenCV 惯例），两者之间缺少一个从 Z-up 到 Z-forward
的基底旋转。

---

## 四、修复方案

### 方案 A（推荐）：在 `world→gimbal` 中补充基底旋转

在 `GimbalTransform` 中加入 **绕 X 轴旋转 -90°** 的固定基底变换
（将 world 的 Z-up 对齐到 gimbal 的 Z-forward）：

```
R_fix：绕 X 旋转 -90°
  world Z(上) → gimbal Z(前)
  world Y(左) → gimbal Y(上→翻转后适配)
```

修改后的逻辑（伪代码）：

```cpp
Eigen::Matrix4d GimbalTransform(double yaw_rad, double pitch_rad) {
  // 基底旋转：将 world Z-up 坐标系对齐到 gimbal Z-forward 坐标系
  // 绕 X 轴旋转 -90°：Z-up → Z-forward；Y-left → Y-up（与 PnP 翻转后一致）
  static const Eigen::Matrix3d R_FIX =
      Eigen::AngleAxisd(-M_PI / 2.0, Eigen::Vector3d::UnitX()).toRotationMatrix();

  Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
  T.block<3, 3>(0, 0) =
      (Eigen::AngleAxisd(yaw_rad, Eigen::Vector3d::UnitZ()) *
       Eigen::AngleAxisd(-pitch_rad, Eigen::Vector3d::UnitY()))
          .toRotationMatrix() *
      R_FIX;
  return T;
}
```

**修复后效果：**
```
装甲板 gimbal 坐标 (0, 0, 5) → Foxglove 渲染为正前方 5 米（沿地平面接近）
```

### 方案 B：在 Foxglove 面板设置中指定 "Up" 轴为 Y

3D 面板 → 设置 → **Display frame** 选 `gimbal` → **Up axis** 改为 `+Y` 或 `-Y`，
让 Foxglove 以 gimbal 本身的 Y 轴为"上"。  
**缺点**：world 帧的网格面板会跟着倾斜，视觉上不直观，且每次重连客户端后需重设。

### 方案 C：发布装甲板位置时先转换到 world 坐标系

在 `detection_publisher.cpp` 和 `pnp_visualizer.cpp` 发布 SceneEntity 时，
将 `entity.frame_id` 改为 `"world"`，并在发布前将 `xyz_in_gimbal` 显式乘以
`R_gimbal2world` 旋转矩阵。  
**缺点**：需要将 world→gimbal 变换传递到子发布器，架构耦合度增加。

**推荐使用方案 A**，改动最小（仅修改一个函数），不影响 PnP 计算结果。

---

## 五、坐标系变换链一览

```
                    ┌───────────────────────────────────────────────────────┐
                    │               物理含义                                │
变换                │  输入帧          输出帧       R 矩阵来源               │
────────────────────┼───────────────────────────────────────────────────────┤
camera → gimbal     │  相机 (Z-fwd)    云台 (Z-fwd) R_camera2gimbal (yaml)  │
gimbal → world      │  云台 (Z-fwd)    世界 (Z-up)  GimbalTransform()        │
pnp_solver 输出     │  —               gimbal 帧    xyz_in_gimbal            │
Foxglove 渲染       │  "gimbal" 帧     屏幕         TF 树自动组合             │
                    └───────────────────────────────────────────────────────┘
```

---

## 六、Foxglove 3D 面板配置建议

| 设置项 | 推荐值 | 说明 |
|---|---|---|
| Display frame | `world` | 以世界坐标系为渲染基准 |
| Up axis | `+Z` | 与 world 帧 Z-up 惯例一致 |
| Follow mode | `No follow` 或 `Position only` | 目标移动时相机不随动 |
| /tf topic | `/tf` | 勾选所有来源 |

---

## 七、calibration.yaml 外参说明

`R_camera_to_gimbal`（3×3，行优先存储）将相机坐标变换到云台坐标：

```yaml
calibration:
  # R_c2g 的行优先元素（9 个值）
  # 默认 Y 翻转：[[1,0,0],[0,-1,0],[0,0,1]]
  R_camera_to_gimbal: [1.0, 0.0, 0.0,
                        0.0,-1.0, 0.0,
                        0.0, 0.0, 1.0]
  t_camera_to_gimbal: [0.0, 0.0, 0.0]  # 相机与云台旋转中心重合时为零向量
```

**Y 轴翻转的含义**：相机 Y 向下 → 云台 Y 向上，满足 `pitch = atan2(-y, z) > 0` 表示"目标偏上"的直觉。
