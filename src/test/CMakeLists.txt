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
