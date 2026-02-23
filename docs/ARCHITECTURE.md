# 架构文档

本文档详细说明 EX_MiracleVision 的项目架构、模块设计和代码组织。

---

## 📋 目录

- [系统架构](#系统架构)
- [模块说明](#模块说明)
- [数据流](#数据流)
- [设计模式](#设计模式)
- [目录结构](#目录结构)

---

## 🏛️ 系统架构

### 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                        Main Program                         │
│                   (base/MiracleVision.cpp)                  │
└───────────────────┬─────────────────────────────────────────┘
                    │
        ┌───────────┴───────────┐
        │                       │
┌───────▼────────┐      ┌──────▼──────┐
│  Device Layer  │      │   Modules   │
│   (devices/)   │      │  (module/)  │
└───────┬────────┘      └──────┬──────┘
        │                      │
        │                      │
┌───────▼────────┐      ┌──────▼──────────┐
│   Camera       │      │ Armor Detection │
│   Serial       │      │ Buff Detection  │
└────────────────┘      │ Angle Solver    │
                        │ Predictor       │
                        │ Filter          │
                        └─────────────────┘
```

### 层次结构

1. **应用层 (Application Layer)**: `base/`
   - 主程序入口
   - 模块调度和协调
   - 整体流程控制

2. **算法层 (Algorithm Layer)**: `module/`
   - 装甲板检测 (armor)
   - 能量机关识别 (buff)
   - 姿态解算 (angle_solve)
   - 弹道预测 (predictor)
   - 滤波器 (filter)
   - ROI 管理 (roi)
   - 深度学习推理 (ml)

3. **设备层 (Device Layer)**: `devices/`
   - 相机驱动 (camera)
   - 串口通信 (serial)

4. **工具层 (Utility Layer)**: `utils/`
   - 日志系统 (logger)
   - 数学工具 (math_tools)
   - 图像工具 (img_tools)
   - 性能分析 (fps)

5. **第三方库 (Third-party)**: `3rdparty/`
   - MindVision SDK
   - Foxglove SDK
   - fmt 库

---

## 🔧 模块说明

### 1. 装甲板检测模块 (module/armor/)

**功能**: 识别和定位敌方装甲板

**组件**:
- `basic_armor`: 基于传统视觉的装甲板检测
  - 灯条检测
  - 灯条配对
  - 装甲板拟合
  - 颜色验证
- `fan_armor`: 扇形装甲板检测（哨兵模式）
- `DNN_armor`: 基于深度学习的装甲板检测

**主要类**:
```cpp
namespace basic_armor {
    class Detector {
        bool runBasicArmor(const cv::Mat& img, 
                          const uart::Receive_Data& data);
        bool fittingArmor();
        bool lightJudge(int i, int j);
        Armor_Data returnFinalArmor(int num);
    };
}
```

**数据结构**:
- `Armor_Data`: 装甲板信息（位置、大小、类型）
- `Armor_Config`: 检测参数配置
- `Light_Config`: 灯条参数配置

**算法流程**:
```
原图 → 预处理 → 灯条检测 → 灯条配对 → 装甲板拟合 → 最优选择
```

### 2. 能量机关模块 (module/buff/)

**功能**: 识别和击打能量机关

**组件**:
- `basic_buff`: 能量机关基础检测
- `new_buff`: 改进的能量机关算法
- `abstract_object`: 抽象对象基类

**主要特性**:
- 扇叶识别
- 中心 R 标识别
- 旋转预测
- 击打点计算

### 3. 姿态解算模块 (module/angle_solve/)

**功能**: 计算目标的三维位置和姿态

**组件**:
- `basic_pnp`: PnP 算法实现
- `angle_solve`: 角度解算主类

**算法**:
```cpp
namespace basic_pnp {
    class PnP {
        void solvePnP(const std::vector<cv::Point2f>& image_points,
                     const std::vector<cv::Point3f>& object_points);
        cv::Point3f getPosition();
    };
}
```

**坐标系**:
- 相机坐标系
- 世界坐标系
- 云台坐标系

### 4. 弹道预测模块 (module/predictor/)

**功能**: 预测目标位置并计算弹道补偿

**主要类**:
```cpp
namespace predictor {
    class Predictor {
        void predict(const Target& current_target,
                    float bullet_speed,
                    float gyro_data);
        cv::Point2f getPredictedPoint();
    };
}
```

**考虑因素**:
- 目标运动速度
- 子弹飞行时间
- 重力下坠
- 空气阻力
- 云台延迟

### 5. 滤波器模块 (module/filter/)

**功能**: 平滑目标轨迹，减少抖动

**组件**:
- `basic_kalman`: 卡尔曼滤波器实现

**状态向量**:
```
[x, y, vx, vy] 或 [x, y, z, vx, vy, vz]
```

### 6. ROI 模块 (module/roi/)

**功能**: 管理感兴趣区域，提高检测效率

**策略**:
- 基于上一帧目标位置缩小搜索区域
- 动态调整 ROI 大小
- ROI 失效后恢复全图搜索

### 7. 深度学习模块 (module/ml/)

**功能**: ONNX 模型推理

**组件**:
- `onnx_inferring`: ONNX Runtime 封装

**支持模型**:
- YOLOv5/YOLOv8 目标检测
- 自定义装甲板检测模型

### 8. 相机标定模块 (module/camera/)

**功能**: 相机内参标定

**组件**:
- `camera_calibration`: 标定工具

### 9. Foxglove 发布模块 (module/foxglove_publisher/)

**功能**: 实时数据可视化

**发布内容**:
- 原始图像
- 检测结果标注
- 目标位置数据
- 系统状态信息

---

## 📡 数据流

### 主程序流程

```
1. 初始化
   ├── 加载配置文件
   ├── 初始化相机
   ├── 初始化串口
   └── 初始化算法模块

2. 主循环
   ├── 读取图像帧
   ├── 读取串口数据
   │
   ├── 根据模式选择算法
   │   ├── AUTO_AIM: 自动瞄准
   │   ├── BUFF: 能量机关
   │   └── SENTRY: 哨兵模式
   │
   ├── 装甲板检测
   │   ├── 图像预处理
   │   ├── 灯条检测
   │   ├── 装甲板拟合
   │   └── 最优选择
   │
   ├── 姿态解算
   │   ├── PnP 求解
   │   └── 坐标转换
   │
   ├── 弹道预测
   │   ├── 目标追踪
   │   ├── 运动预测
   │   └── 弹道补偿
   │
   ├── 发送串口数据
   │   ├── 目标位置
   │   ├── 开火指令
   │   └── 状态信息
   │
   └── 数据可视化（可选）
       ├── Foxglove 发布
       └── 本地显示

3. 退出
   ├── 释放相机资源
   ├── 关闭串口
   └── 保存日志
```

### 串口通信协议

**接收数据**:
```cpp
struct Receive_Data {
    Mode mode;           // 工作模式
    Color my_color;      // 我方颜色
    float bullet_speed;  // 子弹速度
    float yaw_angle;     // 当前 yaw 角度
    float pitch_angle;   // 当前 pitch 角度
};
```

**发送数据**:
```cpp
struct Write_Data {
    int target_num;      // 目标数量
    bool fire;           // 开火标志
    node target_pos;     // 目标位置 (x, y)
    float yaw_angle;     // 目标 yaw 角度
    float pitch_angle;   // 目标 pitch 角度
};
```

---

## 🎨 设计模式

### 1. 单例模式

**Logger 系统**:
```cpp
class Logger {
    static Logger& getInstance();
private:
    Logger();
    Logger(const Logger&) = delete;
};
```

### 2. 工厂模式

**目标检测器创建**:
```cpp
std::unique_ptr<Detector> createDetector(DetectorType type) {
    switch(type) {
        case BASIC: return std::make_unique<BasicDetector>();
        case DNN: return std::make_unique<DNNDetector>();
    }
}
```

### 3. 策略模式

**图像预处理策略**:
```cpp
class PreprocessStrategy {
    virtual cv::Mat process(const cv::Mat& img) = 0;
};

class GrayPreprocess : public PreprocessStrategy { ... };
class ColorPreprocess : public PreprocessStrategy { ... };
```

### 4. 观察者模式

**Foxglove 数据发布**:
```cpp
class Publisher {
    void addSubscriber(Subscriber* sub);
    void notify(const Data& data);
};
```

---

## 📂 目录结构详解

```
EX_MiracleVision/
│
├── base/                           # 主程序
│   ├── MiracleVision.cpp          # 主函数，程序入口
│   ├── MiracleVision.hpp          # 主类定义
│   └── CMakeLists.txt             # 构建配置
│
├── module/                         # 算法模块
│   ├── armor/                     # 装甲板检测
│   │   ├── basic_armor.cpp       # 传统视觉检测实现
│   │   ├── basic_armor.hpp       # 接口定义
│   │   ├── fan_armor.cpp         # 扇形装甲板
│   │   ├── DNN_armor.cpp         # 深度学习检测
│   │   └── CMakeLists.txt        # 模块构建配置
│   │
│   ├── buff/                      # 能量机关
│   │   ├── basic_buff.cpp        # 基础能量机关
│   │   ├── new_buff.cpp          # 改进算法
│   │   ├── abstract_object.cpp   # 抽象基类
│   │   └── CMakeLists.txt
│   │
│   ├── angle_solve/               # 姿态解算
│   │   ├── basic_pnp.cpp         # PnP 算法
│   │   ├── angle_solve.cpp       # 角度解算主类
│   │   └── CMakeLists.txt
│   │
│   ├── predictor/                 # 弹道预测
│   │   ├── predictor.cpp         # 预测器实现
│   │   ├── predictor.hpp         # 接口定义
│   │   └── CMakeLists.txt
│   │
│   ├── filter/                    # 滤波器
│   │   ├── basic_kalman.cpp      # 卡尔曼滤波
│   │   └── CMakeLists.txt
│   │
│   ├── roi/                       # ROI 管理
│   │   ├── basic_roi.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── ml/                        # 深度学习
│   │   ├── onnx_inferring.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── camera/                    # 相机标定
│   │   ├── camera_calibration.cpp
│   │   └── CMakeLists.txt
│   │
│   ├── foxglove_publisher/        # 数据发布
│   │   ├── foxglove_publisher.cpp
│   │   └── CMakeLists.txt
│   │
│   └── CMakeLists.txt             # 模块总配置
│
├── devices/                        # 设备层
│   ├── camera/                    # 相机驱动
│   │   ├── video_capture.cpp     # 视频采集
│   │   ├── mv_video_capture.cpp  # MindVision 相机
│   │   └── video_capture.hpp     # 接口定义
│   │
│   ├── serial/                    # 串口通信
│   │   ├── uart_serial.cpp       # 串口实现
│   │   ├── uart_serial.hpp       # 接口定义
│   │   └── crc.cpp               # CRC 校验
│   │
│   ├── command.hpp                # 通信协议定义
│   └── CMakeLists.txt
│
├── utils/                          # 工具库
│   ├── logger.cpp                 # 日志系统
│   ├── logger.hpp
│   ├── math_tools.cpp             # 数学工具
│   ├── math_tools.hpp
│   ├── img_tools.cpp              # 图像处理工具
│   ├── img_tools.hpp
│   ├── fps.hpp                    # 帧率统计
│   ├── plotter.cpp                # 数据绘图
│   ├── base64.cpp                 # Base64 编解码
│   ├── debug_tools.hpp            # 调试工具
│   ├── thread_safe_queue.hpp     # 线程安全队列
│   └── CMakeLists.txt
│
├── 3rdparty/                       # 第三方库
│   ├── mindvision/                # MindVision SDK
│   │   └── linux/
│   │       ├── include/
│   │       └── lib/
│   ├── foxglove/                  # Foxglove SDK
│   │   ├── include/
│   │   └── lib/
│   └── fmt/                       # fmt 库（源码）
│
├── configs/                        # 配置文件
│   ├── auto_aim.yaml              # 主配置
│   ├── armor/                     # 装甲板参数
│   │   └── armor_config_DEFAULT.xml
│   ├── buff/                      # 能量机关参数
│   ├── camera/                    # 相机参数
│   │   └── camera_param.yaml
│   ├── filter/                    # 滤波器参数
│   ├── ml/                        # 模型配置
│   ├── predictor/                 # 预测器参数
│   ├── roi/                       # ROI 参数
│   └── serial/                    # 串口配置
│
├── test/                           # 测试程序
│   ├── minimum_vision.cpp         # 最小测试程序
│   ├── foxglove_camera_sdk.cpp    # Foxglove 测试
│   └── CMakeLists.txt
│
├── cmake/                          # CMake 模块
│   ├── CompilerOptions.cmake      # 编译选项
│   ├── Dependencies.cmake         # 依赖查找
│   └── ThirdParty.cmake           # 第三方库配置
│
├── docs/                           # 文档
│   ├── ARCHITECTURE.md            # 本文档
│   ├── BUILD_GUIDE.md             # 构建指南
│   ├── ENVIRONMENT_SETUP.md       # 环境配置
│   ├── DEVELOPMENT.md             # 开发指南
│   └── ...
│
├── scripts/                        # 工具脚本
│   └── install_dependencies.sh    # 依赖安装脚本
│
├── video/                          # 测试视频
│   └── test.mp4
│
├── CMakeLists.txt                  # 主 CMake 文件
├── README.md                       # 项目说明
├── LICENSE                         # 开源许可证
└── .gitignore                      # Git 忽略规则
```

---

## 🔄 模块交互

```
┌─────────────┐
│   Main      │
└──────┬──────┘
       │
       ├──────► ┌────────────┐
       │        │  Camera    │
       │        └────────────┘
       │
       ├──────► ┌────────────┐
       │        │  Serial    │
       │        └────────────┘
       │
       ├──────► ┌────────────┐      ┌──────────┐
       │        │   Armor    │─────►│   PnP    │
       │        └────────────┘      └──────────┘
       │               │
       │               ├──────────► ┌──────────┐
       │               │            │ Kalman   │
       │               │            └──────────┘
       │               │
       │               └──────────► ┌──────────┐
       │                            │Predictor │
       │                            └──────────┘
       │
       └──────► ┌────────────┐
                │ Foxglove   │
                └────────────┘
```

---

## 📚 相关文档

- [构建指南](BUILD_GUIDE.md) - CMake 配置和编译
- [开发指南](DEVELOPMENT.md) - 开发规范和调试
- [环境配置](ENVIRONMENT_SETUP.md) - 环境搭建

---

**最后更新**: 2026-02-14  
**维护者**: [@prototype956](https://github.com/prototype956)
