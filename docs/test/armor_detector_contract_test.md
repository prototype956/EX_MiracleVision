# armor_detector_contract_test 使用说明

## 1. 测试目标

`mv-armor-detector-contract-test` 用于验证 detection 子域的接口契约，不用于评估算法精度。

覆盖点：

- `Detect(empty frame)` 返回空容器；
- detector 输出角点顺序为 `BL, BR, TR, TL`；
- `RoiManager::RestoreAndUpdate` 能正确恢复局部坐标到全图；
- ROI 连续丢失达到阈值后回退全图。

## 2. 构建命令

```bash
cmake -S . -B build -DUSE_MINDVISION_SDK=OFF
cmake --build build --target mv-armor-detector-contract-test -j$(nproc)
```

## 3. 运行命令

```bash
./build/src/test/mv-armor-detector-contract-test
```

预期输出包含：

- `[PASS] armor_detector_contract_test`

## 4. 常见问题

1. `MindVision SDK library not found`
- 原因：当前环境未提供相机 SDK 动态库。
- 处理：构建时添加 `-DUSE_MINDVISION_SDK=OFF`。

2. 合成图像未检测到装甲
- 原因：该测试依赖宽松阈值与合成样本形状。
- 处理：优先检查测试中参数覆写是否被改动，再检查形态学相关默认参数。

## 5. 维护约束

- 若修改角点顺序契约，必须同步更新：
  - `src/interfaces/types.hpp`
  - `src/modules/armor_detector/basic_armor_detector.cpp`
  - `src/modules/pnp_solver/pnp_solver.cpp`
  - 本测试文件
  - 本文档
