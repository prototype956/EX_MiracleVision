# ============================================================================
# Base 主程序
# ============================================================================

message(STATUS "  Configuring main executable...")

# ----------------------------------------------------------------------------
# MiracleVision - 主程序
# ----------------------------------------------------------------------------
add_executable(MiracleVision
    MiracleVision.cpp
    ${CMAKE_SOURCE_DIR}/utils/plotter.cpp
)

# 包含路径
target_include_directories(MiracleVision PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/module
    ${CMAKE_SOURCE_DIR}/devices
    ${CMAKE_SOURCE_DIR}/utils
)

# 链接依赖 - 核心库
target_link_libraries(MiracleVision
    PRIVATE
    # 线程库
    Threads::Threads

    # vcpkg 依赖
    fmt::fmt
    spdlog::spdlog
    yaml-cpp
    nlohmann_json::nlohmann_json
    TBB::tbb

    # OpenCV
    opencv_core
    opencv_imgproc
    opencv_highgui
    opencv_videoio
    opencv_imgcodecs
    opencv_calib3d
    opencv_dnn

    # 设备层
    mv-uart-serial

    # 算法模块
    mv-basic-armor
    mv-fan-armor
    mv-basic-buff
    mv-new-buff
    mv-basic-pnp
    mv-angle-solve
    mv-basic-kalman
    mv-basic-roi
    mv-onnx-inferring
    mv-camera-calibration
    mv-predictor

    # 工具库
    mv-img-tools
    mv-plotter
)

# 如果启用了 Foxglove
if(TARGET mv-foxglove-publisher)
    target_link_libraries(MiracleVision PRIVATE mv-foxglove-publisher)
endif()

# 如果启用了 MindVision SDK
if(USE_MINDVISION_SDK AND TARGET mv-video-capture)
    target_link_libraries(MiracleVision PRIVATE mv-video-capture)
    # 修复CI时的编译错误：
    # 由于 mv-video-capture 是 PRIVATE 链接，HAS_MVSDK 宏没有传播到主程序，导致头文件中的 #ifdef HAS_MVSDK 条件编译失败
    target_compile_definitions(MiracleVision PRIVATE HAS_MVSDK)
endif()

# 如果启用了 ONNX Runtime
if(USE_ONNXRUNTIME AND TARGET mv-dnn-armor)
    target_link_libraries(MiracleVision PRIVATE mv-dnn-armor)
    target_compile_definitions(MiracleVision PRIVATE USE_ONNXRUNTIME)
endif()

# 如果启用了 OpenVINO
if(USE_OPENVINO AND TARGET openvino::runtime)
    target_link_libraries(MiracleVision PRIVATE openvino::runtime)
endif()

# 编译定义
target_compile_definitions(MiracleVision PRIVATE
    CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
    SOURCE_PATH="${SOURCE_PATH}"
)

# 编译特性
target_compile_features(MiracleVision PRIVATE cxx_std_17)

# 属性设置
set_target_properties(MiracleVision PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    INTERPROCEDURAL_OPTIMIZATION OFF
    # 设置 RPATH 以便找到共享库
    BUILD_RPATH "${CMAKE_BINARY_DIR}/lib;${CMAKE_BINARY_DIR}/devices;${CMAKE_SOURCE_DIR}/3rdparty/mindvision/linux/lib/x64"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
)

# GCC 12 在 -O2 下编译 fmt 会出现 ICE,降低主程序优化等级
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 12.0)
    target_compile_options(MiracleVision PRIVATE -O0 -g)
    message(STATUS "    ! GCC 12+ detected, using -O0 for main executable to avoid ICE")
endif()

message(STATUS "    ✓ Main executable configured")
