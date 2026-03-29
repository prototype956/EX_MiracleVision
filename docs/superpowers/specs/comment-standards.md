# 注释瘦身标准（不降信息）

## 1. 目标
- 在不降低接口可用信息的前提下，减少注释体积与重复内容。
- 适用于 `src/` 下新增与修改的 C++ 注释；旧目录按触达治理。

## 2. 核心原则
- 契约信息留在代码：输入输出、单位、失败条件、线程安全。
- 推导与背景外置：完整推导、历史对比、长篇背景写入 `docs/superpowers/specs/`。
- 单点约定复用：坐标系、角点顺序、单位约定只写一次，其余位置引用。
- 避免复述实现：注释解释 why 与边界，不逐行解释 how。

## 3. 长度建议（warning 指导）
- 文件头：建议 5~8 行。
- 类注释：建议 3~5 行。
- 普通函数：建议 5~8 行。
- 数学函数：建议 10~14 行（不含公式块）。
- `@brief`：建议单行，超过 2 行视为密度告警。

## 4. 模板（精简版）

### 4.1 文件模板
```cpp
/**
 * @file demo.hpp
 * @brief 一句话说明文件职责
 * 输入：核心输入；输出：核心输出。
 * 详见 docs/superpowers/specs/<topic>.md
 */
```

### 4.2 类模板
```cpp
/**
 * @brief 类职责说明
 * 实现接口与关键约束说明。
 * @thread_safety Not thread-safe
 */
class Demo final {};
```

### 4.3 普通函数模板
```cpp
/**
 * @brief 函数职责一句话
 * @param input 输入含义与单位
 * @param options 可选参数与边界
 * @return 返回值语义（成功/失败）
 * @note 仅保留关键边界，细节见 specs 文档
 * @thread_safety Not thread-safe
 */
bool Run(const Input& input, const Options& options);
```

### 4.4 数学函数模板
```cpp
/**
 * @brief 求解目标姿态（云台系）
 *
 * 问题定义：给定观测与内参，估计位姿与误差。
 * 核心公式：\f$ s\mathbf{x} = \mathbf{K}(\mathbf{R}\mathbf{X}_w + \mathbf{t}) \f$
 * 符号：\f$\mathbf{K}\f$ 内参(px), \f$\mathbf{X}_w\f$ 世界坐标(m)。
 * 详见 docs/superpowers/specs/<math-topic>.md
 *
 * @param detection 输入角点（px）与输出位姿（m/rad）
 * @return true 成功；false 失败（奇异/越界）
 * @note 失败条件：角点异常、矩阵不可逆、误差越界
 * @thread_safety Not thread-safe
 */
bool Solve(Detection& detection);
```

## 5. 冗长版 vs 精简版（示例）
- 冗长版：同一段坐标系、单位、历史差异在文件头、类注释、函数注释重复出现。
- 精简版：文件头保留约定入口，类/函数仅保留契约信息并引用该入口。

## 6. 评审检查点
- 是否能仅凭注释回答：做什么、输入约束、失败条件、线程安全。
- 是否存在重复段落可上提到文件头或 specs。
- 是否缺少单位（m/rad/px）或边界条件。

## 7. 与门禁关系
- 本标准与 Doxygen 最小模板并行：最小模板是“合规下限”，本标准是“信息密度上限”。
- 当前执行策略为 warning-only，不阻断合并。
