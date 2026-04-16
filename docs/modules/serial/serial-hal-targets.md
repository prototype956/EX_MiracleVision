# 串口 HAL 与测试目标更新

## 变更概述

本次对串口 HAL 构建与测试目标做了增量扩展：

- mv-hal-serial 增加 sim_serial.cpp 编译单元。
- 新增测试可执行目标 mv-sim-serial-test。

## 代码映射

1. HAL 目标
- 代码：src/hal/serial/CMakeLists.txt
- 变更：add_library(mv-hal-serial ...) 增加 sim_serial.cpp

2. 测试目标
- 代码：src/test/CMakeLists.txt
- 变更：新增 add_executable(mv-sim-serial-test serial/sim_serial_test.cpp)

## 影响评估

- 仅影响 src/hal/serial 与 src/test 编译图。
- 不改变既有 UartSerial 目标与链接方式。
- 现有目标（含 MiracleVision、mv-vision-main）保持兼容。

## 建议验证

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
cmake --build build -j$(nproc) --target mv-sim-serial-test
```
