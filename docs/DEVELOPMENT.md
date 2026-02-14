# 开发指南

本文档为 EX_MiracleVision 的开发者提供快速上手、代码规范、调试技巧以及团队协作流程，适用于队友与未来的开源贡献者。

---

## 📋 目录

- [开发环境设置](#开发环境设置)
- [代码风格与工具](#代码风格与工具)
- [构建与调试](#构建与调试)
- [测试与 CI 建议](#测试与-ci-建议)
- [调试技巧与常用命令](#调试技巧与常用命令)
- [Git 工作流与协作规范](#git-工作流与协作规范)
- [如何添加新模块 / 修改 CMake](#如何添加新模块--修改-cmake)

---

## 开发环境设置

推荐在 Ubuntu 22.04 上开发（也可在相同依赖的虚拟机中进行）。下面为快速环境搭建步骤：

1. 安装系统依赖（参考 `docs/ENVIRONMENT_SETUP.md` 或使用脚本）

```bash
# 运行安装脚本（root 用户以外）
./scripts/install_dependencies.sh
```

2. 使用合适的编辑器/IDE：
- VS Code（推荐）
  - 建议安装插件：C/C++, CMake Tools, clang-format, cpptools, CodeLLDB
- CLion（付费）
- Vim/Neovim（高级用户）

3. （可选）启用 `ccache` 加速重复编译：

```bash
sudo apt install -y ccache
# 在 CMake 配置时启用
cmake -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -S . -B build
```

4. 生成编译数据库（便于 IDE 跳转与静态分析）：

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# 将 compile_commands.json 软链接到项目根（某些工具期待在根目录）
ln -sf build/compile_commands.json compile_commands.json
```

---

## 代码风格与工具

1. C++ 标准：C++17（部分代码允许 C++20）
2. 格式化工具：`clang-format`（推荐）

提供基础 `.clang-format`（项目可自行微调）

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Inline
```

在 VS Code 中推荐配置保存时自动格式化。

3. 提交信息规范（Conventional Commits 风格建议）
```
feat(module): add new armor detector
fix(cmake): correct link to MVSDK
docs(readme): update quick start
chore(deps): upgrade fmt to 8.1.1
```

4. 静态分析与 lint：
- `clang-tidy`（建议配合 compile_commands.json 使用）
- `cppcheck`（可用于简单检查）

5. 单元测试框架（建议）：
- GoogleTest（若添加单元测试，请在 `test/` 下创建对应 CMake 目标）

---

## 构建与调试

### 常用构建命令

```bash
# 创建并构建（Release）
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)

# Debug 模式
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(nproc)

# 仅构建单个目标
cmake --build . --target mv-basic-armor -j4
```

### 生成可执行并运行

```bash
cd build
./bin/MiracleVision
# 或测试程序
./bin/minimum_vision
```

### 使用 IDE（VS Code）
- 打开项目根目录
- 确保 `build/compile_commands.json` 可用
- 使用 CMake Tools 插件配置 kit 和 build target

---

## 测试与 CI 建议

- 建议在 CI 中运行：
  - 依赖安装检查
  - CMake 配置（`cmake --build`）
  - 编译（使用 `-j2` 保证稳定）
  - 运行基本 smoke tests（如 `minimum_vision --help`）
- 可使用 GitHub Actions，示例任务：
  - jobs: build
    - runs-on: ubuntu-22.04
    - steps: checkout, setup gcc-11, install dependencies (apt), cmake, build

---

## 调试技巧与常用命令

1. GDB / LLDB

```bash
# 使用 gdb
gdb --args ./bin/MiracleVision --config ../configs/auto_aim.yaml
# 运行后使用 run / bt / info threads 等命令
```

2. AddressSanitizer / UndefinedBehaviorSanitizer（Debug 下启用）

在 CMake 中临时添加：

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -fno-omit-frame-pointer")
```

然后构建并运行以捕获内存错误。

3. 查看共享库依赖

```bash
ldd build/bin/MiracleVision | grep "not found"
# 若有未找到库，尝试 sudo ldconfig 或者设置 LD_LIBRARY_PATH
```

4. 追踪编译器 ICE（Internal Compiler Error）
- 首先确保使用 GCC 11（文档中记录了历史问题）
- 如遇 ICE，可尝试将受影响目标的优化降低为 `-O1`（在对应 CMakeLists 中对 target 使用 `target_compile_options`）

5. 日志排查
- 使用 `fmt` + `spdlog` 打印关键信息
- 日志文件路径：在 `configs/` 中配置

---

## Git 工作流与协作规范

1. 分支策略（建议）
- `main`：稳定发布分支
- `dev`：开发集成分支（所有功能合并到此）
- feature 分支：`feature/<name>`（功能开发）
- hotfix 分支：`hotfix/<name>`（修复生产 bug）
- 你当前分支 `vcpkg` 建议重命名为 `cmake-rebuild`（后续可合并到 `dev`）

2. 提交规范
- 每次提交保持原子性，便于回滚
- 提交信息遵循上方的 Conventional Commits 模式

3. Pull Request 流程
- 创建 PR 到 `dev`（不要直接推送 `main`）
- PR 应包含：变更说明、测试步骤、影响范围
- 至少一位 reviewer 才能合并
- CI 必须通过（编译通过 + 基本测试）

4. 代码评审要点
- API/接口变更是否向后兼容
- CMake 修改是否影响其他模块
- 是否添加或更新文档与测试
- 是否包含无关的大文件（避免将构建产物添加到 repo）

---

## 如何添加新模块 / 修改 CMake

1. 在 `module/` 下创建子目录，例如 `module/my_module/`。
2. 在子目录中创建源文件和 `CMakeLists.txt`。参考现有模块的 `CMakeLists.txt`。
3. 在 `module/CMakeLists.txt` 中添加 `add_subdirectory(my_module)`。
4. 如果模块需要在主程序中被使用，在 `base/CMakeLists.txt` 中将该库添加到 `target_link_libraries(MiracleVision ...)`。

示例 `module/my_module/CMakeLists.txt`：
```cmake
message(STATUS "  Configuring my_module...")

add_library(mv-my-module SHARED
    my_module.cpp
    my_module.hpp
)

target_include_directories(mv-my-module PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# 链接常见依赖
target_link_libraries(mv-my-module PUBLIC
    opencv_core
    mv-logger
)

set_target_properties(mv-my-module PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

message(STATUS "    ✓ my_module configured")
```

---

## 常见问题（给开发者的提示）

- 如果编译失败且日志显示 `internal compiler error`，首先切换到 GCC 11 或对有问题的 target 降低优化级别。
- 增加 `-Werror` 前请确保 CI 已捕获所有警告并确认修复策略。
- 编辑 `configs/` 下的 YAML/XML 配置以调整运行参数，调试时可使用 `--config` 参数。

---

**最后更新**: 2026-02-14  
**维护者**: [@prototype956](https://github.com/prototype956)
