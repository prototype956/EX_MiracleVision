# ============================================================================
# src/test/ — 测试可执行文件目录
# ============================================================================

message(STATUS "    + Configuring test targets...")

# ── Stage 1 冒烟测试（从 app/ 迁入）────────────────────────────────────────
add_executable(mv-smoke-test smoke_test.cpp)

target_link_libraries(mv-smoke-test
    PRIVATE
        mv-core
)

target_compile_definitions(mv-smoke-test PRIVATE
    CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
)

message(STATUS "      + mv-smoke-test (stage-1 smoke test)")

# ── Stage 6 离线视频检测链路测试 ─────────────────────────────────────────────
add_executable(mv-video-test video_pipeline_test.cpp)

target_link_libraries(mv-video-test
    PRIVATE
        mv-tool-debug
        mv-mod-pnp-solver
        mv-mod-simple-predictor
        mv-hal-camera
)

target_compile_definitions(mv-video-test PRIVATE
    CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
)

message(STATUS "      + mv-video-test (stage-6 offline video test)")

# ── Stage 7 Foxglove 模块端到端测试 ──────────────────────────────────────────
if(USE_FOXGLOVE_SDK)
    add_executable(mv-foxglove-vision-test foxglove_vision_test.cpp)

    target_link_libraries(mv-foxglove-vision-test
        PRIVATE
            mv-tool-foxglove
            mv-mod-armor-detector    # mv-tool-debug 未链入，须显式加
            mv-mod-pnp-solver
            mv-mod-simple-predictor
            mv-hal-camera
            foxglove_sdk   # mv-tool-foxglove 以 PRIVATE 链接 foxglove_sdk，
                           # 可执行文件需显式加入以解析 libfoxglove.so 符号
    )

    # link_directories 是目录作用域，不会通过 foxglove_sdk INTERFACE target 传播；
    # 此处显式告知链接器 libfoxglove.so 所在路径
    target_link_directories(mv-foxglove-vision-test
        PRIVATE
            ${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib
    )

    target_compile_definitions(mv-foxglove-vision-test PRIVATE
        CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
    )

    message(STATUS "      + mv-foxglove-vision-test (stage-7 foxglove e2e test)")
else()
    message(STATUS "      ⊗ mv-foxglove-vision-test skipped (USE_FOXGLOVE_SDK=OFF)")
endif()

# ── 预测 + 决策模块 Foxglove 调试测试 ─────────────────────────────────────────
if(USE_FOXGLOVE_SDK)
    add_executable(mv-predict-voter-test predict_voter_test.cpp)

    target_link_libraries(mv-predict-voter-test
        PRIVATE
            mv-tool-foxglove
            mv-tool-debug
            mv-mod-armor-detector
            mv-mod-pnp-solver
            mv-mod-ekf-predictor
            mv-mod-cooldown-voter
            mv-hal-camera
            foxglove_sdk
    )

    target_link_directories(mv-predict-voter-test
        PRIVATE
            ${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib
    )

    target_compile_definitions(mv-predict-voter-test PRIVATE
        CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
    )

    message(STATUS "      + mv-predict-voter-test (EKF predictor + CooldownVoter foxglove test)")
else()
    message(STATUS "      ⊗ mv-predict-voter-test skipped (USE_FOXGLOVE_SDK=OFF)")
endif()

add_executable(mv-minimum-real-vision minimum_real_vision.cpp)
target_link_libraries(mv-minimum-real-vision PRIVATE mv-core mv-hal-serial)
target_compile_definitions(mv-minimum-real-vision PRIVATE CONFIG_FILE_PATH="${CONFIG_FILE_PATH}")

# ── 实机联调程序（检测 + 预测 + 决策 + 串口下发）──────────────────────────────
if(USE_FOXGLOVE_SDK)
    add_executable(mv-real-debug-vision real_debug_vision.cpp)

    target_link_libraries(mv-real-debug-vision
        PRIVATE
            mv-core
            mv-tool-foxglove
            mv-tool-debug
            mv-mod-armor-detector
            mv-mod-pnp-solver
            mv-mod-ekf-predictor
            mv-mod-cooldown-voter
            mv-mod-rm-shooter
            mv-hal-camera
            mv-hal-serial
            foxglove_sdk
    )

    target_link_directories(mv-real-debug-vision
        PRIVATE
            ${CMAKE_SOURCE_DIR}/3rdparty/foxglove/lib
    )

    target_compile_definitions(mv-real-debug-vision PRIVATE
        CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
    )

    message(STATUS "      + mv-real-debug-vision (实机联调：检测+预测+决策+串口+Foxglove)")
else()
    message(STATUS "      ⊗ mv-real-debug-vision skipped (USE_FOXGLOVE_SDK=OFF)")
endif()
