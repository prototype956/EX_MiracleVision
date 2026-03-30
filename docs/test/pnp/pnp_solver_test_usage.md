# PnP 单测使用说明（mv-pnp-solver-test）

## 1. 目的

该单测用于验证 `PnpSolver` 的最小可用行为，覆盖：
- 失败路径：未 `Init` 时 `Solve` 返回 `false`。
- 成功路径：完成 `Init` 后，给定合成角点可 `Solve` 成功。

对应代码：
- `src/test/pnp/pnp_solver_test.cpp`

## 2. 构建

在仓库根目录执行：

```bash
cmake --build build --target mv-pnp-solver-test
```

或执行全量构建：

```bash
cmake --build build
```

## 3. 运行

在仓库根目录执行：

```bash
./build/src/test/mv-pnp-solver-test
```

预期输出包含：
- 一条 `Solve() called before Init()` 日志（这是失败分支的预期行为）；
- 一条 `Init OK` 日志；
- 最终 `[PASS] pnp_solver_test`。

若进程退出码为 `0`，表示该最小单测通过。

## 4. 常用联动检查

建议在提交前至少执行以下命令：

```bash
# 1) 仅检查该测试文件静态问题
./scripts/check_code.sh --file src/test/pnp/pnp_solver_test.cpp

# 2) 检查该测试文件格式
clang-format --dry-run --Werror src/test/pnp/pnp_solver_test.cpp

# 3) 运行单测
./build/src/test/mv-pnp-solver-test
```

## 5. 失败排查

1. `Init` 失败：检查 `configs/vision.yaml` 中 `calibration.camera_matrix` 与 `distort_coeffs`。
2. `Solve` 成功分支失败：先确认角点顺序是否与 `BL/BR/TR/TL` 一致。
3. 二进制不存在：先执行构建命令，确认 `mv-pnp-solver-test` 已生成。
