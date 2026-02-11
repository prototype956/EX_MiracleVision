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

# 如果启用了 MindVision SDK，则链接
if(TARGET MVSDK)
    target_link_libraries(mv-video-capture PUBLIC MVSDK)
    target_compile_definitions(mv-video-capture PUBLIC HAS_MVSDK)
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

target_link_libraries(mv-uart-serial 
    PUBLIC 
    fmt::fmt
    opencv_core
)

target_include_directories(mv-uart-serial PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/serial>
    $<INSTALL_INTERFACE:include/devices>
)

message(STATUS "    ✓ Device libraries configured")
