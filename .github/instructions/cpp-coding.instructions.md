---
description: EX_MiracleVision C++ 编码、接口契约与 Doxygen 注释规范。
applyTo: "**/*.{cpp,hpp,h,cc,cxx}"
---

# C++ Coding Rules

## 适用范围
- 适用于 C++ 源码与头文件。

## 必须规则
- 代码风格遵循项目既有约定：2 空格缩进、行宽 100、Google C++ 风格。
- 新代码统一使用 namespace mv，禁止引入旧命名空间。
- 3D 坐标使用米（m），角度使用弧度（rad），单位换算必须注释说明。
- 接口方法失败优先使用返回值表达：
	- 预期失败（如输入无效、无目标）返回 false 或空容器。
	- 异常失败（如系统级资源错误）可抛异常。
- 新增或修改的接口与关键函数必须使用 Doxygen 模板。
- 枚举统一使用 enum class，禁止裸 enum。
- 涉及模块行为或接口变更时，必须按 commit-pr.instructions.md 同步更新对应文档。

## 接口契约补充
- Init() 失败返回 false，不抛异常。
- 无目标场景返回空容器（{}），调用方无需 try-catch。
- 接口默认非线程安全，调用方负责线程隔离。

## Doxygen 最小模板
- 文件头模板：
```cpp
/**
 * @file demo.hpp
 * @brief 一句话说明文件职责
 */
```
- 类模板：
```cpp
/**
 * @brief 类职责说明
 */
class Demo {};
```
- 函数模板：
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

- 复杂参数模板：
```cpp
/**
 * @brief 复杂参数场景说明
 * @param detectors 检测器列表，顺序代表优先级
 * @param config.max_targets 最大目标数，范围 [1, 10]
 * @pre config.max_targets >= 1
 * @return 检测结果；空结果表示无有效目标
 * @thread_safety Not thread-safe
 */
Result Detect(const std::vector<IDetector*>& detectors, const Config& config);
```

## 数学函数注释规范（RM 视觉专项）
- 涉及几何、PnP、滤波、弹道、坐标变换等数学密集函数，必须提供完整公式说明。
- 公式统一使用 LaTeX 风格：
	- 行内公式：`\f$ ... \f$`
	- 块级公式：`\f[ ... \f]`
- 数学函数注释至少包含：
	- 问题定义（函数在求解什么）；
	- 符号定义（变量含义、单位、坐标系）；
	- 完整公式链（核心方程与必要中间量）；
	- 适用边界与失败条件（例如奇异情况、输入越界、数值稳定性约束）。
- 单位与约束为必填项，禁止省略单位或混用单位。

## 注释瘦身规则（不降信息）
- 注释必须优先保留接口契约信息：输入输出、单位、失败条件、线程安全。
- 完整推导、历史对比、长背景说明应迁移到 `docs/superpowers/specs/` 并在代码中引用。
- 同一模块的坐标系、角点顺序、单位约定应单点定义，禁止在多个函数重复展开。
- `@brief` 建议单行表达核心职责；超过 2 行视为需要精简的高风险注释。
- 数学函数代码内保留“问题定义 + 核心公式 + 符号/单位 + 边界”，其余细节外链。

## 推荐规则
- 注释优先解释 why、输入输出约束与单位边界。
- 文件头包含职责边界，减少跨模块误用。

## 禁止事项
- 禁止注释仅复述代码表面行为。
- 禁止无说明地混用单位。

## 例外流程
- 若因第三方限制无法满足某项规范，需在变更说明中声明限制原因与后续改进方案。

## 验收清单
- 命名、单位、异常策略与注释模板均符合规范。
- Doxygen 注释可用于后续 API 文档生成。
- 接口类、跨模块公共函数、关键算法函数均提供了最小 Doxygen 注释。

## C++ 编码检查清单（执行项）
- [ ] 新增代码使用 namespace mv，未引入旧命名空间。
- [ ] 新增枚举使用 enum class，未使用裸 enum。
- [ ] 单位换算点已注明单位与换算依据。
- [ ] 接口与关键函数包含 Doxygen 注释（含 @brief/@param/@return）。
- [ ] 接口线程安全语义已在注释中显式说明（如 @thread_safety）。
- [ ] 若涉及模块行为或接口变更，已按 commit-pr.instructions.md 的目标映射同步文档。
- [ ] 数学密集函数已使用 LaTeX 公式并补齐符号、单位与边界约束说明。
