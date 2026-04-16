# Simulator Integration Docs

本文档目录用于记录 EX_MiracleVision 对接 at_vision_simulator 的实现方案与联调流程。

## 文档范围

- 本目录只覆盖仿真器接入相关内容。
- 本轮实现明确采用非 ROS2 通道（TCP 自定义流）。
- 开发里程碑采用：先实时输入联调，再闭环控制回注。

## 阅读顺序

1. [stream-protocol-spec.md](stream-protocol-spec.md)
2. [integration-overview.md](integration-overview.md)
3. [iteration-1-function-checklist.md](iteration-1-function-checklist.md)

## 当前阶段

当前执行第一迭代（Milestone B）：

- 目标：AT 推送实时图像与位姿，EX 通过 SimCamera 实时取流并进入现有 Pipeline。
- 边界：不做控制回注，不引入 ROS2，不改 EX 旧目录。
