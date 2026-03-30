# 构建说明更新建议

## 建议添加到 README.md 的内容

---

## 🔨 构建项目

### 前置要求

- **操作系统**: Linux（Ubuntu 20.04+ 推荐）
- **CMake**: 3.15 或更高版本
- **编译器**: GCC 9+ 或 Clang 10+
- **vcpkg**: 最新版本（用于依赖管理）

### 快速开始

```bash
# 1. 克隆项目
git clone https://github.com/prototype956/EX_MiracleVision.git
cd EX_MiracleVision

# 2. 配置项目（使用 vcpkg）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

# 3. 编译
cmake --build build -j$(nproc)

# 4. 运行
./build/bin/MiracleVision
```

### 构建选项

```bash
# 开发模式（包含测试）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTS=ON

# 比赛模式（禁用调试界面）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_FOXGLOVE_SDK=OFF

# 视频调试模式（无真实相机）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DUSE_MINDVISION_SDK=OFF
```

### 可用的构建选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_MAIN` | ON | 编译主程序 |
| `BUILD_TESTS` | OFF | 编译测试程序 |
| `USE_MINDVISION_SDK` | ON | 启用迈德威视相机 SDK |
| `USE_FOXGLOVE_SDK` | ON | 启用 Foxglove 远程调试 |
| `USE_OPENVINO` | OFF | 启用 Intel OpenVINO 推理 |
| `USE_ONNXRUNTIME` | OFF | 启用 ONNX Runtime 推理 |

### 安装 vcpkg

如果还没有安装 vcpkg：

```bash
# 克隆 vcpkg
git clone https://github.com/microsoft/vcpkg.git $HOME/vcpkg

# 编译 vcpkg
cd $HOME/vcpkg
./bootstrap-vcpkg.sh

# 设置环境变量
export VCPKG_ROOT=$HOME/vcpkg
echo 'export VCPKG_ROOT=$HOME/vcpkg' >> ~/.bashrc
```

### 故障排查

#### 问题: 找不到 vcpkg 依赖

```bash
# 确保使用了 vcpkg toolchain
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

#### 问题: OpenCV 模块缺失

```bash
# 重新安装 OpenCV
vcpkg remove opencv4
vcpkg install opencv4[ffmpeg,dnn,jpeg,png,contrib,tbb,eigen]
```

#### 问题: 编译失败

```bash
# 清理并重新编译
rm -rf build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)
```

### 更多信息

- **快速参考**: 查看 [CMAKE_QUICK_REF.md](CMAKE_QUICK_REF.md)
- **完整指南**: 查看 [docs/CMAKE_MIGRATION_GUIDE.md](docs/CMAKE_MIGRATION_GUIDE.md)
- **测试配置**: 运行 `./test_cmake_config.sh`

---

## 📂 项目结构

```
EX_MiracleVision/
├── base/                  # 主程序
├── module/                # 算法模块（装甲板、能量机关、预测等）
├── devices/               # 设备层（相机、串口）
├── utils/                 # 工具库
├── configs/               # 配置文件
├── src/test/              # 测试程序
├── 3rdparty/             # 第三方库（MindVision, Foxglove）
├── cmake/                # CMake 辅助模块
├── docs/                 # 项目文档
└── vcpkg.json           # vcpkg 依赖清单
```

---

## 🚀 快速测试

运行自动化测试脚本验证构建配置：

```bash
./test_cmake_config.sh
```

---

## 📖 文档

- [CMake 快速参考](CMAKE_QUICK_REF.md)
- [CMake 迁移指南](docs/CMAKE_MIGRATION_GUIDE.md)
- [依赖分析](docs/DEPENDENCY_ANALYSIS.md)
- [重构总结](CMAKE_REFACTOR_SUMMARY.md)

---
