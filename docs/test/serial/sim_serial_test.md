# SimSerial 测试说明

## 测试目标

验证 SimSerial 的关键行为：

1. 正常路径：可连接本地 TCP 服务器并完成 Send/Recv。
2. 边界路径：服务端断开后，Send 软降级返回 true，不触发上层串口失败累计。

## 测试文件

- src/test/serial/sim_serial_test.cpp

## 构建目标

- mv-sim-serial-test

## 运行方式

在 EX_MiracleVision 根目录执行：

```bash
cmake -S . -B build
cmake --build build -j$(nproc) --target mv-sim-serial-test
./build/src/test/mv-sim-serial-test
```

## 通过判定

输出包含：

- [PASS] sim_serial_test

## 失败定位建议

- 若 Open 失败：优先检查本地端口占用与 endpoint 配置。
- 若 Send/Recv 失败：检查测试内置 TCP 服务器线程是否正常启动。
- 若断连软降级断言失败：检查 SimSerial::Send 在断连分支是否返回 true。
