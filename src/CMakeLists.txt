# ============================================================================
# src/ — 重构后的核心模块目录
# ============================================================================
# 各阶段子目录按需添加，未完成的阶段注释掉

message(STATUS "  Configuring refactored src modules...")

# 阶段一：基础设施
add_subdirectory(core)

# 冒烟测试可执行文件
add_subdirectory(app)

# 阶段二：硬件抽象层（HAL）
add_subdirectory(hal)

# 阶段三：接口层 + 工厂系统 —— 待实现
# add_subdirectory(interfaces)
# add_subdirectory(factory)
# add_subdirectory(modules)

# 阶段四：线程 Pipeline —— 待实现
# add_subdirectory(pipeline)

# 阶段五：状态机 —— 待实现
# add_subdirectory(fsm)
