# EX_MiracleVision 持续集成 (CI) 工作流文档

本文档详细说明了 `EX_MiracleVision` 项目中代码集成的自动化检查流程。

## 1. 概述

本项目使用 GitHub Actions 作为持续集成（CI）工具。CI 流水线的定义文件位于项目根目录下的 `.github/workflows/ci.yml`。

每次有代码变动触发了特定事件后，GitHub Actions 都会自动分配一台隔离的云服务器执行流水线，以确保引入的代码符合严格的代码规范，且不会破坏现有工程的编译流程。

## 2. 触发条件与并发控制

- **事件触发**：有 Pull Request (PR) 被发起或更新。
- **目标分支限制**：仅当该 PR 目标分支为 `dev` 或 `main` 时触发。
- **并发控制策略**：当同一个 PR 的分支有新的 Push 提交时，正在运行的旧 CI 任务会自动被取消（`cancel-in-progress: true`）。这确保系统只会测试最新的提交记录，并大大节省了服务器资源计算时间。

## 3. 工作流详情 (Jobs)

整个 CI 工作流分为三个相对独立但又相互关联的任务（Jobs）。其中，为保证效率并能最快地暴露低级错误，耗时最短的格式检查会率先甚至独立给出结果，而耗时的编译与分析则在随后进行。

### 📌 Job 1: 代码格式检查 (🎨 Code Format Check)
该任务排在最前，**不需要编译项目即可完成**，主要负责检查新增或修改的代码是否符合项目的代码风格规范。
- **运行环境**：`ubuntu-22.04`
- **核心工具**：`clang-format-14`
- **检查范围**：项目中除第三方库（`3rdparty/`）、构建目录（`build/`）以外的所有的 `*.cpp`, `*.hpp`, `*.h` C++源码文件。
- **执行逻辑**：
  - 使用 `--dry-run --Werror` 选项进行“试运行”检测，不对代码作实体改动。
  - 如果发现任何格式不符的部分，立即输出带有 Diff 对比的详细错误信息，并判定该文件任务失败。
- **如何修复**：
  若此检查未通过，请勿尝试手动修改空格对齐。**推荐的方法是直接在本地终端运行一键格式化脚本 `scripts/format_code.sh`**。修改生成后，将其 commit 并重新推送到 PR 中即可。

### 📌 Job 2: 编译检查 (🔨 Build Check)
该任务负责验证代码是否能够在纯净的 Ubuntu 环境中正常配置外置图谱、成功地构筑完成出可执行文件与链接库。
- **运行环境**：`ubuntu-22.04`
- **系统依赖**：`build-essential`, `cmake`, `pkg-config`，以及用于项目运行的各项核心计算开源库，如 `libopencv-dev`, `libfmt-dev`, `libspdlog-dev`, `libeigen3-dev`, `libyaml-cpp-dev`, `nlohmann-json3-dev`, `libtbb-dev` 等。
- **执行逻辑**：
  1. 下载/ Checkout 此 PR 节点的项目源码。
  2. 安装项目依赖的所有系统级别软件包。
  3. 执行 CMake 构建配置。此时设置了特殊参数（开启 `Release` 模式，开启 `CMAKE_EXPORT_COMPILE_COMMANDS` 用于输出编译参数，同时关闭测试用例及部分不必要的外部硬件 SDK 支持如 OpenVINO, ONNXRuntime_ 等以节省时间）。
  4. 利用所有可用 CPU 核心进行多线程并行编译 `cmake --build build -j$(nproc)`。
- **输出产物 (Artifact)**：
  编译完成后，会将 CMake 顺利生成的**编译数据库 `compile_commands.json` 文件封存并上传**（通常保留 1 天时间的有效期）。此文件对之后的精准静态分析起着至关重要的作用。

### 📌 Job 3: 静态分析 (🔍 Clang-Tidy Analysis)
该任务利用 LLVM 静态分析工具来深入扫描代码中的潜在逻辑层漏洞、性能隐患或危险的用法，该任务强制依赖以上的编译任务（Job 2）生成的文件。
- **前置依赖**：必须等待 `build` 任务彻底完成并成功。
- **运行环境**：`ubuntu-22.04`
- **核心工具**：与 Build 阶段完全相同的依赖库，外加 `clang-tidy-14` 分析器。
- **执行逻辑**：
  1. 从 GitHub Artifact 存储下载前一个 `build` 任务上传的 `compile_commands.json` 文件到本地 `build/` 目录下。
  2. 遍历所有的项目有效源码文件，并使用 `clang-tidy-14 -p build` 的附加编译参数逐一发起审查操作。
  3. **严格模式启用**：所有的 `warning` 级别的提示在此环境都会被强制直接判定为 `error` 报错（`--warnings-as-errors='*'`）。
  4. 汇总所有文件的警告和报错总数。
- **如何修复**：
  如果静态分析报错，请仔细查阅 GitHub Actions 控制台打印中出现的警告说明。你可以在本地运行对应脚本 `scripts/check_code.sh` 来直接复现与定位代码到底错在了哪行并按规范要求修改它。

## 4. CI 开发最佳实践

为了减少不必要的修改反馈循环迭代，向 `dev` 或 `main` 提 Pull Request 的开发人员建议养成以下强迫症好习惯：

- **禁止依赖 CI 帮你“初查”代码**：在把代码 `git push` 到远端仓库前，最好先在你的本地终端手动运行一次：
  ```bash
  $ bash scripts/format_code.sh   # 处理代码对齐
  $ bash scripts/check_code.sh    # 本地跑通检查潜在 bug 漏洞
  ```
- **理解 Tidy 为何需要 Build 生成 JSON?**：因为 C++ 静态审查过程中，如果没有精确的编译参数传入（例如宏定义、系统 include 包含路径等），工具根本不知道代码是基于什么条件展开的，进而导致大量误报或漏报。这就是为什么分析任务必须紧跟着构建之后，以此利用完整的 `compile_commands.json` 作为参照。
