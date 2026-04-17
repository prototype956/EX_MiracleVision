# CMake 重构 - 快速参考

## 🚀 快速开始

### 基础编译（推荐）

```bash
# 配置
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)

# 运行
./build/bin/MiracleVision
```

---

## 📋 常用配置

### 开发模式
```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTS=ON
```

### 比赛模式（无调试界面）
```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_FOXGLOVE_SDK=OFF
```

### 视频调试模式（无相机）
```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DUSE_MINDVISION_SDK=OFF
```

### 启用 OpenVINO
```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DUSE_OPENVINO=ON \
    -DOpenVINO_DIR=/opt/intel/openvino_2024.6.0/runtime/cmake
```

---

## 🎛️ 构建选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `BUILD_MAIN` | ON | 编译主程序 |
| `BUILD_TESTS` | OFF | 编译测试程序 |
| `USE_MINDVISION_SDK` | ON | 使用真实相机 |
| `USE_FOXGLOVE_SDK` | ON | 远程调试界面 |
| `USE_OPENVINO` | OFF | Intel 推理引擎 |
| `USE_ONNXRUNTIME` | OFF | ONNX 推理引擎 |

---

## 📁 文件结构

```
新增文件:
  cmake/CompilerOptions.cmake
  cmake/Dependencies.cmake
  cmake/ThirdParty.cmake
  3rdparty/mindvision/CMakeLists.txt
  3rdparty/foxglove/CMakeLists.txt
  utils/CMakeLists.txt
  devices/CMakeLists.txt
  module/CMakeLists.txt
  base/CMakeLists.txt

备份文件:
  CMakeLists.txt.backup (旧版本)

更新文件:
  CMakeLists.txt (主文件，316行 → 185行)
  module/foxglove_publisher/CMakeLists.txt
  test/CMakeLists.txt
  vcpkg.json (移除 vtkgtk)
```

---

## 🔧 常用命令

```bash
# 重新配置
cmake -B build -S .

# 增量编译
cmake --build build

# 清理
cmake --build build --target clean

# 完全重建
rm -rf build && cmake -B build -S . && cmake --build build

# 查看可用目标
cmake --build build --target help

# 仅编译 detection 契约测试
cmake --build build --target mv-armor-detector-contract-test -j$(nproc)

# 运行 detection 契约测试
./build/src/test/mv-armor-detector-contract-test

# 编译 Foxglove 在线联调测试入口（依赖 USE_FOXGLOVE_SDK=ON）
cmake --build build --target mv-foxglove-vision-test -j$(nproc)

# 使用 SimCamera 输入运行（端口 8765）
./build/src/test/mv-foxglove-vision-test sim blue 8765

# 使用 SimCamera 并覆盖 endpoint
./build/src/test/mv-foxglove-vision-test sim:127.0.0.1:19090 blue 8765

# 安装（如果配置了）
cmake --install build
```

---

## ⚠️ 注意事项

1. **必须使用 vcpkg toolchain**: `-DCMAKE_TOOLCHAIN_FILE=...`
2. **OpenVINO 和 ONNX Runtime 不要同时启用**
3. **首次编译需要时间**: vcpkg 会安装依赖
4. **MindVision SDK**: 需要硬件才能使用，否则设为 OFF

---

## 📖 详细文档

- **完整指南**: `docs/CMAKE_MIGRATION_GUIDE.md`
- **重构计划**: `docs/CMAKE_REFACTOR_PLAN.md`
- **依赖分析**: `docs/DEPENDENCY_ANALYSIS.md`
