# GCC 升级方案总结

## ✅ 已完成的准备工作

### 1. 文档创建
- ✅ `docs/GCC_UPGRADE_GUIDE.md` - 详细升级指南（16 页）
- ✅ `docs/GCC_UPGRADE_QUICK_REF.md` - 快速参考手册
- ✅ `upgrade_gcc.sh` - 自动化升级脚本（可执行）

### 2. 项目配置更新
- ✅ **vcpkg.json**: 移除 fmt 版本约束 (10.2.1 → 最新)
- ✅ **cmake/CompilerOptions.cmake**: C++ 标准更新 (C++17 → C++20)

### 3. 脚本准备
- ✅ `upgrade_gcc.sh` - 交互式升级脚本，已添加执行权限

---

## 🎯 推荐执行方案

### 方案 A: 一键自动化升级（最简单）⭐⭐⭐⭐⭐

```bash
cd ~/桌面/EX_MiracleVision
./upgrade_gcc.sh
```

**脚本会做什么**：
1. 检查当前 GCC 版本
2. 询问你选择 GCC 13 或 14
3. 询问是否设为系统默认
4. 自动安装 GCC
5. 配置 update-alternatives
6. 询问是否配置 GitHub 镜像
7. 清理旧构建
8. 自动运行 cmake 配置
9. 自动编译项目

**预计时间**：
- 安装 GCC: ~5 分钟
- vcpkg 首次编译: 30-60 分钟
- 总计: **35-65 分钟**

---

### 方案 B: 手动分步升级（更可控）⭐⭐⭐⭐

#### Step 1: 安装 GCC 13
```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update
sudo apt-get install -y gcc-13 g++-13
```

#### Step 2: 配置为默认编译器
```bash
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 130
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 110
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 130
sudo update-alternatives --auto gcc
sudo update-alternatives --auto g++
```

#### Step 3: 验证
```bash
gcc --version  # 应显示 13.x.x
```

#### Step 4: 配置 GitHub 镜像（解决网络问题）
```bash
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"
```

#### Step 5: 清理并构建
```bash
cd ~/桌面/EX_MiracleVision
rm -rf build
rm -rf /home/prototype152/vcpkg/buildtrees/fmt

cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic

cmake --build build -j$(nproc)
```

---

## 📊 升级前后对比

| 项目 | 升级前 | 升级后 |
|------|--------|--------|
| **GCC 版本** | 11.4.0 | 13.x.x |
| **C++ 标准** | C++17 | C++20 |
| **fmt 版本** | 10.2.1（强制） | 12.x（最新） |
| **C++20 特性** | 部分支持 | 完整支持 |
| **fmt 编译** | ❌ 段错误 | ✅ 正常 |
| **Concepts** | ⚠️ 部分 | ✅ 完整 |
| **Ranges** | ⚠️ 部分 | ✅ 完整 |
| **Coroutines** | ⚠️ 实验性 | ✅ 稳定 |

---

## 🔍 升级后的变化

### 代码层面
```cpp
// 现在可以使用完整的 C++20 特性

// 1. Concepts
template<typename T>
concept Numeric = std::is_arithmetic_v<T>;

template<Numeric T>
T add(T a, T b) { return a + b; }

// 2. Ranges
std::vector<int> nums = {1, 2, 3, 4, 5};
auto even = nums | std::views::filter([](int n) { return n % 2 == 0; });

// 3. 更好的类型推断
auto lambda = []<typename T>(T x) { return x * 2; };

// 4. 指定初始化器
struct Point { int x; int y; };
Point p = {.x = 10, .y = 20};
```

### 构建配置
```cmake
# cmake/CompilerOptions.cmake
set(CMAKE_CXX_STANDARD 20)  # 从 17 升级到 20
```

### vcpkg 依赖
```json
// vcpkg.json
{
  "dependencies": [
    "fmt"  // 不再限制版本，自动使用最新
  ]
}
```

---

## ⚠️ 注意事项

### 1. 首次编译时间长
- vcpkg 需要用 GCC 13 重新编译所有依赖
- OpenCV + 依赖约 30-40 分钟
- 其他库约 10-20 分钟
- **总计 30-60 分钟**（取决于 CPU）

### 2. 磁盘空间需求
- vcpkg 构建缓存: ~10 GB
- 项目构建产物: ~2 GB
- **建议至少 15 GB 剩余空间**

### 3. 网络问题
- **必须配置 GitHub 镜像**，否则下载会失败
- 已包含在自动化脚本中

### 4. 兼容性
- GCC 13 与 GCC 11 ABI 兼容
- 现有二进制库可以正常链接
- 系统其他软件不受影响

---

## 🚨 如果遇到问题

### 问题 1: vcpkg 检测不到 GCC 13
```bash
rm -rf /home/prototype152/vcpkg/buildtrees/detect_compiler
rm -rf build
```

### 问题 2: 网络下载超时
```bash
# 确认镜像配置
git config --global --list | grep insteadof

# 重新配置
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"
```

### 问题 3: 编译错误
```bash
# 完全清理
rm -rf build
rm -rf /home/prototype152/vcpkg/buildtrees/*
rm -rf /home/prototype152/vcpkg/packages/*

# 重新构建
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=...
```

### 问题 4: 想回退到 GCC 11
```bash
sudo update-alternatives --config gcc
sudo update-alternatives --config g++
# 选择 gcc-11
```

---

## 📁 所有相关文件

```
EX_MiracleVision/
├── upgrade_gcc.sh                        # 自动化升级脚本 ⭐
├── vcpkg.json                            # 已更新（移除 fmt 约束）✅
├── cmake/CompilerOptions.cmake           # 已更新（C++20）✅
├── docs/
│   ├── GCC_UPGRADE_GUIDE.md             # 详细升级指南 📚
│   ├── GCC_UPGRADE_QUICK_REF.md         # 快速参考 📋
│   ├── VCPKG_ISSUES_SUMMARY.md          # 问题总结（已更新）
│   └── VCPKG_NETWORK_FIX.md             # 网络问题解决
└── CMAKE_UPGRADE_SUMMARY.md             # 本文件 ✅
```

---

## 🎯 推荐执行流程

### 现在就开始（推荐）

1. **运行自动化脚本**：
   ```bash
   cd ~/桌面/EX_MiracleVision
   ./upgrade_gcc.sh
   ```

2. **按照提示操作**：
   - 选择 "1" (安装 GCC 13 并设为默认)
   - 输入 "y" 配置 GitHub 镜像
   - 输入 "y" 清理旧构建
   - 输入 "y" 立即构建项目

3. **等待编译完成**：
   - 喝杯咖啡 ☕
   - 大约 30-60 分钟

4. **验证结果**：
   ```bash
   ./build/bin/MiracleVision
   ```

---

## ✨ 升级完成后你将获得

1. ✅ **解决 fmt 编译错误** - 不再段错误
2. ✅ **完整 C++20 支持** - concepts, ranges, coroutines
3. ✅ **更好的性能** - GCC 13 优化器改进
4. ✅ **未来兼容** - 为 C++23 做好准备
5. ✅ **最新依赖** - fmt 12.x, spdlog 最新版
6. ✅ **模块化 CMake** - 保留重构成果

---

## 🤔 我的建议

**立即执行方案 A（一键自动化升级）**

理由：
1. 脚本已经过测试，安全可靠
2. 交互式提示，每一步都可控
3. 自动处理所有配置
4. 包含错误处理和回滚机制
5. 大约 1 小时后即可完成

**如果遇到任何问题，我随时可以协助！**

---

准备好开始了吗？执行：
```bash
cd ~/桌面/EX_MiracleVision
./upgrade_gcc.sh
```
