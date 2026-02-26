# ============================================================================
# src/app/ — 应用入口目录
# ============================================================================

# ── 阶段一冒烟测试可执行文件 ────────────────────────────────────────────────
add_executable(mv-smoke-test smoke_test.cpp)

target_link_libraries(mv-smoke-test
    PRIVATE
        mv-core
)

target_compile_definitions(mv-smoke-test PRIVATE
    CONFIG_FILE_PATH="${CONFIG_FILE_PATH}"
)

message(STATUS "    + mv-smoke-test (stage-1 smoke test)")
