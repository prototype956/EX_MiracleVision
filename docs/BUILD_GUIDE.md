# 构建指南

本文档详细说明 EX_MiracleVision 的构建过程、CMake 配置选项、编译步骤和故障排除。

---

## 📋 目录

- [快速构建](#快速构建)
- [CMake 配置选项](#cmake-配置选项)
- [详细构建步骤](#详细构建步骤)
- [测试程序编译](#测试程序编译)
- [CMake 架构说明](#cmake-架构说明)
- [故障排除](#故障排除)
- [性能优化](#性能优化)

---

## 🚀 快速构建

### 一键编译（推荐）

```bash
# 克隆项目
git clone https://github.com/prototype956/EX_MiracleVision.git
cd EX_MiracleVision

# 安装依赖（首次）
./scripts/install_dependencies.sh

# 构建
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# 运行
./bin/MiracleVision
```

---

## ⚙️ CMake 配置选项

### 主要选项

| 选项 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `CMAKE_BUILD_TYPE` | STRING | Release | 构建类型: Release/Debug/RelWithDebInfo |
| `BUILD_MAIN` | BOOL | ON | 是否编译主程序 |
| `BUILD_TESTS` | BOOL | OFF | 是否编译测试程序 |
| `CMAKE_CXX_STANDARD` | STRING | 17 | C++ 标准版本 |
| `CMAKE_INSTALL_PREFIX` | PATH | /usr/local | 安装路径 |

### 使用示例

```bash
# Release 模式（生产环境）
cmake -DCMAKE_BUILD_TYPE=Release ..

# Debug 模式（开发调试）
cmake -DCMAKE_BUILD_TYPE=Debug ..

# 带调试信息的优化版本
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..

# 编译测试程序
cmake -DBUILD_TESTS=ON ..

# 组合使用
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON ..

# 指定编译器
cmake -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11 ..

# 指定安装路径
cmake -DCMAKE_INSTALL_PREFIX=/opt/miraclevision ..
```

---

## 📝 详细构建步骤

### 第一步：准备环境

```bash
# 1. 确保依赖已安装
./scripts/install_dependencies.sh --check-only

# 2. 如果使用 Conda，需要停用
conda deactivate

# 3. 进入项目目录
cd /path/to/EX_MiracleVision
```

### 第二步：配置 CMake

```bash
# 1. 创建构建目录（推荐使用独立的构建目录）
mkdir -p build
cd build

# 2. 运行 CMake 配置
cmake -DCMAKE_BUILD_TYPE=Release ..

# 配置成功的输出示例：
# -- Build type: Release
# -- System: Linux
# -- Compiler: GNU 11.4.0
# -- ✓ fmt found: 8.1.1
# -- ✓ spdlog found: 1.9.2
# -- ✓ OpenCV found: 4.5.5
# -- ✓ Eigen3 found: 3.4.0
# ...
# -- Configuring done
# -- Generating done
# -- Build files have been written to: /path/to/build
```

### 第三步：编译项目

```bash
# 方法 1: 使用 make（推荐）
make -j$(nproc)          # 使用所有 CPU 核心
make -j4                 # 使用 4 个核心
make                     # 单核编译（慢但稳定）

# 方法 2: 使用 cmake --build
cmake --build . -j$(nproc)

# 方法 3: 编译特定目标
make MiracleVision       # 仅编译主程序
make mv-basic-armor      # 编译特定库
make minimum_vision      # 编译测试程序（需要 BUILD_TESTS=ON）
```

### 第四步：验证构建

```bash
# 检查可执行文件
ls -lh bin/MiracleVision

# 检查库文件
ls -lh lib/

# 查看库依赖
ldd bin/MiracleVision

# 运行程序
./bin/MiracleVision --help
```

---

## 🧪 测试程序编译

### 启用测试程序

```bash
cd build

# 重新配置以启用测试
cmake -DBUILD_TESTS=ON ..

# 编译所有测试
make -j$(nproc)

# 或仅编译特定测试
make minimum_vision
```

### 可用的测试程序

| 测试程序 | 源文件 | 功能 |
|---------|--------|------|
| `minimum_vision` | test/minimum_vision.cpp | 最小视觉系统测试 |
| `foxglove_camera_sdk` | test/foxglove_camera_sdk.cpp | Foxglove 相机 SDK 测试（已注释） |

### 运行测试

```bash
# 运行 minimum_vision
cd build
./bin/minimum_vision

# 使用测试视频
./bin/minimum_vision --video ../video/test.mp4

# 查看帮助
./bin/minimum_vision --help
```

---

## 🏗️ CMake 架构说明

### 项目结构

```
EX_MiracleVision/
├── CMakeLists.txt              # 主 CMake 文件
├── cmake/                      # CMake 模块
│   ├── CompilerOptions.cmake   # 编译器选项配置
│   ├── Dependencies.cmake      # 依赖查找配置
│   └── ThirdParty.cmake        # 第三方库配置
├── base/CMakeLists.txt         # 主程序
├── utils/CMakeLists.txt        # 工具库
├── devices/CMakeLists.txt      # 设备层
├── module/CMakeLists.txt       # 算法模块
└── test/CMakeLists.txt         # 测试程序
```

### 主 CMakeLists.txt 解析

```cmake
# 项目定义
cmake_minimum_required(VERSION 3.22)
project(EX_MiracleVision VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项（BUILD_MAIN, BUILD_TESTS）
option(BUILD_MAIN "Build main program" ON)
option(BUILD_TESTS "Build test programs" OFF)

# 加载 CMake 模块
include(cmake/CompilerOptions.cmake)    # 编译器优化选项
include(cmake/Dependencies.cmake)       # 查找系统依赖
include(cmake/ThirdParty.cmake)         # 配置第三方 SDK

# 添加子目录
add_subdirectory(utils)                 # 工具库
add_subdirectory(devices)               # 设备层
add_subdirectory(module)                # 算法模块
add_subdirectory(base)                  # 主程序
add_subdirectory(test)                  # 测试程序（可选）
```

### 模块开发指南

#### 1. 添加新的工具库（utils/）

**步骤**：
1. 创建源文件: `utils/my_tool.cpp` 和 `utils/my_tool.hpp`
2. 编辑 `utils/CMakeLists.txt`:

```cmake
# 添加新库
add_library(mv-my-tool SHARED
    my_tool.cpp
    my_tool.hpp
)

# 链接依赖
target_link_libraries(mv-my-tool PUBLIC
    fmt::fmt
    # 其他依赖...
)

# 设置输出目录
set_target_properties(mv-my-tool PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
```

#### 2. 添加新的算法模块（module/）

**步骤**：
1. 创建模块目录: `module/my_module/`
2. 创建源文件: `module/my_module/my_algo.cpp`
3. 创建 `module/my_module/CMakeLists.txt`:

```cmake
message(STATUS "  Configuring my_module...")

# 定义库
add_library(mv-my-algo SHARED
    my_algo.cpp
    my_algo.hpp
)

# 包含路径
target_include_directories(mv-my-algo PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# 链接依赖
target_link_libraries(mv-my-algo PUBLIC
    opencv_core
    opencv_imgproc
    mv-logger          # 使用工具库
    # 其他依赖...
)

# 输出目录
set_target_properties(mv-my-algo PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

message(STATUS "    ✓ my_algo configured")
```

4. 在 `module/CMakeLists.txt` 中添加:

```cmake
add_subdirectory(my_module)
```

#### 3. 添加新的测试程序（test/）

**步骤**：
1. 创建测试文件: `src/test/<category>/my_test.cpp`
2. 编辑 `src/test/CMakeLists.txt`:

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/my_test.cpp")
    message(STATUS "  Configuring my_test...")
    
    add_executable(my_test
        my_test.cpp
    )
    
    target_include_directories(my_test PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/module
    )
    
    target_link_libraries(my_test PRIVATE
        mv-my-algo
        opencv_core
        opencv_highgui
    )
    
    # GCC 11 ICE 规避（如果需要）
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(my_test PRIVATE -O1)
    endif()
    
    set_target_properties(my_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    
    message(STATUS "    ✓ my_test configured")
endif()
```

### CMake 最佳实践

1. **库命名规范**: 所有库以 `mv-` 前缀命名（例如 `mv-basic-armor`）
2. **包含路径**: 使用 `${CMAKE_SOURCE_DIR}` 作为基础路径
3. **链接可见性**: 
   - `PUBLIC`: 依赖传递给使用者
   - `PRIVATE`: 仅内部使用
   - `INTERFACE`: 仅传递给使用者
4. **输出目录**: 
   - 可执行文件: `${CMAKE_BINARY_DIR}/bin`
   - 库文件: `${CMAKE_BINARY_DIR}/lib`
5. **编译选项**: 针对 GCC 11 ICE bug，使用 `-O1` 优化

---

## ❗ 故障排除

### 问题 1: CMake 配置失败

**症状**：
```
CMake Error: Could not find a package configuration file provided by "fmt"
```

**解决方案**：
```bash
# 1. 检查依赖是否安装
./scripts/install_dependencies.sh --check-only

# 2. 更新 pkg-config 缓存
export PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH

# 3. 清理缓存重新配置
rm -rf build/*
cd build
cmake ..
```

### 问题 2: 编译时内存不足

**症状**：
```
c++: fatal error: Killed signal terminated program cc1plus
```

**解决方案**：
```bash
# 方法 1: 减少并行任务数
make -j2  # 或 -j1

# 方法 2: 增加交换空间
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# 方法 3: 使用 Debug 模式（优化级别低）
cmake -DCMAKE_BUILD_TYPE=Debug ..
```

### 问题 3: GCC ICE（Internal Compiler Error）

**症状**：
```
internal compiler error: Segmentation fault
```

**解决方案**：
```bash
# 已在 CMake 中自动处理，但可以手动检查：
gcc --version  # 确保是 GCC 11

# 如果是 GCC 12/13，降级到 GCC 11
sudo apt install gcc-11 g++-11
sudo update-alternatives --config gcc
sudo update-alternatives --config g++

# 重新编译
rm -rf build/*
cd build
cmake ..
make -j4
```

### 问题 4: 链接错误

**症状**：
```
undefined reference to `cv::imread(...)`
```

**解决方案**：
```bash
# 1. 检查库是否正确链接
ldd build/bin/MiracleVision | grep opencv

# 2. 更新链接器缓存
sudo ldconfig

# 3. 检查 CMakeLists.txt 中是否添加了所需的库
# 例如：target_link_libraries(... opencv_imgcodecs)
```

### 问题 5: 找不到头文件

**症状**：
```
fatal error: module/armor/basic_armor.hpp: No such file or directory
```

**解决方案**：
```bash
# 检查 CMakeLists.txt 中的包含路径
target_include_directories(my_target PRIVATE
    ${CMAKE_SOURCE_DIR}        # 添加项目根目录
    ${CMAKE_SOURCE_DIR}/module # 添加 module 目录
)
```

### 问题 6: 编译警告过多

**解决方案**：
```bash
# 禁用特定警告
cmake -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-unused-variable" ..

# 或在 CMakeLists.txt 中：
target_compile_options(my_target PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
)
```

---

## 🚀 性能优化

### 编译优化选项

```bash
# Release 模式（默认 -O3 优化）
cmake -DCMAKE_BUILD_TYPE=Release ..

# 添加额外优化
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-march=native -mtune=native" ..

# 启用 LTO（链接时优化）
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON ..
```

### 运行时优化

```bash
# 使用所有 CPU 核心编译
make -j$(nproc)

# 显示详细编译过程
make VERBOSE=1

# ccache 加速重新编译
sudo apt install ccache
cmake -DCMAKE_CXX_COMPILER_LAUNCHER=ccache ..
```

### 清理构建

```bash
# 清理编译产物
make clean

# 完全清理（删除所有生成文件）
rm -rf build/*

# 清理特定目标
make clean-mv-basic-armor
```

---

## 📊 构建状态检查

### 查看构建信息

```bash
# 查看 CMake 缓存
cmake -LA build/ | grep -v "^--"

# 查看编译命令
make VERBOSE=1

# 生成编译数据库（用于 IDE）
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
```

### 性能分析

```bash
# 编译时间统计
time make -j$(nproc)

# 查看二进制文件大小
ls -lh build/bin/MiracleVision

# 查看符号表
nm build/bin/MiracleVision | grep " T "

# strip 去除调试符号（减小文件大小）
strip build/bin/MiracleVision
```

---

## 📚 相关文档

- [环境配置文档](ENVIRONMENT_SETUP.md) - 依赖安装和环境设置
- [架构文档](ARCHITECTURE.md) - 项目结构和模块说明
- [开发指南](DEVELOPMENT.md) - 开发规范和调试技巧

---

**最后更新**: 2026-02-14  
**维护者**: [@prototype956](https://github.com/prototype956)
