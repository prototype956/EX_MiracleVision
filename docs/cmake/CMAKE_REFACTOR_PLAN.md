# CMakeLists.txt 重构计划
## 使用 vcpkg 进行依赖管理

**制定日期**: 2026-02-11  
**项目**: EX_MiracleVision  
**目标**: 现代化 CMake 配置，完全集成 vcpkg 依赖管理

---

## 📋 一、现状分析

### 当前问题

1. **依赖管理混乱**
   - ❌ 硬编码路径（OpenVINO: `/opt/intel/openvino_2024.6.0/`）
   - ❌ 硬编码路径（ONNX Runtime: `/usr/local/`）
   - ❌ 手动 pkg-config 查找（GTK2, FFmpeg）
   - ❌ 混合使用系统库和本地库

2. **CMake 风格过时**
   - ❌ 使用全局 `include_directories()` 和 `link_libraries()`
   - ❌ 大量使用 `list(APPEND)` 累积变量
   - ❌ 缺少 Modern CMake 的 target-based 方法
   - ❌ 缺少导出配置，不支持作为子项目使用

3. **结构问题**
   - ❌ 所有配置都在一个文件中（316 行）
   - ❌ 库定义重复代码多
   - ❌ 第三方库（Foxglove, MindVision）管理不规范

4. **vcpkg 集成不完整**
   - ❌ 没有使用 vcpkg toolchain
   - ❌ 混合使用 vcpkg 和系统包

### 当前依赖分类

| 类型 | 依赖库 | 当前管理方式 | 目标方式 |
|------|--------|------------|---------|
| **vcpkg 可管理** | fmt, spdlog, opencv4, eigen3, yaml-cpp, nlohmann-json, tbb | 混合 | ✅ 完全 vcpkg |
| **系统安装** | OpenVINO, ONNX Runtime | 硬编码路径 | ⚠️ 保持系统安装，但改进查找 |
| **本地第三方** | MindVision SDK, Foxglove SDK | 手动配置 | ✅ 规范化为子模块 |
| **不必要** | GTK2, FFmpeg (pkg-config) | pkg-config | ❌ 移除（OpenCV 自带） |

---

## 🎯 二、重构目标

### 核心目标
1. ✅ **完全集成 vcpkg** - 所有可用库通过 vcpkg 管理
2. ✅ **Modern CMake** - target-based 依赖管理
3. ✅ **模块化** - 拆分为多个 CMake 文件
4. ✅ **可维护性** - 清晰的结构和文档
5. ✅ **灵活性** - 支持不同编译选项和平台

### 次要目标
- 📦 支持作为 CMake 子项目使用
- 🔧 改进构建选项（BUILD_TESTS, BUILD_EXAMPLES, 等）
- 📝 添加 CMake 导出配置
- 🚀 优化编译速度（预编译头、并行编译）

---

## 📐 三、新架构设计

### 文件结构

```
EX_MiracleVision/
├── CMakeLists.txt                    # 主 CMake 文件（简化）
├── vcpkg.json                        # vcpkg 依赖清单 ✅
├── cmake/
│   ├── Dependencies.cmake            # 🆕 所有依赖查找
│   ├── CompilerOptions.cmake         # 🆕 编译选项配置
│   ├── ThirdParty.cmake              # 🆕 第三方库（MindVision, Foxglove）
│   └── Modules.cmake                 # 🆕 项目模块库定义
├── 3rdparty/
│   ├── mindvision/
│   │   └── CMakeLists.txt            # 🆕 MindVision SDK 配置
│   └── foxglove/
│       └── CMakeLists.txt            # 🆕 Foxglove SDK 配置
├── devices/
│   └── CMakeLists.txt                # 🆕 设备层库
├── module/
│   ├── CMakeLists.txt                # 🆕 所有模块的总配置
│   ├── armor/CMakeLists.txt          # 🆕 装甲板模块
│   ├── buff/CMakeLists.txt           # 🆕 能量机关模块
│   ├── angle_solve/CMakeLists.txt    # 🆕 角度解算模块
│   ├── predictor/CMakeLists.txt      # 🆕 预测模块
│   └── foxglove_publisher/CMakeLists.txt  # 已存在，需要更新
├── utils/
│   └── CMakeLists.txt                # 🆕 工具库
├── base/
│   └── CMakeLists.txt                # 🆕 主程序
└── test/
    └── CMakeLists.txt                # 已存在，需要更新
```

### 模块划分

#### 1. 核心库（Core Libraries）
- `mv-core` - 基础类型和工具
- `mv-math-tools` - 数学工具
- `mv-img-tools` - 图像工具
- `mv-logger` - 日志工具

#### 2. 设备层（Device Layer）
- `mv-video-capture` - 相机捕获
- `mv-uart-serial` - 串口通信

#### 3. 算法模块（Algorithm Modules）
- `mv-basic-armor` - 基础装甲板检测
- `mv-fan-armor` - 风车装甲板
- `mv-dnn-armor` - DNN 装甲板
- `mv-basic-buff` - 能量机关检测
- `mv-new-buff` - 新能量机关
- `mv-basic-pnp` - PnP 解算
- `mv-angle-solve` - 角度解算
- `mv-basic-kalman` - 卡尔曼滤波
- `mv-basic-roi` - ROI 处理
- `mv-predictor` - 目标预测
- `mv-onnx-inferring` - ONNX 推理
- `mv-camera-calibration` - 相机标定

#### 4. 集成层（Integration Layer）
- `mv-foxglove-publisher` - Foxglove 发布器
- `mv-plotter` - 数据绘图

#### 5. 应用程序（Applications）
- `MiracleVision` - 主程序
- `minimum_vision` - 测试程序

---

## 🔧 四、详细重构方案

### Phase 1: 准备工作

#### 1.1 更新 vcpkg.json ✅
```json
{
  "name": "ex-miraclevision",
  "version": "0.1.0",
  "dependencies": [
    "fmt",
    "spdlog",
    {
      "name": "opencv4",
      "features": ["ffmpeg", "dnn", "jpeg", "png", "contrib", "tbb", "eigen"]
    },
    "eigen3",
    "yaml-cpp",
    "nlohmann-json",
    "tbb"
  ]
}
```

#### 1.2 创建 CMake 模块目录
```bash
mkdir -p cmake
```

---

### Phase 2: 创建 CMake 辅助文件

#### 2.1 cmake/CompilerOptions.cmake
```cmake
# 编译器选项和标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译标志
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-deprecated)
endif()

# 构建类型特定标志
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# 宏定义
add_compile_definitions(
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:RELEASE>
    SPDLOG_FMT_EXTERNAL
)
```

#### 2.2 cmake/Dependencies.cmake
```cmake
# 查找所有 vcpkg 管理的依赖

# 线程库
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# vcpkg 依赖
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)

message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")

# 系统依赖（OpenVINO 和 ONNX Runtime）
# 这些需要手动安装或通过选项禁用
```

#### 2.3 cmake/ThirdParty.cmake
```cmake
# 第三方库配置（MindVision, Foxglove）

# 选项：是否启用这些库
option(USE_MINDVISION_SDK "Enable MindVision Camera SDK" ON)
option(USE_FOXGLOVE_SDK "Enable Foxglove WebSocket Publisher" ON)
option(USE_OPENVINO "Enable OpenVINO inference" OFF)
option(USE_ONNXRUNTIME "Enable ONNX Runtime inference" OFF)

# MindVision SDK
if(USE_MINDVISION_SDK)
    add_subdirectory(3rdparty/mindvision)
endif()

# Foxglove SDK
if(USE_FOXGLOVE_SDK)
    add_subdirectory(3rdparty/foxglove)
endif()

# OpenVINO
if(USE_OPENVINO)
    set(OpenVINO_DIR /opt/intel/openvino_2024.6.0/runtime/cmake CACHE PATH "OpenVINO directory")
    find_package(OpenVINO REQUIRED COMPONENTS Runtime)
    message(STATUS "OpenVINO found: ${OpenVINO_VERSION}")
endif()

# ONNX Runtime
if(USE_ONNXRUNTIME)
    set(ONNXRUNTIME_ROOT_PATH /usr/local CACHE PATH "ONNX Runtime root")
    set(ONNXRUNTIME_INCLUDE_DIRS 
        ${ONNXRUNTIME_ROOT_PATH}/include/onnxruntime
        ${ONNXRUNTIME_ROOT_PATH}/onnxruntime
    )
    set(ONNXRUNTIME_LIB ${ONNXRUNTIME_ROOT_PATH}/lib/libonnxruntime.so)
    
    if(NOT EXISTS ${ONNXRUNTIME_LIB})
        message(WARNING "ONNX Runtime library not found at ${ONNXRUNTIME_LIB}")
        set(USE_ONNXRUNTIME OFF)
    endif()
endif()
```

---

### Phase 3: 创建子模块 CMakeLists.txt

#### 3.1 3rdparty/mindvision/CMakeLists.txt
```cmake
# MindVision Camera SDK

add_library(MVSDK SHARED IMPORTED)

# 检测系统架构
execute_process(
    COMMAND uname -m
    OUTPUT_VARIABLE ARCHITECTURE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "System architecture: ${ARCHITECTURE}")

# 根据架构选择库
set(MVSDK_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/linux/lib")
if(ARCHITECTURE STREQUAL "x86_64")
    set(MVSDK_LIBRARY "${MVSDK_LIB_DIR}/x64/libMVSDK.so")
elseif(ARCHITECTURE STREQUAL "x86")
    set(MVSDK_LIBRARY "${MVSDK_LIB_DIR}/x86/libMVSDK.so")
elseif(ARCHITECTURE STREQUAL "aarch64")
    set(MVSDK_LIBRARY "${MVSDK_LIB_DIR}/arm64/libMVSDK.so")
elseif(ARCHITECTURE STREQUAL "armv7")
    set(MVSDK_LIBRARY "${MVSDK_LIB_DIR}/arm/libMVSDK.so")
else()
    message(FATAL_ERROR "Unsupported architecture for MindVision SDK: ${ARCHITECTURE}")
endif()

if(NOT EXISTS ${MVSDK_LIBRARY})
    message(FATAL_ERROR "MindVision SDK library not found: ${MVSDK_LIBRARY}")
endif()

set_target_properties(MVSDK PROPERTIES
    IMPORTED_LOCATION ${MVSDK_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/linux/include"
)

message(STATUS "MindVision SDK configured: ${MVSDK_LIBRARY}")
```

#### 3.2 3rdparty/foxglove/CMakeLists.txt
```cmake
# Foxglove WebSocket SDK

add_library(foxglove_sdk INTERFACE)

target_include_directories(foxglove_sdk INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# 添加库搜索路径
set(FOXGLOVE_LIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib")
link_directories(${FOXGLOVE_LIB_PATH})

target_link_libraries(foxglove_sdk INTERFACE foxglove)

# 设置 RPATH
list(APPEND CMAKE_BUILD_RPATH ${FOXGLOVE_LIB_PATH})
list(APPEND CMAKE_INSTALL_RPATH ${FOXGLOVE_LIB_PATH})

message(STATUS "Foxglove SDK configured")
```

#### 3.3 utils/CMakeLists.txt
```cmake
# Utils 库

# mv-img-tools
add_library(mv-img-tools SHARED img_tools.cpp)
target_link_libraries(mv-img-tools
    PUBLIC
    opencv_core
    opencv_imgproc
    fmt::fmt
)
target_include_directories(mv-img-tools PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/utils>
)

# mv-plotter
add_library(mv-plotter SHARED plotter.cpp)
target_link_libraries(mv-plotter
    PUBLIC
    nlohmann_json::nlohmann_json
    fmt::fmt
)
target_include_directories(mv-plotter PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/utils>
)

# mv-logger
add_library(mv-logger OBJECT logger.cpp)
target_link_libraries(mv-logger PUBLIC spdlog::spdlog)

# mv-math-tools
add_library(mv-math-tools OBJECT math_tools.cpp)
target_link_libraries(mv-math-tools PUBLIC opencv_core)
```

#### 3.4 devices/CMakeLists.txt
```cmake
# Devices 库

# mv-video-capture
add_library(mv-video-capture SHARED camera/mv_video_capture.cpp)
target_link_libraries(mv-video-capture
    PUBLIC
    MVSDK
    fmt::fmt
    opencv_core
    opencv_imgproc
)
target_include_directories(mv-video-capture PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/devices>
)

# mv-uart-serial
add_library(mv-uart-serial SHARED serial/uart_serial.cpp)
target_link_libraries(mv-uart-serial PUBLIC fmt::fmt)
target_include_directories(mv-uart-serial PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/devices>
)
```

#### 3.5 module/CMakeLists.txt（部分示例）
```cmake
# 所有算法模块

# angle_solve 模块
add_library(mv-basic-pnp SHARED angle_solve/basic_pnp.cpp)
target_link_libraries(mv-basic-pnp
    PUBLIC
    opencv_core
    opencv_calib3d
    fmt::fmt
)

add_library(mv-angle-solve SHARED angle_solve/angle_solve.cpp)
target_link_libraries(mv-angle-solve
    PUBLIC
    mv-basic-pnp
    fmt::fmt
)

# armor 模块
add_library(mv-basic-armor SHARED armor/basic_armor.cpp)
target_link_libraries(mv-basic-armor
    PUBLIC
    opencv_core
    opencv_imgproc
    opencv_highgui
    fmt::fmt
)

# ... 其他模块类似 ...

# Foxglove publisher
add_subdirectory(foxglove_publisher)
```

---

### Phase 4: 主 CMakeLists.txt 重构

```cmake
cmake_minimum_required(VERSION 3.15)
project(MiracleVision 
    VERSION 2024.2.20
    LANGUAGES CXX
    DESCRIPTION "BJFU RoboMaster 2026 Vision System"
)

# ============================================================================
# 项目选项
# ============================================================================
option(BUILD_TESTS "Build test programs" OFF)
option(BUILD_MAIN "Build main MiracleVision executable" ON)
option(USE_MINDVISION_SDK "Enable MindVision Camera SDK" ON)
option(USE_FOXGLOVE_SDK "Enable Foxglove WebSocket Publisher" ON)
option(USE_OPENVINO "Enable OpenVINO inference" OFF)
option(USE_ONNXRUNTIME "Enable ONNX Runtime inference" OFF)

# 检查平台
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Only Linux is supported")
endif()

# ============================================================================
# CMake 模块
# ============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)
include(Dependencies)
include(ThirdParty)

# ============================================================================
# 配置路径定义
# ============================================================================
set(CONFIG_FILE_PATH "${CMAKE_SOURCE_DIR}/configs" CACHE PATH "Config file path")
set(SOURCE_PATH "${CMAKE_SOURCE_DIR}" CACHE PATH "Source path")

# ============================================================================
# 子目录
# ============================================================================
add_subdirectory(utils)
add_subdirectory(devices)
add_subdirectory(module)

if(BUILD_MAIN)
    add_subdirectory(base)
endif()

if(BUILD_TESTS)
    add_subdirectory(test)
endif()

# ============================================================================
# 输出路径
# ============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# 安装配置（可选）
# ============================================================================
# install() 规则...
```

---

## 📊 五、重构步骤（执行顺序）

### Step 1: 备份和准备 ✅
```bash
# 1. 备份当前 CMakeLists.txt
cp CMakeLists.txt CMakeLists.txt.backup

# 2. 创建目录结构
mkdir -p cmake

# 3. 确认 vcpkg.json 已更新
```

### Step 2: 创建辅助 CMake 文件 🔄
1. 创建 `cmake/CompilerOptions.cmake`
2. 创建 `cmake/Dependencies.cmake`
3. 创建 `cmake/ThirdParty.cmake`

### Step 3: 创建第三方库配置 🔄
1. 创建 `3rdparty/mindvision/CMakeLists.txt`
2. 创建 `3rdparty/foxglove/CMakeLists.txt`

### Step 4: 创建模块 CMakeLists.txt 🔄
1. 创建 `utils/CMakeLists.txt`
2. 创建 `devices/CMakeLists.txt`
3. 创建 `module/CMakeLists.txt`
4. 更新 `module/foxglove_publisher/CMakeLists.txt`
5. 创建 `base/CMakeLists.txt`
6. 更新 `test/CMakeLists.txt`

### Step 5: 重写主 CMakeLists.txt 🔄
1. 备份原文件
2. 用新的简化版本替换
3. 测试编译

### Step 6: 测试和验证 ⏳
```bash
# 配置（使用 vcpkg）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=[vcpkg路径]/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)

# 测试
./build/bin/MiracleVision
```

---

## ⚠️ 六、潜在风险和注意事项

### 风险

1. **OpenVINO 和 ONNX Runtime 路径问题**
   - 这些库通常不在 vcpkg 中
   - 需要保持灵活的查找机制
   - 建议作为可选依赖

2. **第三方 SDK 兼容性**
   - MindVision SDK 和 Foxglove SDK 路径依赖
   - 需要确保 RPATH 设置正确

3. **编译依赖顺序**
   - 某些模块间有依赖关系
   - 需要正确组织 `add_subdirectory()` 顺序

### 兼容性考虑

1. **保持向后兼容**
   - 保留 `CMakeLists.txt.backup`
   - 可以快速回退

2. **环境变量**
   - `CONFIG_FILE_PATH` 和 `SOURCE_PATH` 保持不变
   - 现有脚本应该仍然有效

3. **构建产物位置**
   - 保持 `build/bin/` 和 `build/lib/` 结构

---

## 🎯 七、预期收益

### 短期收益
- ✅ 更清晰的项目结构
- ✅ 更容易添加新模块
- ✅ 更好的 IDE 支持（IntelliSense）
- ✅ 减少编译错误

### 长期收益
- ✅ 更容易维护和升级依赖
- ✅ 支持跨平台（如果未来需要）
- ✅ 可以作为库被其他项目使用
- ✅ 更好的 CI/CD 集成

---

## 📝 八、下一步行动

### 需要确认的问题

1. **是否需要保留 OpenVINO 支持？**
   - 如果是，路径是否可以灵活配置？

2. **是否需要保留 ONNX Runtime 支持？**
   - 还是只用 OpenCV DNN？

3. **是否需要编译所有测试程序？**
   - 还是默认只编译主程序？

4. **Foxglove 和 MindVision SDK 是否必需？**
   - 还是可以作为可选组件？

### 推荐执行方式

**方案 A: 渐进式重构（推荐）**
1. 先创建所有辅助文件
2. 保持主 CMakeLists.txt 不变
3. 逐步迁移模块
4. 最后替换主文件

**方案 B: 一次性重构**
1. 备份当前文件
2. 一次性创建所有新文件
3. 测试编译
4. 如有问题回退

---

## 🤝 九、需要你的反馈

请确认以下问题，我们再开始实施：

1. ✅ vcpkg.json 是否移除 `vtkgtk`？
2. ❓ OpenVINO 是否保留？路径是否需要可配置？
3. ❓ ONNX Runtime 是否保留？
4. ❓ 是否希望 MindVision 和 Foxglove 可选？
5. ❓ 是否需要支持将来的跨平台（Windows/macOS）？
6. ❓ 是否采用渐进式重构还是一次性重构？

---

**准备好后，我们可以开始逐步实施！** 🚀
