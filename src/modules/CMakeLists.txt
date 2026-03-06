# ============================================================================
# mv-modules — 具体算法模块（Stage 6）
# ============================================================================
#
# 每个子目录一个静态库，按需链接到主可执行文件。
#
# 注意：工厂注册通过 .cpp 文件中的匿名命名空间静态变量完成，
#   链接时必须使用 --whole-archive 或等效方式确保
#   "未被直接引用的翻译单元"不被链接器丢弃。
#   main.cpp 直接包含具体头文件以绕过该问题（最简做法）。

message(STATUS "  Configuring mv-modules...")

# ── 装甲板检测器 ────────────────────────────────────────────────────────────
add_library(mv-mod-armor-detector STATIC
    armor_detector/basic_armor_detector.cpp
)
target_include_directories(mv-mod-armor-detector
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(mv-mod-armor-detector
    PUBLIC
        mv-interfaces
        mv-core
        ${OpenCV_LIBS}
)
message(STATUS "    [modules] mv-mod-armor-detector (BasicArmorDetector)")

# ── PnP 位姿解算器 ──────────────────────────────────────────────────────────
add_library(mv-mod-pnp-solver STATIC
    pnp_solver/pnp_solver.cpp
)
target_include_directories(mv-mod-pnp-solver
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(mv-mod-pnp-solver
    PUBLIC
        mv-interfaces
        mv-core
        ${OpenCV_LIBS}
        Eigen3::Eigen
)
message(STATUS "    [modules] mv-mod-pnp-solver (PnpSolver)")

# ── 简单预测器 ───────────────────────────────────────────────────────────────
add_library(mv-mod-simple-predictor STATIC
    simple_predictor/simple_predictor.cpp
)
target_include_directories(mv-mod-simple-predictor
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(mv-mod-simple-predictor
    PUBLIC
        mv-interfaces
        mv-core
        Eigen3::Eigen
)
message(STATUS "    [modules] mv-mod-simple-predictor (SimplePredictor)")

# ── 简单开火投票器 ──────────────────────────────────────────────────────────
add_library(mv-mod-simple-voter STATIC
    simple_voter/simple_voter.cpp
)
target_include_directories(mv-mod-simple-voter
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(mv-mod-simple-voter
    PUBLIC
        mv-interfaces
        mv-core
)
message(STATUS "    [modules] mv-mod-simple-voter (SimpleVoter)")

# ── RM 协议发射器 ────────────────────────────────────────────────────────────
add_library(mv-mod-rm-shooter STATIC
    rm_shooter/rm_shooter.cpp
)
target_include_directories(mv-mod-rm-shooter
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(mv-mod-rm-shooter
    PUBLIC
        mv-interfaces
        mv-hal-serial
        mv-core
)
message(STATUS "    [modules] mv-mod-rm-shooter (RmShooter)")
