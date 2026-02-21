# 代码规范实施清单

## 📋 团队成员设置清单

每位团队成员在开始开发前，请按照以下清单完成设置：

### ✅ 环境设置（必需）

- [ ] **安装基础工具**
  ```bash
  sudo apt update
  sudo apt install -y clangd clang-format clang-tidy
  ```

- [ ] **拉取最新代码**
  ```bash
  git pull origin cmake_rebuild
  ```

- [ ] **运行设置脚本**
  ```bash
  cd /path/to/EX_MiracleVision
  ./scripts/setup_code_style.sh
  ```

- [ ] **安装 VS Code 扩展**
  - clangd (必需)
  - CMake Tools (推荐)
  - GitLens (推荐)

- [ ] **重启 VS Code**
  - 确保所有配置生效

### ✅ 功能验证（必需）

- [ ] **代码补全**
  - 打开任意 `.cpp` 文件
  - 输入代码，检查是否有智能补全
  - 快捷键: `Ctrl+Space`

- [ ] **代码导航**
  - 右键点击函数/类名
  - 选择 "Go to Definition" 或按 `F12`
  - 应该能正确跳转

- [ ] **错误检测**
  - 故意写一个错误（如拼写错误的函数名）
  - 应该立即显示红色波浪线

- [ ] **代码格式化**
  - 打开一个格式不规范的文件
  - 按 `Shift+Alt+F` 格式化
  - 代码应该自动调整格式

- [ ] **状态栏检查**
  - 底部状态栏应显示 "clangd: Idle" 或相关状态

### ✅ 开发流程（日常）

每次编写代码时：

- [ ] **编码前**: 确保 clangd 已加载（状态栏显示）
- [ ] **编码中**: 注意代码提示和警告
- [ ] **提交前**:
  - [ ] 格式化代码: `Shift+Alt+F`
  - [ ] 检查问题面板: `Ctrl+Shift+M`
  - [ ] 确保没有错误（警告可根据情况处理）

---

## 📦 项目配置文件清单

以下文件应提交到 Git 仓库：

### ✅ 必需配置文件

- [x] `.clangd` - clangd 配置
- [x] `.clang-format` - 代码格式化规则
- [x] `.clang-tidy` - 静态分析规则
- [x] `.vscode/settings.json` - VS Code 工作区设置
- [x] `.vscode/extensions.json` - 推荐扩展列表
- [x] `.vscode/c_cpp_properties.json` - C++ 配置
- [x] `.gitignore` - 忽略规则（已更新）
- [x] `CMakeLists.txt` - CMake 配置（已添加 `CMAKE_EXPORT_COMPILE_COMMANDS`）

### ✅ 文档

- [x] `docs/CODE_STYLE_GUIDE.md` - 代码风格指南
- [x] `docs/CHECKLIST.md` - 本文件

### ✅ 实用脚本

- [x] `scripts/setup_code_style.sh` - 一键设置脚本
- [x] `scripts/format_code.sh` - 代码格式化脚本
- [x] `scripts/check_code.sh` - 静态分析脚本

---

## 🔄 团队工作流程

### 日常开发

1. **开始开发前**
   ```bash
   git pull
   cd build && cmake .. && cd ..
   ```

2. **编写代码**
   - 使用 clangd 提供的智能提示
   - 注意并修复警告/错误
   - 保存时自动格式化（已配置）

3. **提交前检查**
   ```bash
   # 可选：格式化整个项目
   ./scripts/format_code.sh --dry-run

   # 可选：静态分析
   ./scripts/check_code.sh --dir module/xxx
   ```

4. **提交代码**
   ```bash
   git add .
   git commit -m "feat: xxxx"
   git push
   ```

### 代码审查

审查者检查清单：

- [ ] 代码格式符合 Google Style
- [ ] 没有明显的 clang-tidy 警告
- [ ] 命名符合规范（类名 CamelCase，变量 snake_case）
- [ ] 私有成员以 `_` 结尾
- [ ] 注释清晰（中文或英文均可）

---

## ⚙️ 配置自定义

如果团队需要调整规范，修改以下文件：

### 放宽检查规则

编辑 `.clang-tidy`，在 `Checks:` 部分添加要忽略的规则：

```yaml
Checks: >
  -*,
  google-*,
  ...
  -your-rule-to-disable
```

### 调整格式化选项

编辑 `.clang-format`，修改相关选项：

```yaml
# 例如：将行宽从 100 改为 120
ColumnLimit: 120

# 例如：将缩进从 2 改为 4
IndentWidth: 4
```

修改后需要：
1. 提交到 Git
2. 通知团队成员更新
3. 重新格式化现有代码（谨慎）

---

## 🐛 常见问题快速解决

### 问题 1: clangd 提示 "No compilation database"

**解决**:
```bash
cd build
cmake ..
cd ..
ln -sf build/compile_commands.json .
```

### 问题 2: 代码补全不工作

**解决**:
1. 检查 clangd 扩展是否已安装
2. 检查状态栏是否显示 clangd 状态
3. 重启 clangd: `Ctrl+Shift+P` → "clangd: Restart"
4. 重启 VS Code

### 问题 3: 格式化后代码很乱

**解决**:
1. 确保 `.clang-format` 文件存在
2. 确保使用 clangd 作为格式化器（settings.json 已配置）
3. 重启 VS Code

### 问题 4: 性能问题（卡顿）

**解决**:
1. 检查是否有太多文件需要索引
2. 排除不必要的目录（在 `.clangd` 中配置）
3. 增加系统内存

---

## 📊 团队采用进度追踪

| 成员 | 工具安装 | 扩展安装 | 功能验证 | 首次格式化 | 状态 |
|------|----------|----------|----------|------------|------|
| 成员1 | ⬜ | ⬜ | ⬜ | ⬜ | 待开始 |
| 成员2 | ⬜ | ⬜ | ⬜ | ⬜ | 待开始 |
| 成员3 | ⬜ | ⬜ | ⬜ | ⬜ | 待开始 |

**说明**: 请每位成员完成设置后在团队群组报告，并更新此表格。

---

## 🎯 里程碑

- [ ] **Phase 1**: 所有成员完成基础设置（目标: 1天）
- [ ] **Phase 2**: 团队适应新工作流程（目标: 1周）
- [ ] **Phase 3**: 格式化现有代码库（谨慎，需团队讨论）
- [ ] **Phase 4**: 添加 Git hooks（可选）

---

## 📞 支持

遇到问题？

1. 查阅 `docs/CODE_STYLE_GUIDE.md`
2. 在团队群组询问
3. 查看 clangd 日志: `~/.cache/clangd/`

---

**更新日期**: 2026-02-20
**维护者**: 团队全员
