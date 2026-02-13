# EX_MiracleVision - 快速开始指南

**项目**: BJFU RoboMaster 2026 视觉系统  
**日期**: 2026年2月13日

---

## 📊 当前状态

🔄 **vcpkg 正在配置中** - 41/115 包已安装（36%）

预计完成时间：约 20-40 分钟

---

## 🚀 后续步骤

### 1. 监控构建进度

**方法 A: 实时监控（推荐）**
```bash
cd ~/Desktop/EX_MiracleVision
watch -n 5 ./check_vcpkg_progress.sh
```
按 `Ctrl+C` 退出监控

**方法 B: 手动检查**
```bash
./check_vcpkg_progress.sh
```

**方法 C: 查看详细日志**
```bash
tail -f cmake_final_config.log
# 或
tail -f build/vcpkg-manifest-install.log
```

---

### 2. 等待配置完成

**成功标志**:
```
-- Running vcpkg install - done
-- Configuring done
-- Generating done
-- Build files have been written to: /home/prototype152/Desktop/EX_MiracleVision/build
```

**预计时间**: 30-60 分钟（首次构建）

---

### 3. 编译项目

配置完成后，运行：

```bash
cd ~/Desktop/EX_MiracleVision
cmake --build build -j$(nproc)
```

**编译时间**: 约 5-10 分钟

---

### 4. 运行程序

```bash
cd ~/Desktop/EX_MiracleVision
./build/MiracleVision
```

---

## ⚠️ 如果遇到问题

### 网络下载失败

**症状**: 看到 `curl: (35) SSL error` 或下载超时

**解决**:
```bash
# 1. 查看是哪个包失败
tail -50 build/vcpkg-manifest-install.log

# 2. 手动下载（示例：下载某个包）
cd /home/prototype152/vcpkg/downloads
wget <失败的URL>

# 3. 清理并重试
cd ~/Desktop/EX_MiracleVision
rm -rf build/
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic-safe \
    -DVCPKG_OVERLAY_TRIPLETS=triplets
```

### 某个包编译失败

**症状**: `error: building <package> failed with: BUILD_FAILED`

**解决**:
```bash
# 1. 查看错误日志
cat /home/prototype152/vcpkg/buildtrees/<package>/*-err.log

# 2. 清理该包
rm -rf /home/prototype152/vcpkg/buildtrees/<package>

# 3. 重新配置
cd ~/Desktop/EX_MiracleVision
rm -rf build/
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic-safe \
    -DVCPKG_OVERLAY_TRIPLETS=triplets
```

### 配置卡住不动

**症状**: 进度长时间（超过10分钟）没有变化

**检查**:
```bash
# 查看是否还在运行
ps aux | grep cmake

# 查看最新日志
tail -20 cmake_final_config.log
```

如果确实卡住了：
```bash
# 1. 停止进程
pkill -9 cmake

# 2. 清理并重启
cd ~/Desktop/EX_MiracleVision
rm -rf build/
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic-safe \
    -DVCPKG_OVERLAY_TRIPLETS=triplets
```

---

## � 系统依赖

在配置 vcpkg 之前，确保已安装以下系统工具：

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    zip unzip tar \
    pkg-config \
    bison \
    flex \
    autoconf \
    autoconf-archive \
    automake \
    libtool \
    ninja-build
```

**或使用一键配置脚本**（推荐）：
```bash
./setup_vcpkg.sh
```

---

## �📝 配置参数说明

### 当前使用的配置

```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic-safe \
    -DVCPKG_OVERLAY_TRIPLETS=triplets
```

**参数说明**:
- `-B build`: 构建目录
- `-S .`: 源代码目录
- `-DCMAKE_TOOLCHAIN_FILE`: vcpkg 工具链文件（必需）
- `-DCMAKE_BUILD_TYPE=Release`: 发布版本（优化）
- `-DVCPKG_TARGET_TRIPLET=x64-linux-dynamic-safe`: 自定义 triplet（避免 GCC 崩溃）
- `-DVCPKG_OVERLAY_TRIPLETS=triplets`: 自定义 triplet 目录

### 项目选项（可选）

```bash
# 构建选项
-DBUILD_MAIN=ON          # 构建主程序（默认开启）
-DBUILD_TESTS=OFF        # 构建测试程序（默认关闭）

# 第三方库选项
-DUSE_MINDVISION_SDK=ON  # 使用 MindVision 相机（默认开启）
-DUSE_FOXGLOVE_SDK=ON    # 使用 Foxglove 发布器（默认开启）
-DUSE_OPENVINO=OFF       # 使用 OpenVINO（默认关闭）
-DUSE_ONNXRUNTIME=OFF    # 使用 ONNX Runtime（默认关闭）
```

示例：
```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic-safe \
    -DVCPKG_OVERLAY_TRIPLETS=triplets \
    -DBUILD_TESTS=ON \
    -DUSE_OPENVINO=ON
```

---

## 📚 详细文档

查看完整文档以了解更多信息：

- **VCPKG_SETUP_STATUS.md** - 详细的配置状态报告
- **docs/VCPKG_CONFIGURATION_GUIDE.md** - 完整配置指南
- **docs/VCPKG_ISSUES_SUMMARY.md** - 已知问题和解决方案
- **CMakeLists.txt** - 项目构建配置

---

## 🎯 关键文件位置

```
EX_MiracleVision/
├── build/                              # 构建输出目录
│   ├── MiracleVision                   # 主程序（编译完成后）
│   ├── vcpkg-manifest-install.log      # vcpkg 安装日志
│   └── vcpkg_installed/                # 已安装的依赖
├── configs/                            # 配置文件
│   ├── auto_aim.yaml                   # 自瞄配置
│   └── camera/                         # 相机配置
├── triplets/
│   └── x64-linux-dynamic-safe.cmake    # 自定义 triplet
├── vcpkg.json                          # 依赖清单
├── CMakeLists.txt                      # 主构建配置
├── cmake_final_config.log              # 最新配置日志
└── check_vcpkg_progress.sh             # 进度监控脚本
```

---

## 💡 有用的命令

```bash
# 查看构建进度
./check_vcpkg_progress.sh

# 实时监控
watch -n 5 ./check_vcpkg_progress.sh

# 查看已安装的包
ls /home/prototype152/vcpkg/packages/

# 查看下载的源码包
ls /home/prototype152/vcpkg/downloads/

# 查看构建日志
tail -f cmake_final_config.log

# 清理构建（重新开始）
rm -rf build/

# 完全重新配置
rm -rf build/ && cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic-safe \
    -DVCPKG_OVERLAY_TRIPLETS=triplets
```

---

## ✨ 配置完成后

一旦看到以下消息：
```
-- Configuring done
-- Generating done
-- Build files have been written to: ...
```

就可以开始编译了：
```bash
cmake --build build -j$(nproc)
```

编译成功后，运行：
```bash
./build/MiracleVision
```

---

**祝编译顺利！** 🚀

如有问题，查看 **VCPKG_SETUP_STATUS.md** 获取详细的故障排除指南。
