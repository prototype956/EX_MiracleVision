# CMake 重构完成 - 使用指南

**完成日期**: 2026-02-11  
**重构类型**: 渐进式重构  
**依赖管理**: vcpkg

---

## ✅ 重构完成清单

### 已完成的工作

- [x] ✅ 备份原 CMakeLists.txt → `CMakeLists.txt.backup`
- [x] ✅ 更新 vcpkg.json（移除 vtkgtk）
- [x] ✅ 创建 cmake/ 辅助文件目录
  - [x] `cmake/CompilerOptions.cmake` - 编译器配置
  - [x] `cmake/Dependencies.cmake` - vcpkg 依赖查找
  - [x] `cmake/ThirdParty.cmake` - 第三方库配置
- [x] ✅ 创建第三方库 CMakeLists.txt
  - [x] `3rdparty/mindvision/CMakeLists.txt`
  - [x] `3rdparty/foxglove/CMakeLists.txt`
- [x] ✅ 创建模块 CMakeLists.txt
  - [x] `utils/CMakeLists.txt`
  - [x] `devices/CMakeLists.txt`
  - [x] `module/CMakeLists.txt`
  - [x] `base/CMakeLists.txt`
- [x] ✅ 更新现有文件
  - [x] `module/foxglove_publisher/CMakeLists.txt`
  - [x] `test/CMakeLists.txt`
- [x] ✅ 重写主 CMakeLists.txt（从 316 行精简到 185 行）

---

## 📊 改进对比

| 项目 | 重构前 | 重构后 |
|------|--------|--------|
| **主文件行数** | 316 行 | 185 行 |
| **文件数量** | 3 个 | 11 个 |
| **依赖管理** | 混乱（vcpkg + 系统 + 硬编码） | 清晰（vcpkg + 可选系统库） |
| **模块化** | ❌ 单文件 | ✅ 分模块 |
| **可配置性** | ❌ 硬编码路径 | ✅ CMake 选项 |
| **可维护性** | ⭐⭐ | ⭐⭐⭐⭐⭐ |

---

## 🚀 使用方法

### 1. 首次配置（使用 vcpkg）

#### 方式 A: 使用 vcpkg toolchain（推荐）

```bash
# 进入项目目录
cd /home/prototype152/桌面/EX_MiracleVision

# 配置项目（指定 vcpkg toolchain）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)

# 运行
./build/bin/MiracleVision
```

#### 方式 B: 使用 vcpkg manifest mode（自动安装依赖）

```bash
# vcpkg 会自动读取 vcpkg.json 并安装依赖
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release
```

---

### 2. 构建选项

#### 基础构建选项

```bash
# 只编译主程序（默认）
cmake -B build -S . -DBUILD_MAIN=ON -DBUILD_TESTS=OFF

# 编译测试程序
cmake -B build -S . -DBUILD_TESTS=ON

# 调试模式
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# 发布模式
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
```

#### 第三方库选项

```bash
# 默认配置（MindVision + Foxglove）
cmake -B build -S . \
    -DUSE_MINDVISION_SDK=ON \
    -DUSE_FOXGLOVE_SDK=ON \
    -DUSE_OPENVINO=OFF \
    -DUSE_ONNXRUNTIME=OFF

# 启用 OpenVINO（自定义路径）
cmake -B build -S . \
    -DUSE_OPENVINO=ON \
    -DOpenVINO_DIR=/opt/intel/openvino_2024.6.0/runtime/cmake

# 启用 ONNX Runtime（自定义路径）
cmake -B build -S . \
    -DUSE_ONNXRUNTIME=ON \
    -DONNXRUNTIME_ROOT_PATH=/usr/local

# 无相机调试模式（只用视频文件）
cmake -B build -S . \
    -DUSE_MINDVISION_SDK=OFF \
    -DUSE_FOXGLOVE_SDK=OFF

# 比赛模式（无调试界面）
cmake -B build -S . \
    -DUSE_FOXGLOVE_SDK=OFF
```

---

### 3. 完整配置示例

#### 开发环境配置

```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_MAIN=ON \
    -DBUILD_TESTS=ON \
    -DUSE_MINDVISION_SDK=ON \
    -DUSE_FOXGLOVE_SDK=ON \
    -DUSE_OPENVINO=OFF \
    -DUSE_ONNXRUNTIME=OFF
```

#### 比赛环境配置

```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_MAIN=ON \
    -DBUILD_TESTS=OFF \
    -DUSE_MINDVISION_SDK=ON \
    -DUSE_FOXGLOVE_SDK=OFF \
    -DUSE_OPENVINO=OFF \
    -DUSE_ONNXRUNTIME=OFF
```

#### 离线调试配置（使用视频文件）

```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_MAIN=ON \
    -DBUILD_TESTS=ON \
    -DUSE_MINDVISION_SDK=OFF \
    -DUSE_FOXGLOVE_SDK=ON \
    -DUSE_OPENVINO=OFF \
    -DUSE_ONNXRUNTIME=OFF
```

---

### 4. 增量编译

```bash
# 修改代码后只需重新编译
cmake --build build

# 并行编译（更快）
cmake --build build -j$(nproc)

# 只编译特定目标
cmake --build build --target MiracleVision
cmake --build build --target minimum_vision
cmake --build build --target mv-basic-armor
```

---

### 5. 清理和重新配置

```bash
# 清理构建目录
rm -rf build

# 重新配置
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=...

# 或者只清理编译产物（保留 CMake 缓存）
cmake --build build --target clean
```

---

## 🔧 配置选项详解

### USE_MINDVISION_SDK

- **默认**: ON
- **说明**: 启用迈德威视相机 SDK
- **使用场景**: 
  - ON: 使用真实相机
  - OFF: 使用视频文件调试（VIDEO_DEBUG 模式）

### USE_FOXGLOVE_SDK

- **默认**: ON
- **说明**: 启用 Foxglove WebSocket 发布器
- **使用场景**:
  - ON: 可以通过网页远程查看调试信息
  - OFF: 比赛时减少资源占用，无调试界面

### USE_OPENVINO

- **默认**: OFF
- **说明**: 启用 Intel OpenVINO 推理引擎
- **使用场景**: 传统视觉方案不依赖 ONNX Runtime
- **配置路径**: `-DOpenVINO_DIR=/opt/intel/openvino_2024.6.0/runtime/cmake`

### USE_ONNXRUNTIME

- **默认**: OFF
- **说明**: 启用 ONNX Runtime 推理引擎
- **使用场景**: 传统视觉方案不依赖 OpenVINO
- **配置路径**: `-DONNXRUNTIME_ROOT_PATH=/usr/local`

---

## ⚠️ 注意事项

### 1. OpenVINO 和 ONNX Runtime 冲突

如果同时启用 OpenVINO 和 ONNX Runtime，CMake 会发出警告：

```
Both OpenVINO and ONNX Runtime are enabled!
This may cause conflicts. Consider using only one.
```

**建议**: 只启用其中一个，或都不启用（使用 OpenCV DNN）。

### 2. vcpkg toolchain 路径

确保正确设置 vcpkg toolchain 路径：

```bash
# 查找 vcpkg 安装位置
which vcpkg

# 假设 vcpkg 在 ~/vcpkg
export VCPKG_ROOT=$HOME/vcpkg
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

### 3. 依赖安装

如果 vcpkg 依赖未安装，运行：

```bash
cd /path/to/vcpkg
./vcpkg install --triplet=x64-linux
```

或使用 manifest mode（推荐）：

```bash
# vcpkg 会自动读取 vcpkg.json
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=...
```

### 4. 第三方库路径

如果 MindVision 或 Foxglove SDK 不在默认位置：

```
⚠ MindVision SDK directory not found, disabling...
⚠ Foxglove SDK directory not found, disabling...
```

确保以下目录存在：
- `3rdparty/mindvision/`
- `3rdparty/foxglove/`

---

## 🐛 故障排查

### 问题 1: OpenCV 模块缺失

```
OpenCV component 'xxx' not found!
```

**解决方法**:
```bash
# 检查 OpenCV 配置
pkg-config --modversion opencv4
pkg-config --libs opencv4

# 重新安装 OpenCV（通过 vcpkg）
vcpkg remove opencv4
vcpkg install opencv4[ffmpeg,dnn,jpeg,png,contrib,tbb,eigen]
```

### 问题 2: 找不到 vcpkg 依赖

```
Could not find package fmt
```

**解决方法**:
```bash
# 确保使用了 vcpkg toolchain
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# 或安装依赖
vcpkg install fmt spdlog eigen3 yaml-cpp nlohmann-json tbb
```

### 问题 3: MindVision SDK 架构不匹配

```
Unsupported architecture for MindVision SDK: xxx
```

**解决方法**:
检查系统架构是否支持：
```bash
uname -m
# 支持: x86_64, x86, aarch64, armv7
```

### 问题 4: 链接错误

```
undefined reference to 'xxx'
```

**解决方法**:
1. 检查库依赖顺序
2. 清理并重新编译
```bash
rm -rf build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=...
cmake --build build
```

---

## 📂 新文件结构

```
EX_MiracleVision/
├── CMakeLists.txt                    # 主配置（185 行）✅
├── CMakeLists.txt.backup             # 备份（316 行）
├── vcpkg.json                        # vcpkg 依赖清单 ✅
├── cmake/                            # CMake 模块 🆕
│   ├── CompilerOptions.cmake         # 编译选项
│   ├── Dependencies.cmake            # vcpkg 依赖
│   └── ThirdParty.cmake              # 第三方库
├── 3rdparty/                         # 第三方库
│   ├── mindvision/
│   │   └── CMakeLists.txt            # MindVision 配置 🆕
│   └── foxglove/
│       └── CMakeLists.txt            # Foxglove 配置 🆕
├── utils/
│   └── CMakeLists.txt                # 工具库 🆕
├── devices/
│   └── CMakeLists.txt                # 设备层 🆕
├── module/
│   ├── CMakeLists.txt                # 算法模块 🆕
│   └── foxglove_publisher/
│       └── CMakeLists.txt            # 已更新 ✅
├── base/
│   └── CMakeLists.txt                # 主程序 🆕
└── test/
    └── CMakeLists.txt                # 测试程序（已更新）✅
```

---

## 🎯 下一步建议

### 立即测试

1. **配置测试**
```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release
```

2. **编译测试**
```bash
cmake --build build -j$(nproc)
```

3. **运行测试**
```bash
./build/bin/MiracleVision
```

### 后续优化

1. **添加单元测试**: 使用 Google Test
2. **添加文档**: 使用 Doxygen
3. **CI/CD 集成**: GitHub Actions
4. **性能优化**: 启用 LTO、PGO
5. **安装配置**: 添加 install() 规则

---

## 📝 重要变更记录

1. ✅ **移除 pkg-config 依赖**: GTK2 和 FFmpeg 不再需要（由 OpenCV 处理）
2. ✅ **OpenVINO 可配置**: 路径可通过 CMake 变量设置
3. ✅ **ONNX Runtime 可配置**: 路径可通过 CMake 变量设置
4. ✅ **MindVision SDK 可选**: 用于视频调试
5. ✅ **Foxglove SDK 可选**: 用于远程调试
6. ✅ **Modern CMake**: 使用 target-based 链接
7. ✅ **模块化**: 11 个独立 CMake 文件

---

## 🤝 获取帮助

如果遇到问题：

1. 查看 CMake 输出日志
2. 检查 `docs/CMAKE_REFACTOR_PLAN.md`
3. 查看 `CMakeLists.txt.backup` 对比变更
4. 提交 Issue

---

**重构完成！享受新的构建系统！** 🎉
