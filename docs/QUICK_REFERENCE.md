# 🎯 快速参考卡片

> 打印此页面并贴在显示器旁，方便日常查阅！

---

## ⚡ 快捷键

| 操作 | 快捷键 | 说明 |
|------|--------|------|
| **代码补全** | `Ctrl+Space` | 手动触发补全 |
| **跳转定义** | `F12` | 跳转到定义 |
| **查找引用** | `Shift+F12` | 查找所有使用位置 |
| **返回** | `Alt+Left` | 返回上一位置 |
| **格式化文件** | `Shift+Alt+F` | 格式化当前文件 |
| **格式化选区** | `Ctrl+K Ctrl+F` | 格式化选中代码 |
| **快速修复** | `Ctrl+.` | 显示修复建议 |
| **问题面板** | `Ctrl+Shift+M` | 查看所有问题 |
| **命令面板** | `Ctrl+Shift+P` | VS Code 命令 |

---

## 🔧 常用命令

```bash
# 设置环境（首次）
./scripts/setup_code_style.sh

# 格式化代码
./scripts/format_code.sh              # 整个项目
./scripts/format_code.sh --dry-run    # 仅检查
./scripts/format_code.sh --dir module # 指定目录

# 静态分析
./scripts/check_code.sh                        # 整个项目
./scripts/check_code.sh --file xxx.cpp         # 单个文件
./scripts/check_code.sh --fix --file xxx.cpp   # 自动修复

# 重新生成编译数据库
cd build && cmake .. && cd ..
```

---

## 📋 提交前检查清单

- [ ] `Shift+Alt+F` 格式化代码
- [ ] `Ctrl+Shift+M` 检查无错误
- [ ] 查看修改的文件，确认变更合理
- [ ] Git commit message 清晰

---

## 🎨 命名规范速查

```cpp
// 类名: CamelCase
class ArmorDetector {};

// 函数名: CamelCase
void DetectArmor() {}

// 变量名: snake_case
int armor_count = 0;

// 常量: UPPER_CASE
const int MAX_ARMOR = 10;

// 私有成员: 结尾下划线
class MyClass {
 private:
  int value_;  // 私有成员
  void Helper_();  // 私有方法
};

// 函数参数: snake_case
void Process(int input_value, bool enable_debug) {}
```

---

## ⚠️ 常见问题

| 问题 | 快速解决 |
|------|----------|
| 无代码补全 | `Ctrl+Shift+P` → "clangd: Restart" |
| 找不到头文件 | `cd build && cmake ..` |
| 格式化无效 | 检查右下角格式化器是否为 clangd |
| 性能问题 | 关闭不需要的文件/窗口 |

---

## 🚗 实车 3 分钟调参 SOP（二阶段）

适用场景：
传统视觉出现梯形误框、tracker 周期性 lost、binary/diff 闪烁明显。

关键面板：
1. `vision/debug/diff`
2. `vision/debug/binary`
3. `vision/debug/roi`
4. `vision/debug/roi_status`（Raw Message）
5. `tracking/lost_stats`（Raw Message）

关键参数：
1. `debug.foxglove.stabilize_diff_binary`（建议保持 `true`）
2. `armor.min_tb_span_ratio`（默认 `0.65`）
3. `pnp.max_reproj_error_px`（默认 `12.0`）

步骤：
1. 先确认 `debug.foxglove.stabilize_diff_binary=true`，让 binary/diff 先稳定。
2. 观察 `vision/debug/roi_status`：如果 `lost_count` 长时间递增，先回查曝光和阈值。
3. 调 `armor.min_tb_span_ratio`：
   `0.55 -> 0.65 -> 0.75`，逐步提高，目标是减少梯形误配但不明显漏检。
4. 调 `pnp.max_reproj_error_px`：
   `16 -> 12 -> 10`，逐步收紧，观察 `tracking/lost_stats` 中
   `rejected_by_reproj_total` 与 `reason_counts.nis_diverged` 的平衡。
5. 以 30 秒窗口观察：
   `lost_total_events` 下降且 `rejected_by_reproj_this_frame` 不长期过高，即为可用配置。

判定经验：
1. `rejected_by_reproj_total` 增长很快且目标经常断：阈值过严，放宽到 `12~16`。
2. `nis_diverged`/`temp_lost_timeout` 持续上升：阈值可适度收紧到 `8~12`。
3. 梯形误框多但 `rejected_by_reproj` 低：优先提高 `armor.min_tb_span_ratio`。

---

## 🔗 快速链接

- 详细文档: `docs/CODE_STYLE_GUIDE.md`
- 团队清单: `docs/CHECKLIST.md`
- 实施总结: `docs/IMPLEMENTATION_SUMMARY.md`
- Google Style: https://google.github.io/styleguide/cppguide.html

---

## 💡 专业提示

1. **保存时自动格式化**（已配置）
   - 每次保存文件都会自动格式化
   - 无需手动操作

2. **内联提示**
   - 悬停在代码上查看类型信息
   - 查看函数参数提示

3. **快速修复**
   - 看到💡灯泡图标时点击
   - 或按 `Ctrl+.` 查看建议

4. **增量索引**
   - clangd 会后台索引代码
   - 首次打开项目需要等待

5. **问题过滤**
   - 在问题面板可按文件筛选
   - 关注红色错误，黄色警告可选择性处理

---

**版本**: 1.0 | **更新**: 2026-02-20
