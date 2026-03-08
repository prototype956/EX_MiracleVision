# ============================================================================
# src/tool/foxglove/ — Foxglove 可视化调试工具库 mv-tool-foxglove
# ============================================================================
# 仅供调试 / 测试程序使用，Release 主程序不强制链接。
# PImpl 模式：Foxglove SDK 头文件完全隔离在 .cpp 内，
# 上游目标 include foxglove_sink.hpp 时不引入 foxglove SDK 编译代价。

add_library(mv-tool-foxglove STATIC
    foxglove_sink.cpp
    detail/image_publisher.cpp
    detail/detection_publisher.cpp
    detail/pnp_visualizer.cpp
    detail/tf_publisher.cpp
    detail/thread_monitor.cpp
)

target_include_directories(mv-tool-foxglove
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src   # 使 #include "tool/foxglove/..." 生效
)

target_link_libraries(mv-tool-foxglove
    PUBLIC
        mv-interfaces             # mv::Detection / GimbalControl
        ${OpenCV_LIBS}
        Eigen3::Eigen
        nlohmann_json::nlohmann_json
    PRIVATE
        foxglove_sdk
        spdlog::spdlog
)

message(STATUS "      + mv-tool-foxglove (foxglove sink / image / detection / pnp / tf / thread monitor)")
