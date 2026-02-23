# EX_MiracleVision: Clangd 代码架构使用与维护指南

这是为您与协作团队准备的 **Clangd 框架核心上手文档**。本项目使用 `clangd` + `clang-format` + `clang-tidy` 构建了现代化的 C++ 代码开发与分析方案。

通过本指南，您将了解该框架是**如何运作的**，以及**如何使用、修改、拓展和维护**这套架构。

---

## 一、 框架概览 (架构组成)

本套系统无需您手动执行繁杂的命令，因为所有的参数和规则已经落实在了以下几个核心文件中：

1. **`.clangd` (IDE 引擎配置)**
   * **作用**：指导 IDE（如 VSCode 或 CLion）如何解析 C++ 代码、到哪里寻找头文件、开启哪些后台检查。
   * **特点**：已配置好使用 C++17 语法、开启后台索引，屏蔽了第三方库（`3rdparty`）和构建目录的报错，让 IDE 免受无关报错的干扰。
2. **`.clang-format` (代码格式化规则)**
   * **作用**：控制代码排版（如缩进、空格、换行、大括号位置）。
   * **特点**：基于 Google C++ Style，但在参数换行、对齐等方面做了更符合开发现状的微调（比如限制每行 100 字符，强制 2 空格缩进）。
3. **`.clang-tidy` (代码质量诊断/静态分析)**
   * **作用**：深层扫描代码中的 Bug、内存泄漏、不安全的强转、未使用的变量等，并在 IDE 中划红线警告。
   * **特点**：组合了 `modernize`（建议使用现代C++特性），`performance`（性能提醒），`bugprone`（易错代码提醒）等检查集。
4. **`scripts/format_code.sh` 与 `scripts/check_code.sh` (辅助脚本)**
   * **作用**：在终端提供一键式的全局/局部检查与修复功能，方便在提交代码前或 CI/CD 流程中使用。

---

## 二、 快速上手：如何使用

要让这套框架真正在每个人电脑上生效并提供强大的代码补全，请执行以下步骤：

### 1. 安装核心依赖
所有协作者都需要在系统的宿主机或 WSL/Ubuntu 中安装工具：
```bash
sudo apt update
sudo apt install clangd clang-format clang-tidy
```

### 2. 生成“编译数据库” (必需的第一步！)
`clangd` 能精准解析代码的前提是它要知道 CMake 是怎么编译每个源码文件的。在每次添加新源码文件后，执行以下操作来更新**编译数据库（`compile_commands.json`）**：
```bash
# 只要使用项目自带的 cmake 配置，就会自动在 build 目录下生成该文件
mkdir build && cd build
cmake ..
```
`.clangd` 文件已经配置好了自动去 `build/` 目录下读取此 JSON 文件。

### 3. 在 IDE 中启用插件 (以 VSCode 为例)
* 安装插件：在插件市场搜索并安装 `clangd` (由 LLVM 提供)。
* **非常重要**：如果您之前安装了微软自带的 `C/C++ (ms-vscode.cpptools)`，请在设置中**禁用它的 IntelliSense 功能**，以免与 clangd 互相冲突卡顿（通常安装 clangd 插件时会弹窗提示禁用，点 Yes 即可）。

### 4. 日常开发与提交前的检查
开发过程：在 IDE 书写代码时，遇到报错会直接显示。悬停鼠标可看诊断信息。

**提交代码 (Git Commit) 前，请务必在终端执行脚本**：
```bash
# 一键自动排版项目中的所有代码
bash scripts/format_code.sh

# 检查所有代码是否违规报错 (不修改文件，仅查看)
bash scripts/check_code.sh

# 如果您想偷懒，也可以让脚本尝试帮您修复警告 (比如补全 const，替换 auto 等)
bash scripts/check_code.sh --fix
```

---

## 三、 如何修改规则 (个性化定制)

如果团队对某些代码规矩不爽，想修改它，很容易。

### 1. 修改代码格式 (缩进/换行) -> 修改 `.clang-format`
如果您觉得大括号一定要换行，或者缩进想改成 4 个空格，请打开 `.clang-format`，修改：
* `IndentWidth: 4` (将基于 Google 默认的 2 改为 4)
* `BreakBeforeBraces: Custom` (调整换行策略)
> 修改后，在 VSCode 中按 `Ctrl + Shift + F`，或者运行 `format_code.sh`，所有文件会立马采用新配置。

### 2. 觉得某条警告太烦了？ -> 修改 `.clang-tidy` 或 `.clangd`
如果您在编写代码时，代码下面有黄色波浪线，写着 `[modernize-use-auto]` 建议您用 auto 关键字，而您不想用：
1. 打开 `.clang-tidy`。
2. 在 `Checks:` 列表的末尾加上：`,-modernize-use-auto` （前面加上负号 `-` 代表禁用此规则）。
3. **同时**打开 `.clangd`（因为 IDE 检查受此文件控制），在 `Diagnostics -> ClangTidy -> Remove:` 下方添加：
   `- modernize-use-auto`。
4. 重启 IDE 或重启 clangd server 即可生效。

### 3. 如何给个别代码豁免检查？
有时候某一行代码虽然不规范，但是是“有意为之”的（比如某些特殊指针强转）。不需要改全局配置，只需在该行代码上方加一句注释即可：
```cpp
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
auto* ptr = reinterpret_cast<uint8_t*>(&data);
```

---

## 四、 如何拓展与维护

### 1. 编译数据库维护 (最常见的卡顿、报红原因)
*症状：* `#include "xxxx"` 头文件下面划红线说找不到文件，或者 IDE 不提示最新增加的类。
*原因：* 您的 CMakeLists 已经被修改过了（加入了新的源文件，或换了新路径），但是 `compile_commands.json` 没更新。
*解决办法：*
```bash
cd build
cmake ..   # 重新生成一次参数即可
```

### 2. 拓展：如何屏蔽新引入的第三方库
如果您在 `3rdparty/` 之外又引入了另一个极其庞大的库（比如叫 `libs/foo`），里面有无数个 warning。为了不让这些 warning 干扰你：
1. 在 `.clangd` 添加屏蔽：
   ```yaml
   Includes:
     IgnoreHeader:
       - 3rdparty/.*
       - build/.*
       - libs/foo/.*   # <--- 新增
   ```
2. 在 `.clang-tidy` 中修改 `HeaderFilterRegex`，确保它不匹配 `libs/foo/`。
3. 修改 `scripts/format_code.sh` 和 `scripts/check_code.sh` 脚本中的排除路径列表 `EXCLUDE_DIRS`，加入新库的文件夹名字。

### 3. 维护：CI/CD 自动化
您的代码仓库中包含了一个 Github Actions 的 `.github/workflows/ci.yml` 脚本。其底层使用的命令实际上正是我们的 `clang-format` 和 `clang-tidy` 命令行。
通过本文档维护这套基础架构后，Github CI 服务器就会严格按照您在文件里制定的新规则，自动在任何 Pull Request 合并前阻拦不合规的代码，**一套配置、本地/云端一致**。
