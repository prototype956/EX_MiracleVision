# ============================================================================# Remake on 2024-02-20 by dgsyrc

# EX_MiracleVision - BJFU RoboMaster 2026 Vision System# For RoboMaster

# ============================================================================

# 重构日期: 2026-02-11# Create project

# 依赖管理: vcpkgcmake_minimum_required(VERSION 3.10.0)

# 平台支持: Linux onlyproject(MiracleVision VERSION 2024.2.20 LANGUAGES CXX)

# ============================================================================

# Check system platform

cmake_minimum_required(VERSION 3.15)if(NOT CMAKE_SYSTEM_NAME STREQUAL Linux)

    message(FATAL_ERROR "Not supported platform: ${CMAKE_SYSTEM_NAME}")

project(MiracleVision endif()

    VERSION 2024.2.20

    LANGUAGES CXX# Specialize CMake build flags for different build types

    DESCRIPTION "BJFU RoboMaster 2026 Vision System"set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-deprecated")

)set(CMAKE_CXX_FLAGS_DEBUG "-g")

set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# ============================================================================

# 项目选项

# ============================================================================# Use standard c++20

set(CMAKE_CXX_STANDARD 17)

# 构建选项set(CMAKE_CXX_STANDARD_REQUIRED True)

option(BUILD_TESTS "Build test programs" OFF)

option(BUILD_MAIN "Build main MiracleVision executable" ON)# Add DEBUG/RELEASE marco to define if debug/release configured

add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>")

# 第三方库选项add_compile_options("$<$<CONFIG:RELEASE>:-DRELEASE>")

option(USE_MINDVISION_SDK "Enable MindVision Camera SDK (required for real camera)" ON)

option(USE_FOXGLOVE_SDK "Enable Foxglove WebSocket Publisher (for remote debugging)" ON)# Don't build test by default

option(USE_OPENVINO "Enable OpenVINO inference (traditional vision without ONNX)" OFF)set(BUILD_TEST False)

option(USE_ONNXRUNTIME "Enable ONNX Runtime inference (traditional vision without OpenVINO)" OFF)

# Prefer posix threads

# ============================================================================set(THREADS_PREFER_PTHREAD_FLAG True)

# 平台检查set(CMAKE_THREAD_LIBS_INIT "-lpthread")

# ============================================================================find_package(Threads REQUIRED)



if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")# Find and add Eigen3、spdlog、yaml-cpp、fmt、OpenVINO as required

    message(FATAL_ERROR "Only Linux platform is supported!")find_package(Eigen3 REQUIRED)

endif()find_package(spdlog REQUIRED)

find_package(yaml-cpp REQUIRED)

message(STATUS "")find_package(fmt REQUIRED)

message(STATUS "============================================================")find_package(nlohmann_json REQUIRED)

message(STATUS "  EX_MiracleVision Configuration")

message(STATUS "============================================================")# Fix spdlog fmt conflict

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")add_compile_definitions(SPDLOG_FMT_EXTERNAL)

message(STATUS "System: ${CMAKE_SYSTEM_NAME}")

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")# OpenVINO

message(STATUS "")set(OpenVINO_DIR /opt/intel/openvino_2024.6.0/runtime/cmake)

find_package(OpenVINO REQUIRED COMPONENTS Runtime)

# ============================================================================message(STATUS "OpenVINO found: ${OpenVINO_VERSION}")

# CMake 模块路径

# ============================================================================list(APPEND EXTRA_INCLUDES ${Eigen3_INCLUDE_DIR})

list(APPEND EXTRA_INCLUDES ${spdlog_INCLUDE_DIR})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")list(APPEND EXTRA_INCLUDES ${yaml-cpp_INCLUDE_DIR})



# ============================================================================# Find and add opencv as required (explicitly require 4.5 to avoid version conflicts)

# 包含 CMake 配置模块# Prioritize system OpenCV over /usr/local to avoid 4.05 conflict

# ============================================================================set(CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu" ${CMAKE_PREFIX_PATH})

find_package(OpenCV 4.5 REQUIRED)

message(STATUS "Loading CMake modules...")message(STATUS "OpenCV version: ${OpenCV_VERSION}")

message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")

# 编译器选项message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")

include(CompilerOptions)include_directories(${OpenCV_INCLUDE_DIRS})

link_libraries(${OpenCV_LIBS})

# vcpkg 依赖查找

include(Dependencies)# Use pkg-config to find GTK and other dependencies

find_package(PkgConfig REQUIRED)

# 第三方库配置pkg_check_modules(GTK2 REQUIRED gtk+-2.0)

include(ThirdParty)pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)



message(STATUS "")#onnxruntime

set(ONNXRUNTIME_ROOT_PATH /usr/local)

# ============================================================================set(ONNXRUNTIME_INCLUDE_DIRS ${ONNXRUNTIME_ROOT_PATH}/include/onnxruntime

# 配置路径定义                             ${ONNXRUNTIME_ROOT_PATH}/onnxruntime)

# ============================================================================set(ONNXRUNTIME_LIB ${ONNXRUNTIME_ROOT_PATH}/lib/libonnxruntime.so)



set(CONFIG_FILE_PATH "${CMAKE_SOURCE_DIR}/configs" # Add fmtlib directory

    CACHE PATH "Configuration files directory")# add_subdirectory(3rdparty/fmt)

set(SOURCE_PATH "${CMAKE_SOURCE_DIR}" # Add fmtlib include to EXTRA_INCLUDES

    CACHE PATH "Source code root directory")# list(APPEND EXTRA_INCLUDES 3rdparty/fmt/include)

set(SAVE_FILE_PATH "${CMAKE_SOURCE_DIR}/configs/camera" 

    CACHE PATH "Save file directory")# Add mindvision-sdk directory and include path for target

add_library(MVSDK SHARED IMPORTED)

message(STATUS "Path configuration:")get_filename_component(MVSDK_PATH 3rdparty/mindvision/linux REALPATH)

message(STATUS "  Source path: ${SOURCE_PATH}")# Check system architecture for mindvision-sdk library

message(STATUS "  Config path: ${CONFIG_FILE_PATH}")execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)

message(STATUS "  Save path: ${SAVE_FILE_PATH}")message(STATUS "Checking platform architecture for MVSDK: ${ARCHITECTURE}")

message(STATUS "")if(${ARCHITECTURE} STREQUAL x86_64)

    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/x64/libMVSDK.so)

# ============================================================================elseif(${ARCHITECTURE} STREQUAL x86)

# 添加子目录    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/x86/libMVSDK.so)

# ============================================================================elseif(${ARCHITECTURE} STREQUAL aarch64)

    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/arm64/libMVSDK.so)

message(STATUS "Configuring project modules...")elseif(${ARCHITECTURE} STREQUAL armv7)

    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/arm/libMVSDK.so)

# 工具库else()

add_subdirectory(utils)    message(FATAL_ERROR "Not supported architecture for MVSDK: ${ARCHITECTURE}")

endif()

# 设备层set_property(TARGET MVSDK PROPERTY IMPORTED_LOCATION ${MIMDVISION_SDK_LIBRARY})

add_subdirectory(devices)

# --- Foxglove SDK 定义 ---

# 算法模块# 使用 INTERFACE 库而非 IMPORTED SHARED，以强制使用 -lfoxglove 链接方式

add_subdirectory(module)# 这样可以避免因缺少 SONAME 导致链接器嵌入相对/绝对路径的问题

# 同时配合 link_directories 让链接器找到库

# 主程序add_library(foxglove_sdk INTERFACE)

if(BUILD_MAIN)

    add_subdirectory(base)target_include_directories(foxglove_sdk INTERFACE 

endif()    "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/include"

)

# 测试程序

if(BUILD_TESTS)# 添加库搜索路径 (供链接器使用)

    add_subdirectory(test)link_directories("${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib")

endif()

# 链接库名 (这会生成 -lfoxglove)

message(STATUS "")target_link_libraries(foxglove_sdk INTERFACE foxglove)



# ============================================================================# 显式将 Foxglove 库目录添加到构建时的 RPATH 中

# 为特定库添加编译定义# 这样运行程序时能自动找到库，无需设置 LD_LIBRARY_PATH

# ============================================================================# CMAKE_INSTALL_RPATH_USE_LINK_PATH 如果为 TRUE 也会有帮助，但这里我们直接添加自定义路径

list(APPEND CMAKE_BUILD_RPATH "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib")

# 为需要配置文件路径的库添加定义list(APPEND CMAKE_INSTALL_RPATH "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib")

if(TARGET mv-basic-buff)

    target_compile_definitions(mv-basic-buff PRIVATE # Add mindvision lib to build target

        CONFIG_FILE_PATH="${CONFIG_FILE_PATH}")list(APPEND EXTRA_LIBS MVSDK)

endif()# Add mindvision include path

list(APPEND EXTRA_INCLUDES 3rdparty/mindvision/linux/include)

if(TARGET mv-onnx-inferring)

    target_compile_definitions(mv-onnx-inferring PRIVATE # Add project source dir to include

        CONFIG_FILE_PATH="${CONFIG_FILE_PATH}")list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR})

endif()

# Add base src and header

if(TARGET mv-camera-calibration)list(APPEND EXTRA_INCLUDES base)

    target_compile_definitions(mv-camera-calibration PRIVATE 

        SAVE_FILE_PATH="${SAVE_FILE_PATH}")# Add device as shared library to build target

endif()list(APPEND EXTRA_INCLUDES devices)



if(TARGET mv-predictor)list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/devices/camera)

    target_compile_definitions(mv-predictor PRIVATE add_library(mv-video-capture SHARED devices/camera/mv_video_capture.cpp)

        CONFIG_FILE_PATH="${CONFIG_FILE_PATH}")target_link_libraries(mv-video-capture

endif()    PRIVATE

    MVSDK

# ============================================================================    fmt::fmt

# 输出目录配置)

# ============================================================================list(APPEND MV_EXTRA_LIBS mv-video-capture)



set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/devices/serial)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)add_library(mv-uart-serial SHARED devices/serial/uart_serial.cpp)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)target_link_libraries(mv-uart-serial PUBLIC fmt::fmt)

list(APPEND MV_EXTRA_LIBS mv-uart-serial)

# ============================================================================

# 构建总结# Add module as shared library to build target

# ============================================================================list(APPEND EXTRA_INCLUDES module)



message(STATUS "============================================================")list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/angle_solve)

message(STATUS "  Configuration Summary")add_library(mv-basic-pnp SHARED module/angle_solve/basic_pnp.cpp)

message(STATUS "============================================================")target_link_libraries(mv-basic-pnp PUBLIC fmt::fmt)

message(STATUS "Build options:")list(APPEND MV_EXTRA_LIBS mv-basic-pnp)

message(STATUS "  BUILD_MAIN:         ${BUILD_MAIN}")

message(STATUS "  BUILD_TESTS:        ${BUILD_TESTS}")list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/angle_solve)

message(STATUS "")add_library(mv-angle-solve SHARED module/angle_solve/angle_solve.cpp)

message(STATUS "Third-party libraries:")target_link_libraries(mv-angle-solve PUBLIC fmt::fmt)

message(STATUS "  MindVision SDK:     ${USE_MINDVISION_SDK}")list(APPEND MV_EXTRA_LIBS mv-angle-solve)

message(STATUS "  Foxglove SDK:       ${USE_FOXGLOVE_SDK}")

message(STATUS "  OpenVINO:           ${USE_OPENVINO}")list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/buff)

message(STATUS "  ONNX Runtime:       ${USE_ONNXRUNTIME}")add_library(mv-new-buff SHARED module/buff/new_buff.cpp)

message(STATUS "")target_link_libraries(mv-new-buff PUBLIC fmt::fmt)

message(STATUS "Output directories:")list(APPEND MV_EXTRA_LIBS mv-new-buff)

message(STATUS "  Executables:        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

message(STATUS "  Libraries:          ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")

message(STATUS "============================================================")list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/armor)

message(STATUS "")add_library(mv-basic-armor SHARED module/armor/basic_armor.cpp)

message(STATUS "Configuration completed successfully!")target_link_libraries(mv-basic-armor PUBLIC fmt::fmt)

message(STATUS "Run 'cmake --build build' to compile the project.")list(APPEND MV_EXTRA_LIBS mv-basic-armor)

message(STATUS "")add_library(mv-fan-armor SHARED module/armor/fan_armor.cpp)

target_link_libraries(mv-fan-armor PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-fan-armor)
add_library(mv-dnn-armor SHARED module/armor/DNN_armor.cpp)
target_link_libraries(mv-dnn-armor PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-dnn-armor)


list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/buff)
add_library(mv-basic-buff SHARED module/buff/basic_buff.cpp)
target_link_libraries(mv-basic-buff
    PRIVATE
    mv-fan-armor
    mv-new-buff
    fmt::fmt
)
list(APPEND MV_EXTRA_LIBS mv-basic-buff)



list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/filter)
add_library(mv-basic-kalman SHARED module/filter/basic_kalman.cpp)
target_link_libraries(mv-basic-kalman PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-basic-kalman)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/ml)
add_library(mv-onnx-inferring SHARED module/ml/onnx_inferring.cpp)
target_link_libraries(mv-onnx-inferring PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-onnx-inferring)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/roi)
add_library(mv-basic-roi SHARED module/roi/basic_roi.cpp)
target_link_libraries(mv-basic-roi PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-basic-roi)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/camera)
add_library(mv-camera-calibration SHARED module/camera/camera_calibration.cpp)
target_link_libraries(mv-camera-calibration PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-camera-calibration)

# Add utils header
list(APPEND EXTRA_INCLUDES utils)

# Add utils libraries
add_library(mv-img-tools SHARED utils/img_tools.cpp)
target_link_libraries(mv-img-tools 
    PUBLIC 
    ${OpenCV_LIBS}
    fmt::fmt
)
list(APPEND MV_EXTRA_LIBS mv-img-tools)

add_library(mv-plotter SHARED utils/plotter.cpp)
target_link_libraries(mv-plotter 
    PUBLIC 
    nlohmann_json::nlohmann_json
    fmt::fmt
)
list(APPEND MV_EXTRA_LIBS mv-plotter)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/predictor)
add_library(mv-predictor SHARED 
            module/predictor/tracker.cpp
            module/predictor/solver.cpp
            module/predictor/extended_kalman_filter.cpp
            module/predictor/armor.cpp
            module/predictor/target.cpp
            utils/logger.cpp
            utils/math_tools.cpp
)
target_link_libraries(mv-predictor
    PUBLIC
    ${OpenCV_LIBS}
    Eigen3::Eigen
    fmt::fmt
    yaml-cpp
)
list(APPEND MV_EXTRA_LIBS mv-predictor)

# 引入新开发的 foxglove 模块
add_subdirectory(module/foxglove_publisher)
list(APPEND MV_EXTRA_LIBS mv-foxglove-publisher)

# Add EXTRA_INCLUDES to project includes
include_directories(${EXTRA_INCLUDES})

# ============================================================================
# 方案 A: 只编译库，不编译主程序
# 主程序 MiracleVision 因为依赖过多而暂时禁用
# 你可以使用编译好的 libsp_algo.so 和 libmv-io.so 库
# ============================================================================
# 如果需要编译主程序，请取消下面的注释并安装所有依赖

add_executable(MiracleVision base/MiracleVision.cpp utils/plotter.cpp)
target_link_libraries(MiracleVision
    PUBLIC
    Threads::Threads
    fmt::fmt
    ${OpenCV_LIBS}
    ${EXTRA_LIBS}
    ${ONNXRUNTIME_LIB}
    openvino::runtime
    PRIVATE
    ${MV_EXTRA_LIBS}
    spdlog::spdlog
    yaml-cpp
    nlohmann_json::nlohmann_json
    ${GTK2_LIBRARIES}
    ${FFMPEG_LIBRARIES}
    tbb
)
# Set configs folder absolute path
# define
set(CONFIG_FILE_PATH ${PROJECT_SOURCE_DIR}/configs)
set(SOURCE_PATH ${PROJECT_SOURCE_DIR})

target_compile_features(MiracleVision PUBLIC cxx_std_${CMAKE_CXX_STANDARD})
set_target_properties(MiracleVision PROPERTIES INTERPROCEDURAL_OPTIMIZATION False)

target_compile_definitions(MiracleVision PRIVATE "CONFIG_FILE_PATH=\"${CONFIG_FILE_PATH}\"")
target_compile_definitions(MiracleVision PRIVATE "SOURCE_PATH=\"${SOURCE_PATH}\"")

# Set built binary to ~/bin
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")

# Set built library to ~/lib
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")
set(SAVE_FILE_PATH ${PROJECT_SOURCE_DIR}/configs/camera)
target_compile_definitions(mv-basic-buff PRIVATE "CONFIG_FILE_PATH=\"${CONFIG_FILE_PATH}\"")
target_compile_definitions(mv-onnx-inferring PRIVATE "CONFIG_FILE_PATH=\"${CONFIG_FILE_PATH}\"")
target_compile_definitions(mv-camera-calibration PRIVATE "SAVE_FILE_PATH=\"${SAVE_FILE_PATH}\"")
target_compile_definitions(mv-predictor PRIVATE "CONFIG_FILE_PATH=\"${CONFIG_FILE_PATH}\"")


# Add test_camera directory
if(BUILD_TEST)
    add_subdirectory(test/test_camera)
endif()

add_subdirectory(test)




