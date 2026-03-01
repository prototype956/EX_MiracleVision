# HAL 硬件抽象层使用文档

本文档介绍 `src/hal/` 目录下硬件抽象层（Hardware Abstraction Layer）的技术方案、设计原因及使用方法。

---

## 1. 为什么需要 HAL？

原始代码（`devices/`）存在以下问题：

| 问题 | 具体表现 | 影响 |
|------|---------|------|
| SDK 渗透业务层 | `CameraApi.h`、`termios.h` 出现在算法模块 | 修改相机型号需要改动多处业务代码 |
| 无法脱离硬件测试 | 测试装甲板检测必须接上真实相机 | CI 无硬件时无法跑任何测试 |
| 全局变量 ODR 冲突 | `kal`（Kalman 滤波器）定义在 `.hpp` | 多个编译单元各生成一份副本，链接时 ODR 违反 |
| 配置耦合 OpenCV | 串口配置用 `cv::FileStorage` 读取 | 不用相机的场景也必须链接 OpenCV |
| 协议与 I/O 混写 | 帧格式、CRC、数据结构与 `read/write` 在同一个类 | 协议逻辑无法单独测试 |

HAL 的核心思路：**上层只看接口，下层只看硬件，通过工厂注入桥接两者**。

```
┌─────────────────────────────────────┐
│  业务层 (Pipeline / Algorithm)       │  只知道 ICamera / ISerial
├─────────────────────────────────────┤
│  HAL 接口层 (ICamera / ISerial)      │  纯虚接口，零硬件依赖
├──────────────┬──────────────────────┤
│ MindVision   │  OpenCvCamera        │  各自封装 SDK 细节
│ Camera (SDK) │  (V4L2/文件/RTSP)    │
├──────────────┴──────────────────────┤
│  硬件 / 驱动层                       │
└─────────────────────────────────────┘
```

---

## 2. 目录结构

```
src/hal/
├── CMakeLists.txt
├── camera/
│   ├── CMakeLists.txt
│   ├── i_camera.hpp           ← ICamera 纯虚接口
│   ├── mindvision_camera.hpp  ← MindVision 工业相机声明（Pimpl）
│   ├── mindvision_camera.cpp  ← 实现（含无 SDK 时的桩实现）
│   ├── opencv_camera.hpp      ← OpenCV 相机声明（Pimpl）
│   └── opencv_camera.cpp      ← 实现（USB / 文件 / RTSP）
└── serial/
    ├── CMakeLists.txt
    ├── i_serial.hpp           ← ISerial 纯虚接口
    ├── uart_serial.hpp        ← UART 串口声明（Pimpl）
    └── uart_serial.cpp        ← Linux termios 实现
```

**两个独立静态库：**
- `mv-hal-camera`：相机 HAL，依赖 `mv-core` + `opencv_core`
- `mv-hal-serial`：串口 HAL，依赖 `mv-core`，零外部依赖

---

## 3. ICamera 相机接口

### 3.1 接口定义

```cpp
// src/hal/camera/i_camera.hpp
class ICamera {
 public:
  virtual bool Open(const YAML::Node& config) = 0;  // 打开相机
  virtual void Close() = 0;                          // 关闭（幂等）
  virtual bool Grab(cv::Mat& frame) = 0;             // 取一帧（阻塞，有超时）
  [[nodiscard]] virtual bool IsOpen() const = 0;     // 查询状态
};
```

### 3.2 MindVisionCamera（工业相机）

**YAML 配置字段：**

```yaml
camera:
  exposure_us: 5000       # 曝光时间（微秒）
  resolution:
    width:  1280          # 图像宽度
    height: 800           # 图像高度
  channel: 3              # 通道数（3=BGR，1=灰度）
```

**使用示例：**

```cpp
#include "hal/camera/mindvision_camera.hpp"

auto cam = std::make_unique<mv::hal::MindVisionCamera>();

// config 来自 ConfigManager
YAML::Node cam_cfg = mv::ConfigManager::Instance().Subtree("camera");
if (!cam->Open(cam_cfg)) {
    MV_LOG_ERROR("main", "工业相机打开失败，退出");
    return -1;
}

cv::Mat frame;
while (running) {
    if (!cam->Grab(frame)) {
        MV_LOG_WARN("main", "取帧失败，跳过本帧");
        continue;
    }
    process(frame);
}
cam->Close();  // 可选，析构时自动调用
```

> **注意**：`MindVisionCamera` 在 `MV_HAS_MVSDK` 未定义时编译为桩实现（`Open()` 始终返回 `false`），无需任何 `#ifdef`，程序可在无硬件机器上正常编译运行。

### 3.3 OpenCvCamera（USB / 文件 / RTSP）

**YAML 配置字段：**

```yaml
camera:
  source: 0       # 设备索引（int）或路径/URL（string）
                  # 示例：0、"/dev/video0"、"video/test.mp4"、"rtsp://..."
  width:  1280    # 期望宽度（可选，硬件不支持时会降级）
  height: 720     # 期望高度（可选）
  fps:    60      # 期望帧率（可选，仅作 hint）
```

**使用示例：**

```cpp
#include "hal/camera/opencv_camera.hpp"

auto cam = std::make_unique<mv::hal::OpenCvCamera>();
YAML::Node cam_cfg = mv::ConfigManager::Instance().Subtree("camera");

if (!cam->Open(cam_cfg)) {
    MV_LOG_ERROR("main", "OpenCV 相机/视频打开失败");
    return -1;
}

cv::Mat frame;
while (running && cam->IsOpen()) {
    if (!cam->Grab(frame)) {
        break;  // 视频文件播放完毕或设备断开
    }
    process(frame);
}
```

**离线调试（播放录制视频）：**

```yaml
camera:
  source: "video/test.mp4"  # 直接用视频文件路径
```

---

## 4. ISerial 串口接口

### 4.1 接口定义

```cpp
// src/hal/serial/i_serial.hpp
class ISerial {
 public:
  virtual bool Open(const YAML::Node& config) = 0;
  virtual void Close() = 0;
  virtual bool Send(const uint8_t* data, std::size_t len) = 0;
  virtual bool Recv(uint8_t* buf, std::size_t len, std::size_t& received) = 0;
  [[nodiscard]] virtual bool IsOpen() const = 0;
};
```

> **注意**：ISerial 只负责**字节流 I/O**，不知道任何帧格式和协议。
> 协议层（帧打包/解包/CRC/Kalman 滤波）将在 Stage 3 的 `mv-protocol` 模块中实现。

### 4.2 UartSerial（Linux termios）

**YAML 配置字段：**

```yaml
serial:
  device: "/dev/ttyUSB0"     # 首选设备节点
  baudrate: 115200            # 115200 / 921600（其他值降级到 115200）
  fallback_devices:           # 可选：依次尝试的备选端口
    - "/dev/ttyUSB1"
    - "/dev/ttyACM0"
    - "/dev/ttyACM1"
```

**使用示例：**

```cpp
#include "hal/serial/uart_serial.hpp"

auto serial = std::make_unique<mv::hal::UartSerial>();
YAML::Node serial_cfg = mv::ConfigManager::Instance().Subtree("serial");

if (!serial->Open(serial_cfg)) {
    MV_LOG_ERROR("main", "串口打开失败");
    return -1;
}

// 发送（协议层负责打包，这里只传字节数组）
std::array<uint8_t, 15> send_buf = pack_frame(yaw, pitch, shoot);
serial->Send(send_buf.data(), send_buf.size());

// 接收（非阻塞，收到 0 字节也不是错误）
std::array<uint8_t, 14> recv_buf{};
std::size_t received = 0;
if (serial->Recv(recv_buf.data(), recv_buf.size(), received) && received > 0) {
    parse_frame(recv_buf.data(), received);
}
```

**非阻塞 Recv 的使用模式：**

```cpp
// Pipeline 线程中的推荐写法：定期尝试收数据，同时检查退出标志
while (running_) {
    std::size_t received = 0;
    if (serial_->Recv(buf_.data(), buf_.size(), received) && received > 0) {
        ring_buf_.Push(buf_.data(), received);  // 送入成帧缓冲区
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 避免空转
}
```

---

## 5. 设计细节

### 5.1 Pimpl 模式

所有具体实现类（`MindVisionCamera`、`OpenCvCamera`、`UartSerial`）均采用 Pimpl 模式：

```cpp
// .hpp 中：只有前向声明，对 SDK 零依赖
class MindVisionCamera : public ICamera {
  struct Impl;                       // 前向声明，不暴露任何 SDK 类型
  std::unique_ptr<Impl> impl_;       // 持有 Impl，析构自动释放
};

// .cpp 中：Impl 包含所有 SDK 类型
struct MindVisionCamera::Impl {
  tSdkCameraDevInfo dev_info{};      // SDK 类型完全隔离在这里
  // ...
};
```

**好处：**
- 修改 SDK 用法只重编一个 `.cpp`，不触发依赖它的所有模块重编
- 没有 MindVision SDK 的 CI 机器可以正常编译整个项目（桩实现）

### 5.2 MindVision 桩实现

`MV_HAS_MVSDK` 由 CMake 控制（有 SDK 时自动定义）：

```cmake
# src/hal/camera/CMakeLists.txt
if(USE_MINDVISION_SDK AND TARGET MVSDK)
    target_compile_definitions(mv-hal-camera PRIVATE MV_HAS_MVSDK)
    target_link_libraries(mv-hal-camera PRIVATE MVSDK)
endif()
```

无 SDK 时，`Open()` / `Grab()` 返回 `false` 并打印日志，工厂注册表仍可无条件包含 `"mindvision"` 类型，运行时再决定是否可用。

### 5.3 非阻塞串口设计

`UartSerial` 使用 `VTIME=0, VMIN=0` 的 termios 配置（非阻塞 `read`），而不是阻塞等待：

```
有数据 → read() 立即返回实际字节数
无数据 → read() 立即返回 0（EAGAIN / EWOULDBLOCK）
```

**原因**：Pipeline 线程需要定期检查退出标志 `running_`，如果 `read()` 永远阻塞，线程无法干净退出。上层负责凑齐完整帧（Stage 3 的 `SerialProtocol` 处理）。

---

## 6. CMake 集成

在新模块的 `CMakeLists.txt` 中：

```cmake
# 使用相机 HAL
target_link_libraries(your_module PRIVATE mv-hal-camera)

# 使用串口 HAL
target_link_libraries(your_module PRIVATE mv-hal-serial)

# 同时使用两者
target_link_libraries(your_module
    PRIVATE
        mv-hal-camera
        mv-hal-serial
)
```

> `mv-hal-camera` 已传递依赖 `mv-core`（Logger + ConfigManager），无需重复声明。

---

## 7. 最佳实践

1. **始终通过 `unique_ptr<ICamera>` 持有相机对象**，不要用具体类型，保留运行时替换的能力：
   ```cpp
   std::unique_ptr<mv::hal::ICamera> cam_;  // ✓
   mv::hal::MindVisionCamera cam_;          // ✗ 锁死了实现
   ```

2. **`Open()` 失败不要抛异常，检查返回值**：
   ```cpp
   if (!cam_->Open(cfg)) { /* 处理失败，记录日志，优雅退出 */ }
   ```

3. **不要在 HAL 层之上包含任何 SDK 头文件**：`CameraApi.h`、`termios.h` 只允许出现在 `src/hal/` 的 `.cpp` 文件中。

4. **`Recv()` 是非阻塞的，调用方负责重试和超时逻辑**，不要期望一次 `Recv()` 就能收到完整帧。

5. **`Close()` 幂等，析构自动调用**，不必手动调用，但在已知可以提前释放资源时手动调用也没问题。
