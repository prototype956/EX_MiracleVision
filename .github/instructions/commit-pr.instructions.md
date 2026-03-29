---
description: EX_MiracleVision 提交信息、PR 说明与评审门禁规范。
applyTo: "**"
---

# Commit And PR Rules

## 适用范围
- 适用于提交、PR、评审与合并阶段。

## 必须规则
- 提交信息遵循 Conventional Commits 格式。
- PR 说明必须包含：变更目的、影响范围、验证方式、风险与回退策略。
- 若涉及行为变化，必须附测试证据与对应文档更新说明。
- 相关代码改动必须同步更新对应文档，否则 PR 不通过。

## 文档同步判定标准
- 需要同步文档的改动类型：
	- 新增或修改模块接口、行为、配置项、输入输出约束。
	- CMake 构建目标、依赖关系、编译选项变更。
	- clangd/clang-tidy/clang-format 使用口径或脚本流程变更。
	- CI 阶段、门禁条件、流水线步骤变更。
	- 注释模板、注释密度规则、Doxygen 注释治理流程变更。
- 文档目标映射：
	- 模块相关改动 -> docs/modules/<module-name>.md。
	- CMake 相关改动 -> docs/cmake/ 下对应文档。
	- clang 工具链相关改动 -> docs/clangd/ 下对应文档。
	- CI 相关改动 -> docs/CI/ 下对应文档。
	- 设计规范/方案流程改动 -> docs/superpowers/specs/<topic>.md。
- 审查通过条件：
	- PR 描述中明确列出“代码改动 -> 文档改动”的对应关系。
	- 文档内容反映本次行为变化，而不是仅修改时间戳或空白。

## 文档分区目录要求
- 模块文档目录：docs/modules/。
- CMake 文档目录：docs/cmake/。
- clang 工具链文档目录：docs/clangd/。
- CI 文档目录：docs/CI/。
- 设计规范目录：docs/superpowers/specs/。
- 上述目录缺失时，必须在本次变更中补齐对应目录与文档。

## PR 映射模板（可直接复制）
```markdown
### 代码改动 -> 文档改动映射

1. 模块改动
- 代码：src/modules/<module-name>/...
- 文档：docs/modules/<module-name>.md
- 变更点：<行为/接口/配置变更摘要>

2. CMake 改动
- 代码：src/modules/CMakeLists.txt 或 cmake/...
- 文档：docs/cmake/<topic>.md
- 变更点：<目标/依赖/编译选项变更摘要>

3. clang 工具链改动
- 代码：.clangd / .clang-tidy / scripts/check_code.sh / scripts/format_code.sh
- 文档：docs/clangd/<topic>.md
- 变更点：<诊断/修复/检查口径变更摘要>

4. CI 改动
- 代码：.github/workflows/... 或 CI 相关脚本
- 文档：docs/CI/<topic>.md
- 变更点：<阶段/门禁/流水线变更摘要>

5. 设计规范改动
- 代码：.github/instructions/... 或 docs 流程规范文件
- 文档：docs/superpowers/specs/<topic>.md
- 变更点：<模板/流程/设计决策变更摘要>

6. 组合改动示例（模块 + CMake + CI）
- 代码：src/modules/armor_detector/..., src/modules/CMakeLists.txt, .github/workflows/ci.yml
- 文档：docs/modules/armor_detector.md, docs/cmake/module-targets.md, docs/CI/pipeline-gates.md
- 变更点：<新增模块并接入构建，同时更新CI门禁>
```

## 推荐规则
- 小步提交，保持单次提交主题聚焦。
- 在 PR 描述中标注对模块、构建、工具链文档的影响。

## 禁止事项
- 禁止无验证证据直接合并行为变更。
- 禁止将文档同步规则作为可忽略项。

## 例外流程
- 紧急修复可临时简化说明，但需在后续补齐完整 PR 信息与文档变更记录。

## PR 例外小节模板（可直接复制）
```markdown
### 例外

- 触发原因：<原因>
- 影响范围：<范围>
- 审批人：<负责人>
- 失效条件：<条件>
- 截止时间：<YYYY-MM-DD>
- 回补任务：<链接>
- 当前状态：<生效中/已关闭>
```

## 验收清单
- 提交信息格式正确。
- PR 必填信息完整。
- 代码改动与文档改动建立映射。
- 文档同步范围、目标与行为变化说明完整。

## PR 文档同步检查清单（执行项）
- [ ] 若修改模块代码，已同步更新 docs/modules/<module-name>.md。
- [ ] 若修改 CMake 相关配置，已同步更新 docs/cmake/ 对应文档。
- [ ] 若修改 clang 工具链配置或脚本，已同步更新 docs/clangd/ 对应文档。
- [ ] 若修改 CI 流程，已同步更新 docs/CI/ 对应文档。
- [ ] 若修改设计规范或流程规则，已同步更新 docs/superpowers/specs/ 对应文档。
- [ ] 若修改注释模板或注释检查口径，已同步更新 docs/superpowers/specs/comment-standards.md（或等效规范文档）。
- [ ] PR 描述中已列出“代码改动 -> 文档改动”的映射关系。
- [ ] docs/modules、docs/cmake、docs/clangd、docs/CI、docs/superpowers/specs 目录与目标文档存在。

## PR 评审证据清单（执行项）
- [ ] PR 描述中已提供变更目的与影响范围。
- [ ] PR 描述中已提供验证方式与结果摘要。
- [ ] 若行为变化，已提供测试证据与回退策略。
- [ ] 若存在例外，已提供例外登记信息与回补任务链接。
- [ ] 若存在例外，已按 migration-exception.instructions.md 模板完整登记字段。
