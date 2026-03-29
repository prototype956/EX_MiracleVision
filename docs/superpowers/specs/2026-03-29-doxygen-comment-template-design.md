# RM视觉系统 Doxygen 注释模板设计

## 1. 目标与范围
- 目标：为 EX_MiracleVision 建立统一、可执行的 Doxygen 注释模板与规范化流程。
- 范围：默认仅对 `src/` 强制执行；`base/`、`module/`、`devices/` 作为参考，不做强制改造。
- 约束：与现有架构边界一致，先告警不阻断，后续按数据趋势收紧。

## 2. 设计原则
- 可读性：注释解释 why、输入输出约束与单位边界。
- 可审查：模板字段固定，评审时可逐项核对。
- 可落地：本地脚本 + CI 告警闭环，避免一次性大改。
- 可演进：保留从 warning 升级到 blocking 的门禁开关。

## 3. 注释模板体系

### 3.1 文件模板（最小）
```cpp
/**
 * @file demo.hpp
 * @brief 一句话说明文件职责
 */
```

### 3.2 类模板（最小）
```cpp
/**
 * @brief 类职责说明
 */
class Demo {};
```

### 3.3 函数模板（最小）
```cpp
/**
 * @brief 函数职责说明
 * @param input 输入含义与单位
 * @param options 可选参数与边界约束
 * @return 返回值语义
 * @thread_safety Not thread-safe
 */
bool Run(const Input& input, const Options& options);
```

### 3.4 数学函数模板（强制完整）
```cpp
/**
 * @brief 计算目标在相机坐标系下的位置
 *
 * 【问题定义】
 *  给定像素观测与内参，估计目标三维位姿。
 *
 * 【符号定义】
 *  - \f$\mathbf{K}\f$：相机内参矩阵（单位：px）
 *  - \f$\mathbf{R}, \mathbf{t}\f$：旋转与平移（单位：m）
 *  - \f$\mathbf{X}_w\f$：世界坐标点（单位：m）
 *  - \f$\mathbf{x}\f$：像素点（单位：px）
 *
 * 【核心公式】
 *  \f[
 *    s\mathbf{x} = \mathbf{K}(\mathbf{R}\mathbf{X}_w + \mathbf{t})
 *  \f]
 *
 * @param detection 输入装甲板观测，角点单位为 px
 * @param camera_to_gimbal 可选外参，单位 m
 * @return true 求解成功并写入位姿结果；false 表示输入或求解失败
 * @note 失败场景包括：角点数量异常、矩阵不可逆、重投影误差越界
 * @thread_safety Not thread-safe
 */
bool Solve(Detection& detection, const std::optional<Transform>& camera_to_gimbal);
```

## 4. 数学公式规范
- 统一使用 LaTeX 风格。
- 行内公式：`\f$ ... \f$`。
- 块级公式：`\f[ ... \f]`。
- 数学函数注释必须包含：问题定义、符号定义、完整公式链、单位一致性、适用边界与失败条件。

## 5. 现有注释规范化策略

### 5.1 扫描阶段
- 先做 `src/` 注释债务盘点，类型包括：
  - 缺 `@file/@brief`
  - 缺 `@param/@return`
  - 缺 `@thread_safety`
  - 数学函数缺公式块或符号定义

### 5.2 增量修复阶段
- 优先级：`src/interfaces` -> 数学密集模块（pnp/predictor/filter/ballistics）-> 其余 `src`。
- 策略：触达即治理，修改到的函数必须补齐模板字段。

### 5.3 门禁阶段
- CI 先以 warning 展示注释缺失摘要，不阻断合并。
- 迭代复盘告警趋势后，再评估升级为核心目录阻断。

## 6. Doxygen 生成链路
- 推荐配置文件：`docs/doxygen/Doxyfile`。
- 推荐输入目录：`src/` 与 `docs/superpowers/specs/`。
- 推荐输出目录：`build/docs/doxygen/`。
- 生成命令：`doxygen docs/doxygen/Doxyfile`。

## 7. 验收标准
- 新增数学函数具备完整公式注释。
- Doxygen 文档可生成且公式正常渲染。
- CI 输出注释检查摘要且不阻断。
- 文档同步映射覆盖：modules/cmake/clangd/CI/superpowers/specs。
