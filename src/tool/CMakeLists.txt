# ============================================================================
# src/tool/ — 调试工具库 mv-tool-debug
# ============================================================================
# 仅供 test 程序使用，不参与 Release 主程序构建。
# Pimpl 隔离了 OpenCV / yaml-cpp，上游目标 include 此库头文件时
# 不引入这两个大型依赖的编译代价。

add_library(mv-tool-debug STATIC
    param_tuner.cpp
    view_renderer.cpp
    debug_session.cpp
)

target_include_directories(mv-tool-debug
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src   # 使 #include "tool/..." 生效
)

target_link_libraries(mv-tool-debug
    PUBLIC
        mv-core
        mv-mod-armor-detector    # DebugData / Params 类型来源
        ${OpenCV_LIBS}
    PRIVATE
        yaml-cpp
)

message(STATUS "      + mv-tool-debug (debug session / param tuner / view renderer)")
