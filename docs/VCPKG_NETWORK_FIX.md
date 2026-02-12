# vcpkg 网络问题解决方案

## 问题描述

vcpkg 下载 fmt 包时出现 SSL 错误：
```
error: curl: (35) error:0A000126:SSL routines::unexpected eof while reading
```

---

## 解决方案

### 方案 1: 使用 vcpkg 镜像源（推荐）

#### 1.1 设置中国镜像

```bash
# 设置 vcpkg 使用中国镜像
export VCPKG_DOWNLOADS_MIRROR="https://mirrors.tuna.tsinghua.edu.cn/github-release"
export VCPKG_BINARY_SOURCES="clear;files,${HOME}/.cache/vcpkg/archives,readwrite"

# 重新配置
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

#### 1.2 使用清华镜像 vcpkg 端口仓库

```bash
# 克隆清华镜像的 vcpkg
cd ~
git clone https://mirrors.tuna.tsinghua.edu.cn/git/vcpkg.git vcpkg_tuna

# 设置环境变量
export VCPKG_ROOT=$HOME/vcpkg_tuna
export PATH=$VCPKG_ROOT:$PATH

# 重新配置项目
cd ~/桌面/EX_MiracleVision
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg_tuna/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

---

### 方案 2: 手动下载依赖包

```bash
# 创建下载目录
mkdir -p /home/prototype152/vcpkg/downloads

# 手动下载 fmt 10.2.1
cd /home/prototype152/vcpkg/downloads
wget https://github.com/fmtlib/fmt/archive/10.2.1.tar.gz -O fmtlib-fmt-10.2.1.tar.gz

# 如果 GitHub 不可访问，使用镜像
wget https://ghproxy.com/https://github.com/fmtlib/fmt/archive/10.2.1.tar.gz \
    -O fmtlib-fmt-10.2.1.tar.gz

# 重新运行 cmake
cd ~/桌面/EX_MiracleVision
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

---

### 方案 3: 配置代理（如果有）

如果你有代理服务器：

```bash
# HTTP 代理
export HTTP_PROXY=http://127.0.0.1:7890
export HTTPS_PROXY=http://127.0.0.1:7890

# SOCKS5 代理（需要转换）
# 推荐使用 proxychains
sudo apt-get install proxychains4

# 编辑 /etc/proxychains4.conf
# 添加：socks5 127.0.0.1 7891

# 使用代理运行 cmake
proxychains4 cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

---

### 方案 4: 使用系统包管理器（最简单）

**放弃 vcpkg**，直接使用 Ubuntu 的 apt：

```bash
# 安装所有依赖
sudo apt-get update
sudo apt-get install -y \
    libopencv-dev \
    libopencv-contrib-dev \
    libfmt-dev \
    libspdlog-dev \
    libeigen3-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libtbb-dev \
    libgtk-3-dev

# 配置项目（不使用 vcpkg）
cd ~/桌面/EX_MiracleVision
rm -rf build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)
```

**注意**：系统包版本可能较旧：
- Ubuntu 22.04 OpenCV: ~4.5.4
- 项目可能需要 OpenCV 4.8+

---

### 方案 5: 离线安装 vcpkg 包

如果有另一台能访问 GitHub 的机器：

#### 在有网络的机器上：

```bash
# 1. 克隆 vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh

# 2. 预先下载所有包
./vcpkg install fmt:x64-linux-dynamic \
    spdlog:x64-linux-dynamic \
    opencv4[ffmpeg,dnn,jpeg,png,contrib,tbb,eigen]:x64-linux-dynamic \
    eigen3:x64-linux-dynamic \
    yaml-cpp:x64-linux-dynamic \
    nlohmann-json:x64-linux-dynamic \
    tbb:x64-linux-dynamic

# 3. 打包
tar -czf vcpkg-packages.tar.gz -C ~ vcpkg/
```

#### 在目标机器上：

```bash
# 解压
cd ~
tar -xzf vcpkg-packages.tar.gz

# 使用
cd ~/桌面/EX_MiracleVision
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

---

## 推荐方案对比

| 方案 | 难度 | 速度 | 版本 | 推荐度 |
|------|------|------|------|--------|
| 方案 1 (镜像) | 中 | 快 | 最新 | ⭐⭐⭐⭐ |
| 方案 2 (手动下载) | 低 | 中 | 最新 | ⭐⭐⭐⭐⭐ |
| 方案 3 (代理) | 中 | 中 | 最新 | ⭐⭐⭐ |
| 方案 4 (apt) | 低 | 极快 | 较旧 | ⭐⭐⭐⭐ |
| 方案 5 (离线) | 高 | 快 | 最新 | ⭐⭐ |

---

## 快速修复命令

### 最简单：使用 GitHub 代理下载

```bash
# 1. 手动下载所有包（使用 ghproxy 镜像）
cd /home/prototype152/vcpkg/downloads

# fmt 10.2.1
wget https://ghproxy.com/https://github.com/fmtlib/fmt/archive/10.2.1.tar.gz \
    -O fmtlib-fmt-10.2.1.tar.gz

# 2. 重新运行 cmake
cd ~/桌面/EX_MiracleVision
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic \
    2>&1 | tee cmake_retry.log
```

### 如果还是失败：直接用系统包

```bash
cd ~/桌面/EX_MiracleVision

# 创建一个不使用 vcpkg 的构建脚本
cat > build_without_vcpkg.sh << 'EOF'
#!/bin/bash

# 安装系统依赖
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake \
    libopencv-dev libopencv-contrib-dev \
    libfmt-dev libspdlog-dev \
    libeigen3-dev libyaml-cpp-dev \
    nlohmann-json3-dev libtbb-dev \
    libgtk-3-dev

# 清理并构建
rm -rf build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

echo "构建完成！可执行文件: build/bin/MiracleVision"
EOF

chmod +x build_without_vcpkg.sh
./build_without_vcpkg.sh
```

---

## 总结

**如果网络问题无法解决，建议使用方案 4（系统包）**，虽然版本稍旧但足够稳定。

**如果必须使用最新版本，使用方案 2（手动下载）**，配合 ghproxy.com 镜像。
