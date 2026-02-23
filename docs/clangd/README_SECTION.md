# 代码规范章节（建议添加到 README.md）

> 以下内容可以添加到项目主 README.md 的适当位置

---

## 📏 代码规范

本项目采用严格的代码规范以确保代码质量和团队协作效率。

### 🎯 代码风格

- **风格指南**: Google C++ Style Guide
- **格式化工具**: clang-format
- **静态分析**: clang-tidy
- **智能补全**: clangd

### 🚀 快速开始

#### 1. 安装工具

```bash
sudo apt update
sudo apt install -y clangd clang-format clang-tidy
```

#### 2. 自动设置

```bash
./scripts/setup_code_style.sh
```

#### 3. VS Code 扩展

安装推荐的 VS Code 扩展（会自动提示）：
- **clangd** - 必需，提供智能补全和代码分析

### 📚 文档

- [代码风格指南](docs/CODE_STYLE_GUIDE.md) - 详细使用文档
- [团队设置清单](docs/CHECKLIST.md) - 新成员必读
- [实施总结](docs/IMPLEMENTATION_SUMMARY.md) - 配置说明
- [快速参考](docs/QUICK_REFERENCE.md) - 速查卡片

### 🔧 实用工具

```bash
# 格式化代码
./scripts/format_code.sh

# 检查代码质量
./scripts/check_code.sh

# 查看帮助
./scripts/format_code.sh --help
./scripts/check_code.sh --help
```

### ✅ 开发工作流

1. **编写代码** - 使用 clangd 提供的智能提示
2. **保存文件** - 自动格式化（已配置）
3. **检查问题** - 查看问题面板 (`Ctrl+Shift+M`)
4. **修复警告** - 使用快速修复 (`Ctrl+.`)
5. **提交代码** - 确保无错误后提交

### 🎓 新成员指南

如果你是新加入团队的成员，请：

1. 阅读 [团队设置清单](docs/CHECKLIST.md)
2. 完成环境配置
3. 验证所有功能正常工作
4. 在团队群组报告完成情况

### 💡 提示

- **快捷键**: 查看 [快速参考](docs/QUICK_REFERENCE.md)
- **问题排查**: 查看 [代码风格指南](docs/CODE_STYLE_GUIDE.md) 的常见问题部分
- **自定义配置**: 团队讨论后可调整 `.clang-format` 和 `.clang-tidy`

---
