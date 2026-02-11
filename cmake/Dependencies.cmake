# ============================================================================
# 依赖查找配置 - 通过 vcpkg 管理的依赖
# ============================================================================

message(STATUS "Finding dependencies via vcpkg...")

# ----------------------------------------------------------------------------
# 线程库
# ----------------------------------------------------------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
message(STATUS "  ✓ Threads found")

# ----------------------------------------------------------------------------
# vcpkg 管理的依赖
# ----------------------------------------------------------------------------

# fmt - 格式化库
find_package(fmt CONFIG REQUIRED)
message(STATUS "  ✓ fmt found: ${fmt_VERSION}")

# spdlog - 日志库
find_package(spdlog CONFIG REQUIRED)
message(STATUS "  ✓ spdlog found: ${spdlog_VERSION}")

# yaml-cpp - YAML 解析
find_package(yaml-cpp CONFIG REQUIRED)
message(STATUS "  ✓ yaml-cpp found")

# nlohmann_json - JSON 解析
find_package(nlohmann_json CONFIG REQUIRED)
message(STATUS "  ✓ nlohmann_json found: ${nlohmann_json_VERSION}")

# Eigen3 - 线性代数
find_package(Eigen3 CONFIG REQUIRED)
message(STATUS "  ✓ Eigen3 found: ${Eigen3_VERSION}")

# TBB - 并行计算
find_package(TBB CONFIG REQUIRED)
message(STATUS "  ✓ TBB found: ${TBB_VERSION}")

# OpenCV - 计算机视觉
find_package(OpenCV CONFIG REQUIRED)
message(STATUS "  ✓ OpenCV found: ${OpenCV_VERSION}")
message(STATUS "    OpenCV modules: ${OpenCV_LIBS}")

# ----------------------------------------------------------------------------
# 检查关键 OpenCV 模块
# ----------------------------------------------------------------------------
set(REQUIRED_OPENCV_COMPONENTS 
    core imgproc imgcodecs highgui videoio calib3d dnn
)

foreach(component ${REQUIRED_OPENCV_COMPONENTS})
    if(NOT TARGET opencv_${component})
        message(WARNING "OpenCV component '${component}' not found!")
    endif()
endforeach()

message(STATUS "All vcpkg dependencies found successfully!")
