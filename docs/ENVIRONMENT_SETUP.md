# 环境配置指南

本文档详细说明如何配置 EX_MiracleVision 的开发和运行环境。

---

## 📋 目录

- [系统要求](#系统要求)
- [依赖安装](#依赖安装)
- [第三方 SDK 配置](#第三方-sdk-配置)
- [环境验证](#环境验证)
- [常见问题](#常见问题)
- [Docker 部署](#docker-部署可选)

---

## 🖥️ 硬件配置
- **内存**: 16GB DDR4
- **硬盘**: PCIe4.0 NVMe 512G
- **相机**: MindVision 工业相机（推荐）
- **串口**: USB-TTL 
- **处理器**: i7-1260P/U5-125H

---

## 📦 依赖安装

### 方法 1: 自动化脚本（推荐）

```bash
cd EX_MiracleVision
chmod +x scripts/install_dependencies.sh
./scripts/install_dependencies.sh
```

脚本将自动：
1. 更新系统包列表
2. 安装所有必需的系统依赖
3. 验证安装是否成功
4. 输出版本信息

### 方法 2: 手动安装

#### 1. 更新系统包

```bash
sudo apt update
sudo apt upgrade -y
```

#### 2. 安装构建工具

```bash
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config
```

#### 3. 安装核心依赖

```bash
sudo apt install -y \
    libopencv-dev \
    libfmt-dev \
    libspdlog-dev \
    libeigen3-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libtbb-dev
```

#### 4. 验证安装

```bash
# 检查 GCC 版本
gcc --version  # 应该显示 11.4.0 或更高

# 检查 CMake 版本
cmake --version  # 应该显示 3.22 或更高

# 检查 OpenCV 版本
pkg-config --modversion opencv4  # 应该显示 4.5.4 或更高
```

### 依赖包版本信息

以下是 Ubuntu 22.04 系统包管理器提供的版本：

| 依赖库 | 系统版本 | 最低要求 |
|--------|----------|----------|
| OpenCV | 4.5.4 | 4.2+ |
| fmt | 8.1.1 | 8.0+ |
| spdlog | 1.9.2 | 1.8+ |
| Eigen3 | 3.4.0 | 3.3+ |
| yaml-cpp | 0.7.0 | 0.6+ |
| nlohmann-json | 3.10.5 | 3.9+ |
| TBB | 2021.5.0 | 2020+ |

---

## 🔧 第三方 SDK 配置

### 1. MindVision 相机 SDK

MindVision SDK 已包含在项目中（`3rdparty/mindvision/`），无需额外安装。

**验证 SDK**：
```bash
ls 3rdparty/mindvision/linux/lib/x64/libMVSDK.so
# 应该显示文件路径
```

**串口权限配置**：
```bash
# 将当前用户添加到 dialout 组（访问串口需要）
sudo usermod -a -G dialout $USER

# 重新登录生效，或临时生效：
newgrp dialout
```

### 2. Foxglove SDK

Foxglove SDK 用于 WebSocket 数据发布和可视化。

**验证 SDK**：
```bash
ls 3rdparty/foxglove/lib/libfoxglove.so
# 应该显示文件路径
```

**安装 Foxglove Studio（可选）**：
```bash
# 下载并安装 Foxglove Studio
# https://foxglove.dev/download
```

### 3. ONNX Runtime（可选）

用于深度学习模型推理。

**安装方法**：
```bash
# 方法 1: 从官方仓库下载预编译包
wget https://github.com/microsoft/onnxruntime/releases/download/v1.14.0/onnxruntime-linux-x64-1.14.0.tgz
tar -xzf onnxruntime-linux-x64-1.14.0.tgz
sudo cp -r onnxruntime-linux-x64-1.14.0/include/* /usr/local/include/
sudo cp -r onnxruntime-linux-x64-1.14.0/lib/* /usr/local/lib/
sudo ldconfig

# 方法 2: 使用系统包（如果可用）
# sudo apt install libonnxruntime-dev
```

**验证安装**：
```bash
ls /usr/local/lib/libonnxruntime.so
# 应该显示文件路径
```

---

## ✅ 环境验证

使用自动化脚本验证环境配置：

```bash
./scripts/install_dependencies.sh --check-only
```

或手动验证：

```bash
# 1. 编译器检查
gcc --version | grep "gcc (Ubuntu 11.4.0"

# 2. CMake 检查
cmake --version | grep "cmake version 3"

# 3. 依赖库检查
pkg-config --exists opencv4 && echo "✓ OpenCV OK"
pkg-config --exists fmt && echo "✓ fmt OK"
pkg-config --exists spdlog && echo "✓ spdlog OK"
pkg-config --exists eigen3 && echo "✓ Eigen3 OK"
pkg-config --exists yaml-cpp && echo "✓ yaml-cpp OK"

# 4. 第三方 SDK 检查
[ -f "3rdparty/mindvision/linux/lib/x64/libMVSDK.so" ] && echo "✓ MindVision SDK OK"
[ -f "3rdparty/foxglove/lib/libfoxglove.so" ] && echo "✓ Foxglove SDK OK"
```

---

## ❗ 常见问题

### 问题 1: GCC 版本不对

**症状**：
```
gcc: error: unrecognized command line option '-std=c++20'
```

**解决方案**：
```bash
# 检查 GCC 版本
gcc --version

# 如果版本 < 11.0，安装 GCC 11
sudo apt install gcc-11 g++-11

# 设置为默认编译器
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100
```

### 问题 2: 编译时出现 ICE（Internal Compiler Error）

**症状**：
```
internal compiler error: Segmentation fault
```

**原因**：GCC 12/13 在编译某些 C++20 特性时存在 bug

**解决方案**：
```bash
# 降级到 GCC 11
sudo apt install gcc-11 g++-11
sudo update-alternatives --config gcc  # 选择 gcc-11
sudo update-alternatives --config g++  # 选择 g++-11
```

### 问题 3: Conda 环境干扰

**症状**：
```
fatal error: spdlog/logger.h: No such file or directory
# 或者
internal compiler error in conda environment
```

**原因**：Conda 环境的头文件和系统头文件冲突

**解决方案**：
```bash
# 编译前停用 Conda 环境
conda deactivate

# 或者从 .bashrc 中移除 conda 初始化
# 编辑 ~/.bashrc，注释掉 conda initialize 部分
```

### 问题 4: OpenCV 找不到

**症状**：
```
CMake Error: Could not find OpenCV
```

**解决方案**：
```bash
# 重新安装 OpenCV
sudo apt install --reinstall libopencv-dev

# 检查 pkg-config 路径
export PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH

# 手动指定 OpenCV 路径
cmake -DOpenCV_DIR=/usr/lib/x86_64-linux-gnu/cmake/opencv4 ..
```

### 问题 5: 串口无权限

**症状**：
```
Error opening serial port: Permission denied
```

**解决方案**：
```bash
# 添加用户到 dialout 组
sudo usermod -a -G dialout $USER

# 重新登录或执行
newgrp dialout

# 或临时修改权限（不推荐）
sudo chmod 666 /dev/ttyUSB0
```

### 问题 6: 链接时找不到库

**症状**：
```
/usr/bin/ld: cannot find -lfmt
```

**解决方案**：
```bash
# 更新链接器缓存
sudo ldconfig

# 检查库文件
ls /usr/lib/x86_64-linux-gnu/libfmt*

# 如果不存在，重新安装
sudo apt install --reinstall libfmt-dev
```

---

## 🐳 Docker 部署（可选）

Docker 部署文档将在后续版本中提供。

### 当前推荐方案

1. **开发环境**: 直接在 Ubuntu 22.04 系统上配置
2. **生产环境**: 使用自动化脚本配置
3. **未来计划**: 提供 Docker 镜像和 docker-compose 配置

---

## 🔍 环境信息收集

如果遇到问题需要提交 Issue，请收集以下信息：

```bash
# 系统信息
uname -a
lsb_release -a

# 编译器信息
gcc --version
g++ --version
cmake --version

# 依赖库信息
pkg-config --modversion opencv4
pkg-config --modversion fmt
pkg-config --modversion spdlog
pkg-config --modversion eigen3

# 库路径信息
echo $LD_LIBRARY_PATH
echo $PKG_CONFIG_PATH

# CMake 配置日志
cat build/CMakeFiles/CMakeOutput.log | grep -A5 "Error"
```

---

## 📝 环境管理总结
### Conda 环境说明

- **Conda**: 用于运行其他队伍的项目，与本项目无关
- **建议**: 编译本项目时执行 `conda deactivate`
- **原因**: Conda 环境的某些库（如 spdlog, fmt）会与系统库冲突

### 最佳实践

1. ✅ **系统依赖**: 使用 apt 安装的系统包
2. ✅ **编译器**: GCC 11.4.0（避免使用 GCC 12/13）
3. ✅ **构建目录**: 始终使用 `build/` 目录（已在 .gitignore 中）
4. ⚠️ **Conda**: 编译前务必停用
5. ⚠️ **权限**: 串口设备需要 dialout 组权限

---

## 🔄 更新依赖

```bash
# 更新所有系统包
sudo apt update
sudo apt upgrade

# 重新编译项目
cd build
cmake ..
make clean
make -j4
```

---

## 📚 相关文档

- [构建指南](BUILD_GUIDE.md) - CMake 配置和编译
- [开发指南](DEVELOPMENT.md) - 开发环境设置
- [架构文档](ARCHITECTURE.md) - 项目结构说明

---

**最后更新**: 2026-02-14  
**维护者**: [@prototype956](https://github.com/prototype956)
