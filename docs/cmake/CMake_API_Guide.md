# EX_MiracleVision: 现代 CMake 构建系统开发指南

本项目采用 **Modern CMake (现代 CMake)** 范式进行重构和管理。与传统的 CMake 脚本（依赖全局变量、`include_directories` 和 `link_libraries`）不同，现代 CMake 强调**以“目标 (Target)”为核心**的属性封装系统。

本指南用于指导开发者如何查询项目中的 Target（目标），以及在日常开发中若添加了新模块、新测试或修改主程序时，如何正确地编写或修改 `CMakeLists.txt`。

---

## 1. 核心概念：什么是 Target？

在现代 CMake 中，一切皆为 Target。一个 Target 可以是：
* **可执行文件 (Executable)**: 使用 `add_executable()` 创建。
* **库 (Library)**: 使用 `add_library()` 创建（常为共享库 `SHARED` 或静态库 `STATIC`）。

每个 Target 都有自己的私有和公有属性（如头文件包含路径、链接库、编译宏）。通过 `PUBLIC`, `PRIVATE`, `INTERFACE` 权限修饰符，CMake 会自动处理依赖传递，无需您手动写一长串绝对路径。

### 权限修饰符速查：
* **`PRIVATE`**: 仅当前 Target 可以使用这个依赖或路径。
* **`INTERFACE`**: 当前 Target 本身不需要，但**依赖它的**其他 Target 需要。
* **`PUBLIC`**: 当前 Target 本身需要，**且依赖它的**其他 Target 也需要。

---

## 2. 如何查询当前项目中的 Target 列表

项目目前将各个功能打散成了以 `mv-` 为前缀的动态链接库。
常用的算法和设备库目标名称（在 `module/CMakeLists.txt` 和 `devices/CMakeLists.txt` 中定义）如下：

| Target 名称 | 描述 | 位置 |
| :--- | :--- | :--- |
| `mv-basic-pnp` | 基础相机姿态 PnP 解算 | `module/angle_solve/` |
| `mv-angle-solve` | 上层角度解算整合模块 | `module/angle_solve/` |
| `mv-basic-armor` | 传统装甲板识别模块 | `module/armor/` |
| `mv-fan-armor` | 扇叶装甲板辅助模块 | `module/armor/` |
| `mv-dnn-armor` | 深度学习装甲板模块 | `module/armor/` |
| `mv-basic-buff` | 传统能量机关识别 | `module/buff/` |
| `mv-new-buff` | 新版能量机关 | `module/buff/` |
| `mv-basic-kalman` | 基础卡尔曼滤波 | `module/filter/` |
| `mv-onnx-inferring`| ONNX / OpenVINO 推理引擎 | `module/ml/` |
| `mv-predictor` | 弹道及运动预测核心 | `module/predictor/` |

**在 CMake 代码中查询某个 Target 是否存在：**
如果您的代码依赖某些可选组件（例如某些第三方闭源 SDK），可以使用 `if(TARGET <target_name>)` 进行条件编译：
```cmake
if(TARGET mv-foxglove-publisher)
    target_link_libraries(my_new_app PRIVATE mv-foxglove-publisher)
endif()
```

---

## 3. 实战：如何新增一个算法模块 (Module)

当您在 `module/` 目录下（例如 `module/super_aim/`）新建了一个算法模块时，您需要在 `module/CMakeLists.txt` 中注册它。

**添加示例如下：**

```cmake
# 1. 声明库目标 (建议以 mv- 开头)
add_library(mv-super-aim SHARED super_aim/super_aim.cpp)

# 2. 声明它依赖的其他库 (大部分模块只需要 PUBLIC 依赖 opencv 和 fmt)
target_link_libraries(mv-super-aim
    PUBLIC
    mv-basic-kalman    # 如果用到了项目内其他模块，写在这里
    opencv_core
    opencv_imgproc
    fmt::fmt
)

# 3. 声明对外暴露的头文件目录 (极为重要的一步)
target_include_directories(mv-super-aim PUBLIC
    # 允许别人用 #include "super_aim/super_aim.hpp" 或 #include "super_aim.hpp" 的方式引用
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/super_aim>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)
```
**说明：** 使用 `$<BUILD_INTERFACE:...>` 是一种高级用法，这保证了编译阶段能正确找到源文件，而出于发布或打包目的时，路径不会被硬编码污染。

---

## 4. 实战：如何新增一个独立的测试程序 (Test)

为了调试新写的 `super_aim` 模块，您可能会在 `test/` 目录下新建一个 `test_super_aim.cpp`。
要编译它，请修改 `test/CMakeLists.txt`：

```cmake
# 1. 在 test/CMakeLists.txt 中添加可执行文件
add_executable(test_super_aim test_super_aim.cpp)

# 2. 为此测试程序添加必要的私有(PRIVATE)包含路径
target_include_directories(test_super_aim PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/module
)

# 3. 链接目标模块以及系统依赖
target_link_libraries(test_super_aim PRIVATE
    mv-super-aim           # 只需要链接你正在测试的核心模块！(它会自动拉取 OpenCV 等 PUBLIC 依赖)
    mv-img-tools           # 如果测试用到了绘图工具
    fmt::fmt               # 打印库
    opencv_highgui         # 测试经常需要弹窗 imshow
)

# 4. 其它必要属性设置 (照抄已有测试即可)
target_compile_definitions(test_super_aim PRIVATE
    SOURCE_PATH="${CMAKE_SOURCE_DIR}"
    CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
)
set_target_properties(test_super_aim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    CXX_STANDARD 17
)
```
重新运行 `cd build && cmake .. && make -j` 即可在 `build/bin/` 目录下得到 `test_super_aim` 独立程序。

---

## 5. 实战：如何将新模块接入主程序 (Main)

在您千锤百炼彻底调试好 `super_aim` 后，是时候将它挂载到 `MiracleVision` 战车主程序里了。
主程序的配置在 `base/CMakeLists.txt`。

您只需要做**一件事**：修改 `target_link_libraries` 片段！

```cmake
# base/CMakeLists.txt 内部片段:
target_link_libraries(MiracleVision
    PRIVATE
    # ... 其他库 ...

    # 算法模块
    mv-basic-armor
    mv-fan-armor
    # ... 现有算法模块 ...
    mv-super-aim    # <---- 这是您唯一需要增加的一行代码！

    # 工具库
    # ...
)
```
因为现代 CMake 的**依赖传递特性**，只要您在 `module/CMakeLists.txt` 里的 `target_include_directories` 声明了 `PUBLIC` 权限，主程序链接 `mv-super-aim` 后，**自动就能搜索到它的头文件并正确编译**。无需在主程序中再补写一堆 `include_directories(...)`。

---

## 6. 特殊情况：第三方宏定义与编译特性

如果您的某些模块/程序必须知道配置文件的绝对物理路径以便读取 YAML：

可以使用 `target_compile_definitions` 注入宏定义：
```cmake
target_compile_definitions(mv-super-aim PRIVATE
    CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
)
```
这样代码中就可以直接使用宏: `std::string path = CONFIG_FILE_PATH;`。

## 结语

通过将复杂的构建逻辑隔离在各个模块私有的 CMake 中，本项目的 `CMakeLists.txt` 将永远保持清爽和易于维护。记住核心心法：**以 Target 为中心，按需链接，区分 PUBLIC 和 PRIVATE！**
