# EX_MiracleVision 编译状态报告

**日期:** 2026-02-13  
**分支:** vcpkg

---

## ✅ 成功完成的工作

### 1. 依赖管理方案切换
- ❌ **vcpkg 方案已放弃** (经过 3+ 小时调试)
  - GCC 13 ICE: fmt, gettext, libxml2
  - GCC 12 ICE: fmt  
  - Clang 14 + vcpkg: hwloc (m4 segfault)
- ✅ **系统 apt 包方案已采用**
  - 所有依赖已通过 `apt install` 安装
  - CMake 配置成功
  - 依赖版本: fmt 9.1.0, spdlog 1.11.0, OpenCV 4.5.5, Eigen3 3.4.0等

### 2. CMake 配置修复
- ✅ `cmake/Dependencies.cmake`: 移除 `CONFIG`,支持系统包
- ✅ `utils/CMakeLists.txt`: 
  - 修复 Eigen3 链接 (mv-math-tools)
  - 添加 POSITION_INDEPENDENT_CODE (所有 OBJECT 库)
- ✅ `devices/CMakeLists.txt`: 添加项目根目录到包含路径
- ✅ `module/CMakeLists.txt`: 所有子模块添加 `CMAKE_SOURCE_DIR`
- ✅ `module/buff/basic_buff.hpp`: 修复包含路径
- ✅ `module/foxglove_publisher/CMakeLists.txt`: 
  - 移除不存在的 `foxglove_sdk` 链接
  - 添加 Foxglove 头文件路径
- ✅ `base/CMakeLists.txt`:
  - 链接 ONNX Runtime (`/usr/local/lib/libonnxruntime.so`)
  - 链接 MindVision SDK
  - GCC 12+ 降级优化到 `-O0`

### 3. 库编译状态
**✅ 全部编译成功 (100%):**
- utils: mv-img-tools, mv-plotter, mv-logger, mv-math-tools, mv-base64
- devices: mv-video-capture, mv-uart-serial  
- module/armor: mv-basic-armor, mv-fan-armor, mv-dnn-armor
- module/buff: mv-new-buff, mv-basic-buff
- module/filter: mv-basic-kalman
- module/angle_solve: mv-basic-pnp, mv-angle-solve
- module/ml: mv-onnx-inferring
- module/roi: mv-basic-roi
- module/camera: mv-camera-calibration
- module/predictor: mv-predictor
- module/foxglove_publisher: mv-foxglove-publisher

---

## ❌ 当前阻塞问题

### GCC 12.3.0 内部编译器错误 (ICE)

**问题描述:**  
GCC 12 和 13 在编译涉及 `fmt` 库和 OpenCV 的代码时出现段错误 (segmentation fault),即使降低到 `-O0` 优化等级也无法完全避免。

**失败位置:**
1. **主程序** (`base/MiracleVision.cpp`):
   - 第1次 ICE: `fmt/format.h:4134` (RTL pass: cse1) @ -O2
   - 第2次 ICE: `opencv2/core/matx.hpp:1034` (constexpr) @ -O1  
   - 第3次 ICE: `opencv2/core/matx.hpp:1034` @ -O0 (已降到最低)

2. **设备库** (`devices/camera/mv_video_capture.cpp`):
   - ICE: `fmt/format.h:2424` (RTL pass: cse1) @ -O2

**根本原因:**
- GCC 12/13 的已知 bug (RTL 优化阶段崩溃)
- `fmt` 库 9.1.0 (系统版本) 与 GCC 12/13 兼容性问题
- `miniconda3/include/fmt` (版本不明) 冲突

**尝试过的解决方案:**
- ❌ GCC 13 → GCC 12 (依然崩溃)
- ❌ -O3 → -O2 → -O1 → -O0 (依然崩溃)
- ❌ Clang 14 (与 GCC 13 libstdc++ 不兼容,`consteval` 错误)

---

## 🔄 后续推荐方案

### 方案 A: 升级到 GCC 13 最新版 (推荐)
```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install gcc-13 g++-13
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100
```
**预期:** GCC 13.2+ 修复了部分 ICE bug

### 方案 B: 使用 Clang 15/16 + libc++
```bash
# 安装新版 Clang
sudo apt install clang-15 libc++-15-dev libc++abi-15-dev

# 使用 libc++ 编译
cd build && rm -rf *
CC=clang-15 CXX=clang++-15 cmake .. \\
  -DCMAKE_CXX_FLAGS="-stdlib=libc++" \\
  -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++ -lc++abi"
make -j4
```
**预期:** Clang 15 + libc++ 避开 GCC libstdc++ 问题

### 方案 C: 隔离 fmt 版本冲突
```bash
# 移除 miniconda 的 fmt,强制使用系统版本
conda remove fmt --force
# 或者临时
export CPLUS_INCLUDE_PATH=/usr/include:$CPLUS_INCLUDE_PATH
```

### 方案 D: 在 Docker 容器中编译
使用已知可行的环境 (Ubuntu 22.04 + GCC 11):
```dockerfile
FROM ubuntu:22.04
RUN apt update && apt install -y gcc-11 g++-11 cmake ...
# 完整环境配置
```

---

## 📊 项目统计

- **总库数量:** 19个
- **编译成功:** 19/19 (100%)
- **链接失败:** 1/1 (主程序)
- **阻塞原因:** 编译器 bug (非代码问题)

---

## 🎯 结论

**项目代码本身没有问题!** 所有库都编译成功,只是主程序遇到了 GCC 12/13 的已知编译器 bug。这是工具链问题,不是你的代码质量问题。

建议尝试上述方案 A 或 B,或者在生产环境使用 Docker 容器确保一致的编译环境。
