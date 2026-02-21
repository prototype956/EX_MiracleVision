# 📁 代码规范文件总览

本文档展示所有与代码规范相关的文件及其用途。

---

## 🎯 配置文件（项目根目录）

```
EX_MiracleVision/
├── .clangd                    # clangd 配置
│   ├── 编译标志
│   ├── 检查规则
│   └── 诊断选项
│
├── .clang-format              # 代码格式化规则
│   ├── Google C++ Style
│   ├── 缩进、换行规则
│   └── 命名和对齐
│
├── .clang-tidy                # 静态分析规则
│   ├── 启用的检查类别
│   ├── 命名规范
│   └── 代码质量规则
│
└── .gitignore                 # Git 忽略规则（已更新）
    └── 忽略 clangd 缓存和编译数据库
```

---

## 🔧 VS Code 配置（.vscode/）

```
.vscode/
├── settings.json              # 工作区设置
│   ├── clangd 参数配置
│   ├── 格式化行为（保存时格式化）
│   ├── 编辑器设置
│   └── 禁用 C++ IntelliSense
│
├── extensions.json            # 推荐扩展
│   ├── clangd（必需）
│   ├── CMake Tools
│   └── CMake 语法高亮
│
└── c_cpp_properties.json      # C++ 配置
    └── 编译数据库路径
```

---

## 📚 文档（docs/）

```
docs/
├── CODE_STYLE_GUIDE.md        # 详细使用指南 ⭐
│   ├── 工具介绍
│   ├── 快速开始
│   ├── 日常使用
│   ├── 团队协作
│   └── 常见问题
│
├── CHECKLIST.md               # 团队设置清单 ⭐
│   ├── 环境设置
│   ├── 功能验证
│   ├── 配置文件清单
│   └── 团队工作流程
│
├── IMPLEMENTATION_SUMMARY.md  # 实施方案总结
│   ├── 已完成配置
│   ├── 下一步操作
│   ├── 预期效果
│   └── 配置细节
│
├── QUICK_REFERENCE.md         # 快速参考卡片
│   ├── 快捷键速查
│   ├── 常用命令
│   ├── 命名规范
│   └── 专业提示
│
├── README_SECTION.md          # README 章节建议
│   └── 可添加到主 README 的内容
│
└── FILES_OVERVIEW.md          # 本文件
    └── 文件结构和用途说明
```

---

## 🛠️ 实用脚本（scripts/）

```
scripts/
├── setup_code_style.sh        # 一键设置脚本 ⭐
│   ├── 检查工具安装
│   ├── 生成编译数据库
│   ├── 创建符号链接
│   ├── 验证配置
│   └── 安装 VS Code 扩展
│
├── format_code.sh             # 代码格式化工具 ⭐
│   ├── 格式化整个项目
│   ├── 格式化指定目录
│   ├── 格式化检查（dry-run）
│   └── 排除第三方库
│
└── check_code.sh              # 静态分析工具 ⭐
    ├── 检查代码质量
    ├── 自动修复选项
    ├── 统计警告/错误
    └── 支持单文件/目录/项目
```

---

## 📊 文件关系图

```
        [开发者]
           │
           ├─── 使用 ───> [VS Code + clangd]
           │                    │
           │                    ├─ 读取 ─> .clangd
           │                    ├─ 读取 ─> .clang-format
           │                    ├─ 读取 ─> .clang-tidy
           │                    └─ 读取 ─> compile_commands.json
           │
           ├─── 运行 ───> [setup_code_style.sh]
           │                    ├─ 安装工具
           │                    ├─ 生成编译数据库
           │                    └─ 配置 VS Code
           │
           ├─── 运行 ───> [format_code.sh]
           │                    └─ 调用 clang-format
           │
           ├─── 运行 ───> [check_code.sh]
           │                    └─ 调用 clang-tidy
           │
           └─── 查阅 ───> [文档]
                              ├─ CODE_STYLE_GUIDE.md
                              ├─ CHECKLIST.md
                              └─ QUICK_REFERENCE.md
```

---

## 🎯 使用优先级

### **首次设置**（新成员必读）
1. ⭐ `docs/CHECKLIST.md` - 跟随清单完成设置
2. ⭐ `scripts/setup_code_style.sh` - 一键设置环境
3. ⭐ `docs/CODE_STYLE_GUIDE.md` - 了解详细用法

### **日常开发**
1. ⭐ `docs/QUICK_REFERENCE.md` - 快捷键和命令速查
2. 💡 VS Code 中直接使用快捷键（如 `Shift+Alt+F`）
3. 💡 保存时自动格式化（无需手动操作）

### **代码提交前**
1. 💡 `scripts/format_code.sh --dry-run` - 检查格式
2. 💡 `scripts/check_code.sh` - 静态分析
3. 💡 VS Code 问题面板 (`Ctrl+Shift+M`) - 查看错误

### **配置调整**（团队讨论后）
1. `.clang-format` - 调整格式化规则
2. `.clang-tidy` - 启用/禁用检查
3. `.clangd` - 修改 clangd 行为

---

## 📦 文件大小

| 文件类型 | 数量 | 说明 |
|---------|------|------|
| 配置文件 | 4 | `.clangd`, `.clang-format`, `.clang-tidy`, `.gitignore` |
| VS Code 配置 | 3 | `settings.json`, `extensions.json`, `c_cpp_properties.json` |
| 文档 | 6 | Markdown 文档 |
| 脚本 | 3 | Bash 脚本 |
| **总计** | **16** | **所有代码规范相关文件** |

---

## 🔍 快速查找

**我想...**

- **设置环境** → 运行 `scripts/setup_code_style.sh`
- **格式化代码** → 按 `Shift+Alt+F` 或运行 `scripts/format_code.sh`
- **检查代码** → 运行 `scripts/check_code.sh`
- **查看快捷键** → 阅读 `docs/QUICK_REFERENCE.md`
- **解决问题** → 查看 `docs/CODE_STYLE_GUIDE.md` 常见问题部分
- **了解命名规范** → 查看 `.clang-tidy` 或 `docs/QUICK_REFERENCE.md`
- **调整规则** → 编辑 `.clang-format` 或 `.clang-tidy`

---

## 🎓 学习路径

### Level 1: 新手（第1天）
- [ ] 阅读 `docs/CHECKLIST.md`
- [ ] 运行 `scripts/setup_code_style.sh`
- [ ] 在 VS Code 中测试基本功能

### Level 2: 熟练（第1周）
- [ ] 熟练使用快捷键（查看 `QUICK_REFERENCE.md`）
- [ ] 理解各种警告/错误含义
- [ ] 能够使用快速修复功能

### Level 3: 精通（第1个月）
- [ ] 能够自定义配置文件
- [ ] 理解各项检查规则的意义
- [ ] 能够帮助团队成员解决问题

---

## 📞 获取帮助

1. **查看文档**: 从 `CODE_STYLE_GUIDE.md` 开始
2. **运行脚本**: 使用 `--help` 参数查看帮助
3. **团队讨论**: 在群组中提问
4. **官方文档**: 
   - [clangd](https://clangd.llvm.org/)
   - [Google Style Guide](https://google.github.io/styleguide/cppguide.html)

---

**版本**: 1.0  
**更新**: 2026-02-20  
**维护**: 团队全员
