# ============================================================================
# Devices 设备层库
# ============================================================================

message(STATUS "  Configuring device libraries...")

# ----------------------------------------------------------------------------
# mv-video-capture - 相机视频捕获
# ----------------------------------------------------------------------------
add_library(mv-video-capture SHARED camera/mv_video_capture.cpp)

target_link_libraries(mv-video-capture
    PUBLIC
    opencv_core
    opencv_imgproc
    fmt::fmt
)

# 总是链接 MVSDK (如果库文件存在)
set(MVSDK_SO_PATH "${CMAKE_SOURCE_DIR}/3rdparty/mindvision/linux/lib/x64/libMVSDK.so")
if(EXISTS ${MVSDK_SO_PATH})
    target_link_libraries(mv-video-capture PUBLIC ${MVSDK_SO_PATH})
    target_compile_definitions(mv-video-capture PUBLIC HAS_MVSDK)
    message(STATUS "    ! Linked MVSDK to mv-video-capture")
else()
    message(WARNING "MVSDK library not found: ${MVSDK_SO_PATH}")
endif()

target_include_directories(mv-video-capture PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/camera>
    $<INSTALL_INTERFACE:include/devices>
)

# ----------------------------------------------------------------------------
# mv-uart-serial - 串口通信
# ----------------------------------------------------------------------------
add_library(mv-uart-serial SHARED serial/uart_serial.cpp)

# GCC 11 在 Release 模式编译 uart_serial 时会 ICE,降低优化等级
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mv-uart-serial PRIVATE -O1)
endif()

target_link_libraries(mv-uart-serial 
    PUBLIC 
    fmt::fmt
    opencv_core
)

target_include_directories(mv-uart-serial PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/serial>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/devices>
)

message(STATUS "    ✓ Device libraries configured")
