# 代码规范使用指南

## 📚 目录
- [工具介绍](#工具介绍)
- [快速开始](#快速开始)
- [日常使用](#日常使用)
- [团队协作](#团队协作)
- [常见问题](#常见问题)

---

## 🛠️ 工具介绍

本项目使用以下工具来保证代码质量：

### 1. **clangd** - 智能补全和导航
- 提供精确的代码补全
- 快速跳转到定义/引用
- 实时错误提示
- 悬停显示详细信息

### 2. **clang-format** - 代码格式化
- 自动格式化代码
- 统一代码风格（Google Style）
- 保存时自动格式化

### 3. **clang-tidy** - 静态代码分析
- 检测潜在 bug
- 提供现代化建议
- 性能优化提示
- 代码风格检查

---

## 🚀 快速开始

### Step 1: 安装工具

```bash
# 安装 clangd、clang-format、clang-tidy
sudo apt update
sudo apt install -y clangd clang-format clang-tidy

# 验证安装
clangd --version
clang-format --version
clang-tidy --version
```

### Step 2: 安装 VS Code 扩展

打开 VS Code，会自动提示安装推荐扩展：
- **clangd** (llvm-vs-code-extensions.vscode-clangd) - **必需**
- CMake Tools
- CMake 语法高亮

**重要**: 安装 clangd 扩展后，需要禁用默认的 C/C++ IntelliSense（已在配置中自动处理）

### Step 3: 生成编译数据库

```bash
cd /home/prototype152/EX_MiracleVision
mkdir -p build && cd build
cmake ..
```

这会在 `build/` 目录下生成 `compile_commands.json`，clangd 会自动使用它。

### Step 4: 创建符号链接（可选，推荐）

为了让 clangd 更容易找到编译数据库：

```bash
cd /home/prototype152/EX_MiracleVision
ln -sf build/compile_commands.json .
```

### Step 5: 重启 VS Code

重启 VS Code 以激活所有配置。打开任意 `.cpp` 文件，应该会看到：
- 底部状态栏显示 "clangd: Idle" 或 "clangd: Indexing"
- 代码补全工作正常
- 悬停显示类型信息

---

## 📝 日常使用

### 代码补全

输入代码时，会自动显示智能补全建议：
- **函数参数提示**: 输入函数名后会显示参数信息
- **成员访问**: 输入 `.` 或 `->` 后显示成员列表
- **内联提示**: 显示推断的类型和参数名

### 代码导航

- **跳转到定义**: `F12` 或 `Ctrl+Click`
- **查找所有引用**: `Shift+F12`
- **查看类型定义**: `Ctrl+K F12`
- **返回上一位置**: `Alt+Left`

### 代码格式化

- **格式化整个文件**: `Shift+Alt+F`
- **格式化选中代码**: 选中后 `Ctrl+K Ctrl+F`
- **保存时自动格式化**: 已配置，保存文件时自动格式化

### 查看诊断信息

- **查看问题面板**: `Ctrl+Shift+M`
- **查看当前文件问题**: 底部状态栏显示错误/警告数量
- **悬停查看详情**: 鼠标悬停在警告/错误上查看详细信息

### 快速修复

当看到黄色波浪线（警告）或红色波浪线（错误）时：
1. 将光标移到问题代码上
2. 按 `Ctrl+.` 或点击💡图标
3. 选择建议的修复方案

---

## 👥 团队协作

### 代码提交前检查

**重要**: 提交代码前请确保：

1. ✅ 没有编译错误
2. ✅ 没有 clang-tidy 警告（或已确认可忽略）
3. ✅ 代码已格式化

### 手动格式化和检查

```bash
# 格式化单个文件
clang-format -i path/to/file.cpp

# 格式化整个项目（谨慎使用！）
find . -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i

# 检查单个文件
clang-tidy path/to/file.cpp -- -I/path/to/include

# 检查并自动修复
clang-tidy -fix path/to/file.cpp -- -I/path/to/include
```

### Git 集成（未来可选）

可以添加 Git pre-commit hook 自动检查：

```bash
#!/bin/bash
# .git/hooks/pre-commit

# 格式化已暂存的文件
git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|hpp|c|h)$' | while read file; do
    clang-format -i "$file"
    git add "$file"
done

# 运行 clang-tidy 检查
# ... (可选配置)
```

---

## ⚙️ 配置说明

### 文件说明

- **`.clangd`**: clangd 配置文件
  - 配置编译标志
  - 启用的检查规则
  - 诊断选项

- **`.clang-format`**: 代码格式化配置
  - Google C++ Style
  - 行宽限制: 100
  - 缩进: 2 空格
  - 指针/引用左对齐

- **`.clang-tidy`**: 静态分析配置
  - 启用的检查类别
  - 命名规范
  - 函数大小限制

- **`.vscode/settings.json`**: VS Code 工作区设置
  - clangd 参数
  - 格式化行为
  - 编辑器配置

### 自定义配置

团队可以根据需求调整配置文件：

1. **放宽检查**: 在 `.clang-tidy` 中移除特定检查
2. **调整格式**: 修改 `.clang-format` 中的选项
3. **修改行宽**: 调整 `ColumnLimit` (当前 100)

---

## ❓ 常见问题

### Q1: clangd 显示 "No compilation database found"

**解决方案**:
```bash
cd build
cmake ..
cd ..
ln -sf build/compile_commands.json .
```

### Q2: 代码补全不工作

**检查清单**:
1. ✅ clangd 扩展已安装且启用
2. ✅ `build/compile_commands.json` 存在
3. ✅ 底部状态栏显示 clangd 状态
4. ✅ C/C++ IntelliSense 已禁用（见 settings.json）

### Q3: 格式化后代码很乱

可能是配置冲突，确保：
- 只使用 clangd 作为格式化器
- `.clang-format` 配置正确
- 重启 VS Code

### Q4: clang-tidy 警告太多

可以临时禁用特定警告：
```cpp
// NOLINTNEXTLINE(warning-name)
int problematic_code = 0;
```

或在 `.clang-tidy` 中全局禁用。

### Q5: 修改配置后不生效

需要重新构建索引：
1. `Ctrl+Shift+P`
2. 输入 "clangd: Restart language server"
3. 等待重新索引完成

### Q6: 性能问题（索引很慢）

对于大型项目：
1. 增加内存限制（在 settings.json 中添加 `--limit-results=100`）
2. 排除不需要的目录（在 `.clangd` 中配置）
3. 使用更快的磁盘（SSD）

---

## 📖 参考资料

- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [clangd 文档](https://clangd.llvm.org/)
- [clang-format 配置](https://clang.llvm.org/docs/ClangFormatStyleOptions.html)
- [clang-tidy 检查列表](https://clang.llvm.org/extra/clang-tidy/checks/list.html)

---

## 🔄 更新日志

- **2026-02-20**: 初始配置
  - 添加 clangd、clang-format、clang-tidy 配置
  - 采用 Google C++ Style
  - 配置 VS Code 集成

---

**团队成员**: 如有任何问题或建议，请在团队群组讨论！
