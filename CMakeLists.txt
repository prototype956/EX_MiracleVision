# Remake on 2024-02-20 by dgsyrc
# For RoboMaster

# Create project
cmake_minimum_required(VERSION 3.10.0)
project(MiracleVision VERSION 2024.2.20 LANGUAGES CXX)

# Check system platform
if(NOT CMAKE_SYSTEM_NAME STREQUAL Linux)
    message(FATAL_ERROR "Not supported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# Specialize CMake build flags for different build types
set(CMAKE_CXX_FLAGS "-Wall -Wextra -Wno-deprecated")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")


# Use standard c++20
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add DEBUG/RELEASE marco to define if debug/release configured
add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>")
add_compile_options("$<$<CONFIG:RELEASE>:-DRELEASE>")

# Don't build test by default
set(BUILD_TEST False)

# Prefer posix threads
set(THREADS_PREFER_PTHREAD_FLAG True)
set(CMAKE_THREAD_LIBS_INIT "-lpthread")
find_package(Threads REQUIRED)

# Find and add Eigen3、spdlog、yaml-cpp、fmt、OpenVINO as required
find_package(Eigen3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)

# Fix spdlog fmt conflict
add_compile_definitions(SPDLOG_FMT_EXTERNAL)

# OpenVINO
set(OpenVINO_DIR /opt/intel/openvino_2024.6.0/runtime/cmake)
find_package(OpenVINO REQUIRED COMPONENTS Runtime)
message(STATUS "OpenVINO found: ${OpenVINO_VERSION}")

list(APPEND EXTRA_INCLUDES ${Eigen3_INCLUDE_DIR})
list(APPEND EXTRA_INCLUDES ${spdlog_INCLUDE_DIR})
list(APPEND EXTRA_INCLUDES ${yaml-cpp_INCLUDE_DIR})

# Find and add opencv as required (explicitly require 4.5 to avoid version conflicts)
# Prioritize system OpenCV over /usr/local to avoid 4.05 conflict
set(CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu" ${CMAKE_PREFIX_PATH})
find_package(OpenCV 4.5 REQUIRED)
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include dirs: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
include_directories(${OpenCV_INCLUDE_DIRS})
link_libraries(${OpenCV_LIBS})

# Use pkg-config to find GTK and other dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK2 REQUIRED gtk+-2.0)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale)

#onnxruntime
set(ONNXRUNTIME_ROOT_PATH /usr/local)
set(ONNXRUNTIME_INCLUDE_DIRS ${ONNXRUNTIME_ROOT_PATH}/include/onnxruntime
                             ${ONNXRUNTIME_ROOT_PATH}/onnxruntime)
set(ONNXRUNTIME_LIB ${ONNXRUNTIME_ROOT_PATH}/lib/libonnxruntime.so)

# Add fmtlib directory
# add_subdirectory(3rdparty/fmt)
# Add fmtlib include to EXTRA_INCLUDES
# list(APPEND EXTRA_INCLUDES 3rdparty/fmt/include)

# Add mindvision-sdk directory and include path for target
add_library(MVSDK SHARED IMPORTED)
get_filename_component(MVSDK_PATH 3rdparty/mindvision/linux REALPATH)
# Check system architecture for mindvision-sdk library
execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE ARCHITECTURE)
message(STATUS "Checking platform architecture for MVSDK: ${ARCHITECTURE}")
if(${ARCHITECTURE} STREQUAL x86_64)
    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/x64/libMVSDK.so)
elseif(${ARCHITECTURE} STREQUAL x86)
    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/x86/libMVSDK.so)
elseif(${ARCHITECTURE} STREQUAL aarch64)
    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/arm64/libMVSDK.so)
elseif(${ARCHITECTURE} STREQUAL armv7)
    set(MIMDVISION_SDK_LIBRARY ${MVSDK_PATH}/lib/arm/libMVSDK.so)
else()
    message(FATAL_ERROR "Not supported architecture for MVSDK: ${ARCHITECTURE}")
endif()
set_property(TARGET MVSDK PROPERTY IMPORTED_LOCATION ${MIMDVISION_SDK_LIBRARY})

# --- Foxglove SDK 定义 ---
# 使用 INTERFACE 库而非 IMPORTED SHARED，以强制使用 -lfoxglove 链接方式
# 这样可以避免因缺少 SONAME 导致链接器嵌入相对/绝对路径的问题
# 同时配合 link_directories 让链接器找到库
add_library(foxglove_sdk INTERFACE)

target_include_directories(foxglove_sdk INTERFACE 
    "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/include"
)

# 添加库搜索路径 (供链接器使用)
link_directories("${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib")

# 链接库名 (这会生成 -lfoxglove)
target_link_libraries(foxglove_sdk INTERFACE foxglove)

# 显式将 Foxglove 库目录添加到构建时的 RPATH 中
# 这样运行程序时能自动找到库，无需设置 LD_LIBRARY_PATH
# CMAKE_INSTALL_RPATH_USE_LINK_PATH 如果为 TRUE 也会有帮助，但这里我们直接添加自定义路径
list(APPEND CMAKE_BUILD_RPATH "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib")
list(APPEND CMAKE_INSTALL_RPATH "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib")

# Add mindvision lib to build target
list(APPEND EXTRA_LIBS MVSDK)
# Add mindvision include path
list(APPEND EXTRA_INCLUDES 3rdparty/mindvision/linux/include)

# Add project source dir to include
list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR})

# Add base src and header
list(APPEND EXTRA_INCLUDES base)

# Add device as shared library to build target
list(APPEND EXTRA_INCLUDES devices)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/devices/camera)
add_library(mv-video-capture SHARED devices/camera/mv_video_capture.cpp)
target_link_libraries(mv-video-capture
    PRIVATE
    MVSDK
    fmt::fmt
)
list(APPEND MV_EXTRA_LIBS mv-video-capture)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/devices/serial)
add_library(mv-uart-serial SHARED devices/serial/uart_serial.cpp)
target_link_libraries(mv-uart-serial PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-uart-serial)

# Add module as shared library to build target
list(APPEND EXTRA_INCLUDES module)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/angle_solve)
add_library(mv-basic-pnp SHARED module/angle_solve/basic_pnp.cpp)
target_link_libraries(mv-basic-pnp PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-basic-pnp)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/angle_solve)
add_library(mv-angle-solve SHARED module/angle_solve/angle_solve.cpp)
target_link_libraries(mv-angle-solve PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-angle-solve)

list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/buff)
add_library(mv-new-buff SHARED module/buff/new_buff.cpp)
target_link_libraries(mv-new-buff PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-new-buff)


list(APPEND EXTRA_INCLUDES ${PROJECT_SOURCE_DIR}/module/armor)
add_library(mv-basic-armor SHARED module/armor/basic_armor.cpp)
target_link_libraries(mv-basic-armor PUBLIC fmt::fmt)
list(APPEND MV_EXTRA_LIBS mv-basic-armor)
add_library(mv-fan-armor SHARED module/armor/fan_armor.cpp)
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




