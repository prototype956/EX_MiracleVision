# ============================================================================
# 编译器选项配置
# ============================================================================

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译器标志
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wno-deprecated)
endif()

# 构建类型特定标志
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# 编译宏定义
add_compile_definitions(
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:RELEASE>
    SPDLOG_FMT_EXTERNAL  # 解决 spdlog 和 fmt 冲突
)

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
