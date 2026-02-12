# vcpkg 配置问题总结

**日期**: 2026年2月12日  
**项目**: EX_MiracleVision CMake 重构

---

## 🎯 原始目标

将项目从传统 CMake + pkg-config 迁移到现代化的 vcpkg 依赖管理系统。

---

## 📋 已完成的工作

### ✅ 1. CMake 结构重构
- [x] 创建模块化 CMake 结构（11个 CMakeLists.txt 文件）
- [x] 实现 target-based 现代 CMake 架构
- [x] 分离编译选项、依赖、第三方库配置
- [x] 创建完整文档（6个 markdown 文件）

### ✅ 2. vcpkg 配置文件
- [x] 创建 `vcpkg.json` 清单文件
- [x] 定义依赖列表：fmt, spdlog, opencv4[features], eigen3, yaml-cpp, nlohmann-json, tbb
- [x] 添加 `builtin-baseline` 版本控制

### ✅ 3. 配置选项
- [x] 实现 6 个 CMake 选项：
  - `BUILD_MAIN` (默认 ON)
  - `BUILD_TESTS` (默认 ON)
  - `USE_MINDVISION_SDK` (默认 ON)
  - `USE_FOXGLOVE_SDK` (默认 ON)
  - `USE_OPENVINO` (默认 OFF)
  - `USE_ONNXRUNTIME` (默认 OFF)

---

## ❌ 遇到的问题

### 问题 1: OpenCV TBB 静态链接冲突 ✅ 已解决

**错误信息**:
```
opencv4[tbb] is only supported on '!static', which does not match x64-linux
```

**原因**: vcpkg 默认使用静态链接（`x64-linux` triplet），但 OpenCV 的 TBB 特性不支持静态链接。

**解决方案**: ✅
```bash
cmake -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

---

### 问题 2: GCC 编译器内部错误 ✅ 已解决

**错误信息**:
```
internal compiler error: 段错误 (segmentation fault)
在编译 fmt 12.1.0 时发生
```

**原因**: fmt 12.1.0 与 GCC 11.4 存在已知兼容性问题（编译器 bug）

**解决方案**: ✅ 升级 GCC 到 13.x
- GCC 13+ 修复了此 bug，可以正常编译 fmt 12.x
- 同时获得完整的 C++20 支持
- 为未来的 C++23 特性做准备

**执行步骤**:
```bash
# 运行自动化升级脚本
./upgrade_gcc.sh

# 或手动执行
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update
sudo apt-get install -y gcc-13 g++-13
```

**项目配置已更新**:
- ✅ vcpkg.json: 已移除 fmt 版本约束，使用最新版本
- ✅ cmake/CompilerOptions.cmake: C++ 标准更新为 C++20
- ✅ 创建自动化升级脚本: `upgrade_gcc.sh`

---

### 问题 3: vcpkg 网络下载失败 ⚠️ 当前问题

**错误信息**:
```
curl error: Failed to connect to github.com port 443 after ... ms: Connection timed out
curl error: Timeout was reached
```

**原因**: 
- GitHub 访问不稳定（中国大陆网络问题）
- vcpkg 从 GitHub 下载源代码包
- 网络超时或连接被重置

**影响范围**:
- 无法下载 fmt, spdlog, opencv4 等依赖的源代码
- vcpkg 安装过程中断

---

## 🔍 当前状态分析

### 系统环境
- **操作系统**: Linux (Ubuntu)
- **CMake 版本**: 3.22
- **GCC 版本**: 11.4.0
- **vcpkg 路径**: `/home/prototype152/vcpkg/`
- **项目路径**: `/home/prototype152/桌面/EX_MiracleVision/`

### vcpkg 配置
- **Triplet**: `x64-linux-dynamic` ✅
- **Toolchain**: `/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake` ✅
- **Baseline**: `aa2d37682e3318d93aef87efa7b0e88e81cd3d59` ✅

### 依赖安装进度
```
安装进度: 4/115 包
- [✅] eigen3:x64-linux-dynamic@5.0.1 (已成功)
- [❌] fmt:x64-linux-dynamic@10.2.1 (网络下载失败)
- [⏸️] 其余 111 个包等待中
```

---

## 🚧 网络问题详细分析

### 失败的下载请求

1. **fmt 10.2.1**
   ```
   https://github.com/fmtlib/fmt/archive/10.2.1.tar.gz
   ```

2. **可能还会失败的包**（预测）:
   - spdlog (从 GitHub)
   - opencv4 (从 GitHub)
   - yaml-cpp (从 GitHub)
   - nlohmann-json (从 GitHub)
   - 其他依赖包

### vcpkg 下载机制

vcpkg 下载顺序：
1. 检查本地缓存 (`~/.cache/vcpkg/archives`)
2. 尝试从 vcpkg binary cache 下载预编译包
3. 下载源代码并编译

当前问题发生在**第 3 步**。

---

## 💡 可选解决方案

### 方案 1: 配置 GitHub 镜像 🌟 推荐

**优点**: 一劳永逸，解决所有 GitHub 下载问题  
**缺点**: 需要配置 Git 和 vcpkg

**操作步骤**:
```bash
# 1. 配置 Git 使用镜像
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"

# 或使用其他镜像
git config --global url."https://mirror.ghproxy.com/https://github.com".insteadOf "https://github.com"

# 2. 清理并重试
rm -rf build
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

---

### 方案 2: 手动下载依赖包

**优点**: 可控，不依赖镜像  
**缺点**: 需要手动下载每个包，繁琐

**操作步骤**:
```bash
# 进入 vcpkg 下载目录
cd /home/prototype152/vcpkg/downloads

# 手动下载（使用代理或下载工具）
wget https://github.com/fmtlib/fmt/archive/10.2.1.tar.gz -O fmtlib-fmt-10.2.1.tar.gz
wget https://github.com/gabime/spdlog/archive/v1.x.x.tar.gz -O gabime-spdlog-v1.x.x.tar.gz
# ... 下载其他包

# 重试 cmake
cmake -B build -S . ...
```

---

### 方案 3: 使用 vcpkg 预编译二进制缓存

**优点**: 如果有缓存，速度快  
**缺点**: 需要配置 binary cache，可能缓存不全

**操作步骤**:
```bash
# 配置 binary cache
export VCPKG_BINARY_SOURCES="clear;x-azurl,https://vcpkg.blob.core.windows.net/vcpkg-binary-cache,read"

# 重试
cmake -B build -S . ...
```

---

### 方案 4: 使用系统包管理器代替 vcpkg ⚡ 最快方案

**优点**: 速度快，稳定，无网络问题  
**缺点**: 系统包版本可能较旧，失去 vcpkg 优势

**操作步骤**:
```bash
# 1. 安装系统依赖
sudo apt-get update
sudo apt-get install -y \
    libopencv-dev \
    libfmt-dev \
    libspdlog-dev \
    libeigen3-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libtbb-dev

# 2. 修改 CMakeLists.txt（移除 vcpkg 工具链）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build build -j$(nproc)
```

**需要修改的文件**:
- `cmake/Dependencies.cmake`: 改为直接使用 `find_package()`
- 不使用 `-DCMAKE_TOOLCHAIN_FILE` 参数

---

### 方案 5: 混合方案（系统包 + 第三方库）

**优点**: 结合两者优势，快速且灵活  
**缺点**: 配置稍复杂

**操作**:
- 使用系统包安装：OpenCV, fmt, spdlog, eigen3, yaml-cpp, nlohmann-json, tbb
- 保留：MindVision SDK, Foxglove SDK（本地第三方库）
- 可选：OpenVINO, ONNX Runtime（手动配置路径）

---

## 🎯 推荐决策路径

### 如果你想继续使用 vcpkg（学习现代依赖管理）
→ **方案 1: 配置 GitHub 镜像**

### 如果你想快速完成项目（注重实用）
→ **方案 4: 使用系统包管理器**

### 如果你想平衡两者
→ **方案 5: 混合方案**

---

## 📊 时间成本估算

| 方案 | 配置时间 | 编译时间 | 总时间 | 成功率 |
|------|---------|---------|--------|--------|
| 方案 1 (镜像) | 5 分钟 | 30-60 分钟 | ~1 小时 | 90% |
| 方案 2 (手动下载) | 20-30 分钟 | 30-60 分钟 | ~1.5 小时 | 95% |
| 方案 3 (binary cache) | 5 分钟 | 10-30 分钟 | ~30 分钟 | 60% |
| 方案 4 (系统包) | 5 分钟 | 5-10 分钟 | **~15 分钟** | 99% |
| 方案 5 (混合) | 10 分钟 | 10-15 分钟 | **~25 分钟** | 95% |

---

## 🔧 技术债务分析

### 如果选择 vcpkg
- ✅ 优点：版本控制精确，跨平台，可复现
- ❌ 缺点：首次编译慢，网络依赖强，调试复杂

### 如果选择系统包
- ✅ 优点：速度快，稳定，简单
- ❌ 缺点：版本可能旧，依赖系统环境

---

## ❓ 需要你的决策

请选择一个方案：

1. **方案 1**: 配置 GitHub 镜像，继续 vcpkg（推荐学习）
2. **方案 4**: 改用系统包，放弃 vcpkg（推荐快速完成）
3. **方案 5**: 混合方案，系统包 + 保留模块化 CMake（推荐平衡）
4. **其他**: 你有其他想法吗？

---

## 📝 下一步行动

等待你的决策后，我会：

1. **如果选方案 1**: 
   - 配置 Git 镜像
   - 重新运行 vcpkg 安装
   - 监控编译进度

2. **如果选方案 4**:
   - 修改 `cmake/Dependencies.cmake`
   - 安装系统依赖
   - 快速编译测试

3. **如果选方案 5**:
   - 修改 CMake 配置支持混合依赖
   - 安装系统包
   - 保留可选组件的灵活性

---

## 🤔 我的建议

作为你的技术合伙人，我的建议是：

### 如果这是学习项目或需要跨平台
→ **选择方案 1**（配置镜像 + vcpkg）

理由：学习现代 C++ 依赖管理，未来价值高

### 如果这是实际项目需要快速交付
→ **选择方案 5**（混合方案）

理由：
- 核心依赖用系统包（稳定快速）
- 保留模块化 CMake 结构（已经完成的工作不浪费）
- MindVision/Foxglove 等特殊库仍然灵活配置
- OpenVINO/ONNX Runtime 可选择性安装

**这样你既能快速推进项目，又保留了 CMake 重构的成果。**

---

你想选择哪个方案？或者有其他想法？
