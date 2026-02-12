# GCC 升级快速参考

## 🚀 一键升级（推荐）

```bash
cd ~/桌面/EX_MiracleVision
./upgrade_gcc.sh
```

这个脚本会：
1. ✅ 安装 GCC 13.x
2. ✅ 配置为默认编译器（可选）
3. ✅ 配置 GitHub 镜像（解决网络问题）
4. ✅ 更新项目配置（C++20）
5. ✅ 自动构建项目

---

## 📋 手动升级步骤

### 1. 安装 GCC 13
```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update
sudo apt-get install -y gcc-13 g++-13
```

### 2. 设为默认编译器
```bash
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 130
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 130
sudo update-alternatives --auto gcc
sudo update-alternatives --auto g++
```

### 3. 验证
```bash
gcc --version  # 应显示 13.x.x
```

### 4. 配置 GitHub 镜像
```bash
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"
```

### 5. 构建项目
```bash
rm -rf build
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
cmake --build build -j$(nproc)
```

---

## 🔄 编译器版本切换

### 列出可用版本
```bash
update-alternatives --list gcc
update-alternatives --list g++
```

### 交互式选择
```bash
sudo update-alternatives --config gcc
sudo update-alternatives --config g++
```

### 临时使用特定版本
```bash
# 使用 GCC 11
CC=gcc-11 CXX=g++-11 cmake -B build -S .

# 使用 GCC 13
CC=gcc-13 CXX=g++-13 cmake -B build -S .
```

---

## ✅ 已完成的更新

### 1. vcpkg.json
**移除了 fmt 版本约束**，现在使用最新版本：
```json
{
  "dependencies": [
    "fmt",
    ...
  ]
}
```

### 2. cmake/CompilerOptions.cmake
**C++ 标准更新为 C++20**：
```cmake
set(CMAKE_CXX_STANDARD 20)
```

---

## 🧪 测试 C++20 特性

```bash
# 创建测试文件
cat > test_cpp20.cpp << 'EOF'
#include <iostream>
#include <vector>
#include <ranges>

int main() {
    std::vector<int> nums = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    // C++20 ranges
    auto even = nums | std::views::filter([](int n) { return n % 2 == 0; });
    
    std::cout << "Even numbers: ";
    for (int n : even) {
        std::cout << n << " ";
    }
    std::cout << "\n";
    
    return 0;
}
EOF

# 编译测试
g++ -std=c++20 test_cpp20.cpp -o test_cpp20
./test_cpp20

# 预期输出: Even numbers: 2 4 6 8 10
```

---

## ⚠️ 常见问题

### Q1: vcpkg 检测不到新编译器？
```bash
rm -rf /home/prototype152/vcpkg/buildtrees/detect_compiler
rm -rf build
```

### Q2: 某些库仍用旧编译器编译？
```bash
# 清理所有 vcpkg 缓存
rm -rf /home/prototype152/vcpkg/buildtrees/*
rm -rf /home/prototype152/vcpkg/packages/*
rm -rf build
```

### Q3: 网络下载仍然失败？
```bash
# 确认 Git 镜像配置
git config --global --get url.https://ghproxy.com/https://github.com.insteadof

# 如果没有输出，重新配置
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"
```

### Q4: 想回退到 GCC 11？
```bash
sudo update-alternatives --config gcc
sudo update-alternatives --config g++
# 选择 gcc-11 和 g++-11
```

---

## 📊 GCC 版本对比

| 版本 | C++17 | C++20 | C++23 | fmt 12.x | 推荐度 |
|------|-------|-------|-------|----------|--------|
| GCC 11.4 | ✅ | ⚠️ 部分 | ❌ | ❌ 崩溃 | ⭐⭐ |
| GCC 12.x | ✅ | ✅ 完整 | ⚠️ 部分 | ✅ | ⭐⭐⭐⭐ |
| **GCC 13.x** | ✅ | ✅ 完整 | ✅ 良好 | ✅ | ⭐⭐⭐⭐⭐ |
| GCC 14.x | ✅ | ✅ 完整 | ✅ 更好 | ✅ | ⭐⭐⭐⭐ |

---

## 📁 相关文档

- **详细指南**: `docs/GCC_UPGRADE_GUIDE.md`
- **问题总结**: `docs/VCPKG_ISSUES_SUMMARY.md`
- **网络问题**: `docs/VCPKG_NETWORK_FIX.md`
- **升级脚本**: `upgrade_gcc.sh`

---

## 🎯 下一步

1. **执行升级脚本**: `./upgrade_gcc.sh`
2. **等待 vcpkg 编译**: 首次约 30-60 分钟
3. **测试构建**: `cmake --build build -j$(nproc)`
4. **运行程序**: `./build/bin/MiracleVision`

---

**升级完成后，你将获得**:
- ✅ 解决 fmt 编译错误
- ✅ 完整 C++20 支持
- ✅ 更好的编译器优化
- ✅ 为 C++23 做好准备
