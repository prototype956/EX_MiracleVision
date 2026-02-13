# ============================================
# Test 目录 CMake 配置
# ============================================

message(STATUS "配置 test 目录...")

# ============================================
# 1. Foxglove Camera SDK 测试程序
# ============================================

# # 手动指定 Foxglove SDK 路径
# set(FOXGLOVE_SDK_PATH "/home/prototype152/桌面/EX_MiracleVision/3rdparty/foxglove")

# # 检查 Foxglove SDK 是否存在
# if(EXISTS "${FOXGLOVE_SDK_PATH}/lib/libfoxglove.so")
#     message(STATUS "  ✅ 找到 Foxglove SDK: ${FOXGLOVE_SDK_PATH}")
    
#     # 查找必要的依赖
#     find_package(Boost REQUIRED COMPONENTS system)
    
#     # 检查 foxglove_camera_sdk.cpp 是否存在
#     if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/foxglove_camera_sdk.cpp")
#         message(STATUS "  ✅ 配置 foxglove_camera_sdk")
        
#         # add_executable(foxglove_camera_sdk
#         #     foxglove_camera_sdk.cpp
#         # )
        
#         target_include_directories(foxglove_camera_sdk PRIVATE
#             ${FOXGLOVE_SDK_PATH}/include
#             ${OpenCV_INCLUDE_DIRS}
#             ${Boost_INCLUDE_DIRS}
#         )
        
#         target_link_libraries(foxglove_camera_sdk PRIVATE
#             ${OpenCV_LIBS}
#             ${FOXGLOVE_SDK_PATH}/lib/libfoxglove.so
#             Boost::system
#             nlohmann_json::nlohmann_json
#             pthread
#             z  # zlib for compression
#         )
        
#         target_compile_definitions(foxglove_camera_sdk PRIVATE
#             SOURCE_PATH="${CMAKE_SOURCE_DIR}"
#             CONFIG_FILE_PATH="${CMAKE_SOURCE_DIR}/configs"
#         )
        
#         set_target_properties(foxglove_camera_sdk PROPERTIES
#             RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
#             CXX_STANDARD 17
#             CXX_STANDARD_REQUIRED ON
#         )
        
#         message(STATUS "  ✅ foxglove_camera_sdk 目标已配置")
#     else()
#         message(STATUS "  ⚠️  未找到 foxglove_camera_sdk.cpp")
#     endif()
        
# else()
#     message(STATUS "  ⚠️  未找到 Foxglove SDK: ${FOXGLOVE_SDK_PATH}")
#     message(STATUS "     请确认 SDK 已正确安装")
# endif()

# ============================================================================
# Test 测试程序目录
# ============================================================================

message(STATUS "  Configuring test programs...")

# ============================================
# Minimum Vision 测试程序
# ============================================

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/minimum_vision.cpp")
    message(STATUS "    Configuring minimum_vision...")
    
    add_executable(minimum_vision
        minimum_vision.cpp
    )
    
    target_include_directories(minimum_vision PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/base
        ${CMAKE_SOURCE_DIR}/module
        ${CMAKE_SOURCE_DIR}/devices
        ${CMAKE_SOURCE_DIR}/utils
    )
    
    target_link_libraries(minimum_vision PRIVATE
        # 工具库
        mv-img-tools
        mv-plotter
        
        # 算法模块
        mv-basic-armor
        mv-predictor
        
        # 设备层
        mv-uart-serial
        
        # vcpkg 依赖
        fmt::fmt
        nlohmann_json::nlohmann_json
        
        # OpenCV
        opencv_core
        opencv_imgproc
        opencv_highgui
        opencv_videoio
        opencv_calib3d
        
        # 文件系统库
        stdc++fs
    )
    
    # 如果启用了 Foxglove
    if(TARGET mv-foxglove-publisher)
        set(FOXGLOVE_LIB_PATH "${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib/libfoxglove.so")
        if(EXISTS ${FOXGLOVE_LIB_PATH})
            target_link_libraries(minimum_vision PRIVATE 
                mv-foxglove-publisher
                ${FOXGLOVE_LIB_PATH}
            )
            message(STATUS "      ! Linked Foxglove SDK to minimum_vision")
        else()
            target_link_libraries(minimum_vision PRIVATE mv-foxglove-publisher)
        endif()
    endif()
    
    target_compile_definitions(minimum_vision PRIVATE
        SOURCE_PATH="${CMAKE_SOURCE_DIR}"
        CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
    )
    
    set_target_properties(minimum_vision PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    
    # GCC 11 在 Release 模式下编译此文件时会出现 ICE，需要降低优化级别
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(minimum_vision PRIVATE -O1)
        message(STATUS "      ! Applied -O1 optimization for minimum_vision (GCC ICE workaround)")
    endif()
    
    message(STATUS "      ✓ minimum_vision configured")
else()
    message(STATUS "      ⊗ minimum_vision.cpp not found")
endif()

# ============================================
# 5. 其他测试程序（可扩展）
# ============================================

# 自动发现所有 *_test.cpp 文件（可选）
file(GLOB TEST_FILES "*_test.cpp")
foreach(TEST_FILE ${TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    
    # 排除已经手动配置的测试
    if(NOT TEST_NAME STREQUAL "demo_test" 
       AND NOT TEST_NAME STREQUAL "video_demo"
       AND NOT TEST_NAME STREQUAL "minimum_vision")
        
        message(STATUS "  ℹ️  发现测试文件: ${TEST_NAME}")
        
        # 可以在这里自动生成测试目标
        # add_executable(${TEST_NAME} ${TEST_FILE})
        # ...
    endif()
endforeach()

message(STATUS "test 目录配置完成")
