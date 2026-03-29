---
description: EX_MiracleVision 重构期架构边界与目录治理规则。
applyTo: "**"
---

# Architecture Boundary Rules

## 适用范围
- 适用于重构期所有目录与跨模块调用行为。
- 侧重目录级与架构级约束，不负责描述调试过程细节。
- 调试过程细节见 collab-debugging.instructions.md。

## 必须规则
- 新功能与新实现默认仅放在 src 目录。
- 旧目录 base、module、devices 仅允许最小修复，不允许功能扩展。
- 未经用户确认，不得变更公共接口、目录结构与跨模块调用关系。
- 进入编码前必须先完成模块映射与调用链说明。
- 本文件约束优先于协同流程建议；若与协同调试规则冲突，以本文件为准。
- 新增模块必须在 docs/modules/ 下建立对应说明文档。
- 模块行为或接口变更时，必须同步更新 docs/modules/ 对应文档。
- 新代码落位约束：
	- 实现类 -> src/modules/<module-name>/。
	- 接口定义 -> src/interfaces/。
	- 工具类 -> src/tool/。
	- 测试程序 -> src/test/。

## 新模块接入顺序
- 先确认接口是否已存在（IDetector/ISolver/IPredictor 等）。
- 再在 src/modules/<name>/ 下新增 .hpp 与 .cpp 实现文件。
- 在实现 .cpp 中完成工厂注册。
- 最后在 src/modules/CMakeLists.txt 中接入新目标。

## 相关文档同步
- 模块实现或接口变更 -> 同步 docs/modules/<module-name>.md。
- 模块接入或修改 CMake 目标 -> 同步 docs/cmake/ 对应文档。
- clang 工具链口径变更 -> 同步 docs/clangd/ 对应文档。
- CI 流程或门禁变更 -> 同步 docs/CI/ 对应文档。

## 推荐规则
- 每次改动前给出影响范围，优先小步可回滚变更。
- 跨层调用需求优先通过接口抽象而非直接耦合。

## 禁止事项
- 禁止在旧目录新增模块或扩展功能。
- 禁止先改代码后补调用链解释。

## 例外流程
- 若必须触及旧目录，需说明为何无法在 src 完成，并记录迁移计划。

## 验收清单
- 改动目录与边界规则一致。
- 调用链说明先于实现变更。
- 无未授权的接口或目录结构变更。

## 架构边界检查清单（执行项）
- [ ] 新功能实现均位于 src 目录。
- [ ] 若触及旧目录，已说明必要性与迁移计划。
- [ ] 变更前已给出模块映射与调用链说明。
- [ ] 未经确认未修改公共接口、目录结构或跨模块调用关系。
- [ ] 新增模块已建立 docs/modules/<module-name>.md 文档。
- [ ] 模块行为或接口变更已同步更新 docs/modules/<module-name>.md。
