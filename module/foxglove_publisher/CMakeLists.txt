# 获取 Foxglove C++ 源文件
file(GLOB_RECURSE FOXGLOVE_CPP_SOURCES "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/src/*.cpp")

# 定义 foxglove_publisher 库
add_library(mv-foxglove-publisher SHARED
    foxglove_publisher.cpp
    ${FOXGLOVE_CPP_SOURCES}
    ${CMAKE_SOURCE_DIR}/utils/base64.cpp
)

# 包含路径
target_include_directories(mv-foxglove-publisher PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/module
    ${CMAKE_SOURCE_DIR}/devices
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/base
)

# 链接依赖
target_link_libraries(mv-foxglove-publisher
    PUBLIC
    foxglove_sdk
    ${OpenCV_LIBS}
    Eigen3::Eigen
    nlohmann_json::nlohmann_json
    fmt::fmt
    spdlog::spdlog
)
