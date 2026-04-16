# Sim Serial Bridge Phase2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 完成 EX 与 AT 的仿真串口闭环第二阶段，使下行命令驱动真实仿真体、上行反馈改为真实姿态，并补齐配置化与联调验收脚本。

**Architecture:** 保持 RM 15B 下行与 28B 上行协议不变，在 AT 侧将 serial bridge 从“协议转发层”升级为“控制与状态映射层”。EX 侧保持 SerialNode/Pipeline/FSM 结构不变，仅消费真实反馈。通过脚本化验收将断连重连行为固化为可回归流程。

**Tech Stack:** C++17（EX）、Rust + Bevy（AT）、TCP（127.0.0.1:19090/19091）、YAML 配置、bash 验收脚本。

---

### Task 1: 下行命令驱动真实云台与发射

**Files:**
- Modify: at_vision_simulator/src/sim_bridge/serial_bridge.rs
- Modify: at_vision_simulator/src/main.rs

- [ ] **Step 1: 设计控制资源与超时回退策略**
定义 bridge 到仿真控制层的共享资源：目标 yaw/pitch、fire 脉冲、tracking、命令时间戳。

- [ ] **Step 2: 将 last_cmd 映射到共享资源**
在 serial bridge 中解析完整下行帧后更新共享资源，并记录最后命令时间。

- [ ] **Step 3: 在主调度链接入资源仲裁**
在 gimbal_controls / projectile_launch 前增加“串口命令优先、键鼠输入回退”的仲裁逻辑。

- [ ] **Step 4: 增加命令超时回退**
当命令超过阈值（建议 100ms）未更新时，自动回退到本地输入，避免网络抖动卡死。

- [ ] **Step 5: 构建验证**
Run: `cd /home/nuc/at_vision_simulator && cargo build`
Expected: build success

### Task 2: 上行反馈改为真实姿态与状态

**Files:**
- Modify: at_vision_simulator/src/sim_bridge/serial_bridge.rs
- Reference: EX_MiracleVision/src/hal/serial/rm_protocol.hpp

- [ ] **Step 1: 读取真实仿真状态**
从当前仿真实体提取 yaw/pitch、角速度、姿态四元数、弹速，替换占位值。

- [ ] **Step 2: 按 RM 28B 协议量化编码**
按 i16 ×100 / ×10000 规则填充字段，维持头尾、CRC 和 seq 递增。

- [ ] **Step 3: 加入边界保护**
对四元数归一化并对 i16 字段做饱和裁剪，避免异常值污染 EX 侧状态。

- [ ] **Step 4: 构建验证**
Run: `cd /home/nuc/at_vision_simulator && cargo build`
Expected: build success

### Task 3: serial bridge 配置入口化

**Files:**
- Modify: at_vision_simulator/src/main.rs
- Modify: at_vision_simulator/src/sim_bridge/mod.rs
- Modify: at_vision_simulator/src/sim_bridge/serial_bridge.rs

- [ ] **Step 1: 扩展配置结构**
新增可配置字段：bind_addr、status_period_ms、enemy_color、bullet_speed。

- [ ] **Step 2: 接入配置读取入口**
优先读取环境变量，其次使用默认值；启动时打印最终生效配置。

- [ ] **Step 3: 将配置注入插件资源**
在 plugin build 阶段注入 SimSerialBridgeConfig，替代硬编码常量。

- [ ] **Step 4: 构建验证**
Run: `cd /home/nuc/at_vision_simulator && cargo build`
Expected: build success

### Task 4: 联调验收脚本（含断连重连）

**Files:**
- Create: EX_MiracleVision/scripts/sim_serial_e2e.sh
- Modify: EX_MiracleVision/docs/test/serial/sim_serial_test.md

- [ ] **Step 1: 脚本启动链路**
脚本负责按顺序启动 AT 与 EX，检查 19090/19091 连通与进程状态。

- [ ] **Step 2: 注入断连重连测试**
在脚本中模拟 19091 短断连并恢复，观察 EX 是否持续 AUTO_AIM 且恢复收发。

- [ ] **Step 3: 增加日志断言**
断言关键日志：连接建立、断连软降级、自动重连成功、无 ERROR 状态跳转。

- [ ] **Step 4: 输出结果与归档**
输出 PASS/FAIL 摘要并保存日志到 logs/e2e_*.log。

- [ ] **Step 5: 执行验证**
Run: `cd /home/nuc/EX_MiracleVision && bash scripts/sim_serial_e2e.sh`
Expected: script PASS + 关键断言通过

### Task 5: 最终门禁与文档同步

**Files:**
- Modify: EX_MiracleVision/docs/modules/serial/sim_serial.md
- Modify: EX_MiracleVision/docs/test/serial/sim_serial_test.md
- Modify: EX_MiracleVision/docs/cmake/serial-hal-targets.md

- [ ] **Step 1: 更新代码->文档映射**
确保本次行为变化和配置变化在文档中有对应描述。

- [ ] **Step 2: EX 校验**
Run: `cd /home/nuc/EX_MiracleVision && cmake --build build -j$(nproc)`
Expected: build success

- [ ] **Step 3: EX 静态检查（变更文件）**
Run: `cd /home/nuc/EX_MiracleVision && scripts/check_code.sh --file src/hal/serial/sim_serial.cpp && scripts/check_code.sh --file src/test/serial/sim_serial_test.cpp`
Expected: no new warnings/errors on changed files

- [ ] **Step 4: AT 校验**
Run: `cd /home/nuc/at_vision_simulator && cargo fmt && cargo build`
Expected: build success

## 预期效果（交付判定）

1. EX 下发控制将驱动 AT 仿真体真实动作（不再仅缓存命令）。
2. EX 接收的是 AT 实时姿态/角速度/弹速，预测与状态切换更接近真机。
3. 端口、周期、敌我色支持配置化切换，联调复现成本下降。
4. 断连重连被脚本化验证，回归测试可一键复现。
