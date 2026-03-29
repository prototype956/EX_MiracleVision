# Doxygen 注释债务基线（首轮）

## 1. 目的
- 记录 Doxygen 注释规范落地初期的债务现状。
- 作为后续“触达即治理”和门禁收紧的对比基线。

## 2. 采集方式
- 脚本：`scripts/check_code.sh`
- 模式：`--doxygen-only`（warning-only，不阻断）
- 时间：2026-03-29

## 3. 首轮样本结果

### 3.1 src/interfaces
- 命令：`bash scripts/check_code.sh --doxygen-only --dir src/interfaces`
- 结果：注释告警数 = 1
- 主要问题：
  - `@thread_safety` 缺失（示例：`src/interfaces/i_predictor.hpp`）

### 3.2 src/modules/pnp_solver
- 命令：`bash scripts/check_code.sh --doxygen-only --dir src/modules/pnp_solver`
- 结果：注释告警数 = 22
- 主要问题：
  - 函数缺少 Doxygen 注释块
  - 缺少 `@brief/@param/@return/@thread_safety`
  - 头文件中个别接口注释未补齐线程安全标签

## 4. 问题类型分桶
- A 类：缺少函数 Doxygen 注释块
- B 类：缺少 `@brief`
- C 类：参数存在但缺少 `@param`
- D 类：非 `void` 返回值缺少 `@return`
- E 类：缺少 `@thread_safety`
- F 类：数学密集函数缺少 LaTeX 公式（建议项）

## 5. 治理优先级
1. `src/interfaces`：先清零 `@thread_safety` 等契约注释缺失。
2. `src/modules/pnp_solver`：按“单次一个函数”节奏补齐核心函数注释。
3. 其余 `src/modules`：按触达路径增量治理。

## 6. 下一步执行建议
- 在不改变行为的前提下，优先补接口层契约注释。
- 对 `pnp_solver` 先从对外方法开始补注释：`Init()`、`Solve()`。
- 每次补注释后重复执行：
  - `bash scripts/check_code.sh --doxygen-only --dir <target>`
  - 记录告警数变化趋势。
