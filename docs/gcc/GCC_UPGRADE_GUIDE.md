# GCC 升级方案：从 11.4 到 13.x

**日期**: 2026年2月12日  
**系统**: Ubuntu 22.04.5 LTS (jammy)  
**当前版本**: GCC 11.4.0  
**目标版本**: GCC 13.x (推荐) 或 GCC 14.x

---

## 🎯 升级目标

### 为什么升级 GCC？

1. **解决 fmt 12.x 编译器内部错误** ✅
   - GCC 11.4 存在 bug，编译 fmt 12.1.0 时段错误
   - GCC 13+ 修复了此问题

2. **支持 C++20 完整特性** 🚀
   - GCC 11: C++20 部分支持
   - GCC 13: C++20 完整支持，更好的 concepts、ranges、coroutines
   - GCC 14: C++23 部分支持

3. **性能优化和标准库改进** ⚡
   - 更好的优化器
   - 更完善的标准库实现

---

## 📋 推荐版本选择

| GCC 版本 | C++20 支持 | C++23 支持 | 稳定性 | 推荐度 |
|----------|-----------|-----------|--------|--------|
| GCC 12.x | ✅ 完整 | ⚠️ 部分 | ⭐⭐⭐⭐ | 可选 |
| **GCC 13.x** | ✅ 完整 | ✅ 良好 | ⭐⭐⭐⭐⭐ | **推荐** |
| GCC 14.x | ✅ 完整 | ✅ 更好 | ⭐⭐⭐⭐ | 可选 |

**推荐：GCC 13.2** - 稳定且 C++20/23 支持最好

---

## 🔧 方案 1: 使用 Ubuntu 官方 PPA（推荐）⭐⭐⭐⭐⭐

### 优点
- 官方维护，稳定可靠
- 与系统集成良好
- 支持多版本并存
- 可以随时切换回 GCC 11

### 安装步骤

#### 1. 添加 Toolchain Test PPA
```bash
# 添加 PPA（包含 GCC 12, 13, 14）
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update
```

#### 2. 安装 GCC 13
```bash
# 安装 GCC 13 和 G++ 13
sudo apt-get install -y gcc-13 g++-13

# 验证安装
gcc-13 --version
g++-13 --version
```

#### 3. 配置默认编译器（方式 A：update-alternatives）
```bash
# 配置 gcc
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 130

# 配置 g++
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 110
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 130

# 配置 c++
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-11 110
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-13 130

# 配置 cc
sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-11 110
sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-13 130

# 选择默认版本（自动选择优先级最高的，即 GCC 13）
sudo update-alternatives --auto gcc
sudo update-alternatives --auto g++
sudo update-alternatives --auto c++
sudo update-alternatives --auto cc
```

#### 4. 验证切换
```bash
# 查看当前版本
gcc --version
g++ --version
c++ --version

# 应该显示 gcc (Ubuntu 13.x.x...) 13.x.x
```

#### 5. 切换版本（可选）
```bash
# 如果需要切换回 GCC 11
sudo update-alternatives --config gcc
sudo update-alternatives --config g++
sudo update-alternatives --config c++
sudo update-alternatives --config cc

# 或者临时使用特定版本
gcc-11 --version  # 使用 GCC 11
gcc-13 --version  # 使用 GCC 13
```

---

## 🔧 方案 2: 仅为项目指定 GCC 13（不改系统默认）

### 优点
- 不影响系统其他软件
- 更安全保守
- 可以随时切换

### 操作步骤

#### 1. 安装 GCC 13（同方案 1）
```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update
sudo apt-get install -y gcc-13 g++-13
```

#### 2. 在 CMake 中指定编译器
```bash
# 方式 A: 环境变量
export CC=/usr/bin/gcc-13
export CXX=/usr/bin/g++-13

cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic

# 方式 B: CMake 参数
cmake -B build -S . \
    -DCMAKE_C_COMPILER=/usr/bin/gcc-13 \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

#### 3. 创建构建脚本
```bash
cat > build_with_gcc13.sh << 'EOF'
#!/bin/bash

# 设置 GCC 13 编译器
export CC=/usr/bin/gcc-13
export CXX=/usr/bin/g++-13

# 清理旧构建
rm -rf build

# 配置项目
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic

# 编译
cmake --build build -j$(nproc)

echo "构建完成！"
EOF

chmod +x build_with_gcc13.sh
./build_with_gcc13.sh
```

---

## 🔧 方案 3: 安装 GCC 14（最新特性）

### 如果想体验 C++23 特性

```bash
# 添加 PPA
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update

# 安装 GCC 14
sudo apt-get install -y gcc-14 g++-14

# 配置为默认（可选）
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 140
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 140
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 140
sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-14 140
```

---

## 📝 完整升级流程（推荐执行）

### Step 1: 备份当前配置
```bash
# 记录当前 GCC 版本
gcc --version > ~/gcc_version_before.txt

# 备份项目构建
cd ~/桌面/EX_MiracleVision
tar -czf ~/miraclevision_backup_$(date +%Y%m%d).tar.gz .
```

### Step 2: 安装 GCC 13
```bash
# 添加 PPA
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update

# 安装
sudo apt-get install -y gcc-13 g++-13
```

### Step 3: 配置默认编译器
```bash
# 注册 GCC 11 和 13
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 130
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 110
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 130
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-11 110
sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-13 130
sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-11 110
sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-13 130

# 自动选择最高优先级
sudo update-alternatives --auto gcc
sudo update-alternatives --auto g++
sudo update-alternatives --auto c++
sudo update-alternatives --auto cc
```

### Step 4: 验证安装
```bash
# 检查版本
gcc --version
g++ --version
c++ --version

# 应该显示 GCC 13.x.x
```

### Step 5: 更新 vcpkg.json（恢复 fmt 最新版本）
```bash
cd ~/桌面/EX_MiracleVision
```

编辑 `vcpkg.json`，**移除 fmt 版本约束**：
```json
{
  "name": "ex-miraclevision",
  "version": "0.1.0",
  "dependencies": [
    "fmt",
    "spdlog",
    {
      "name": "opencv4",
      "features": [
        "ffmpeg",
        "dnn",
        "jpeg",
        "png",
        "contrib",
        "tbb",
        "eigen"
      ]
    },
    "eigen3",
    "yaml-cpp",
    "nlohmann-json",
    "tbb"
  ],
  "builtin-baseline": "aa2d37682e3318d93aef87efa7b0e88e81cd3d59"
}
```

**移除 overrides 部分**，让 vcpkg 使用最新的 fmt。

### Step 6: 更新项目使用 C++20
编辑 `cmake/CompilerOptions.cmake`：
```cmake
# 设置 C++ 标准为 C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ... 其他配置
```

### Step 7: 清理并重新构建
```bash
# 清理旧构建和 vcpkg 缓存
rm -rf build
rm -rf /home/prototype152/vcpkg/buildtrees/fmt
rm -rf /home/prototype152/vcpkg/packages/fmt*

# 重新配置（使用 GCC 13）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic

# 编译
cmake --build build -j$(nproc)
```

---

## ⚠️ 潜在问题和解决方案

### 问题 1: 网络下载仍然失败

**解决方案**: 配置 GitHub 镜像（独立于编译器版本）
```bash
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"
```

### 问题 2: vcpkg 检测不到新编译器

**解决方案**: 清理 vcpkg 编译器缓存
```bash
rm -rf /home/prototype152/vcpkg/buildtrees/detect_compiler
```

### 问题 3: 某些库仍然使用 GCC 11 编译

**解决方案**: 完全清理并重新安装
```bash
rm -rf /home/prototype152/vcpkg/buildtrees/*
rm -rf /home/prototype152/vcpkg/packages/*
rm -rf /home/prototype152/vcpkg/installed/*
```

---

## 🧪 验证 C++20 特性

升级完成后，测试 C++20 特性：

```cpp
// test_cpp20.cpp
#include <iostream>
#include <concepts>
#include <ranges>
#include <format>  // C++20 std::format (如果 GCC 支持)

// Concepts 示例
template<typename T>
concept Numeric = std::is_arithmetic_v<T>;

template<Numeric T>
T add(T a, T b) {
    return a + b;
}

int main() {
    // Ranges 示例
    std::vector<int> nums = {1, 2, 3, 4, 5};
    auto even = nums | std::views::filter([](int n) { return n % 2 == 0; });
    
    std::cout << "Even numbers: ";
    for (int n : even) {
        std::cout << n << " ";
    }
    std::cout << "\n";
    
    // Concepts
    std::cout << "Add: " << add(10, 20) << "\n";
    
    return 0;
}
```

编译测试：
```bash
g++ -std=c++20 test_cpp20.cpp -o test_cpp20
./test_cpp20
```

---

## 📊 升级影响评估

### ✅ 优点
- 解决 fmt 编译错误
- 完整 C++20 支持
- 更好的性能和优化
- 面向未来的技术栈

### ⚠️ 注意事项
- 可能需要重新编译所有依赖
- vcpkg 包需要重新下载编译（约 30-60 分钟）
- 确保系统有足够磁盘空间（至少 15GB）

### 🔄 回滚方案
如果需要回退到 GCC 11：
```bash
sudo update-alternatives --config gcc
sudo update-alternatives --config g++
# 选择 gcc-11 和 g++-11
```

---

## 🎯 我的推荐步骤（最佳实践）

### 阶段 1: 安装 GCC 13 并验证
```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt-get update
sudo apt-get install -y gcc-13 g++-13
gcc-13 --version
```

### 阶段 2: 先为项目单独使用 GCC 13（保守方案）
```bash
# 创建构建脚本
cat > build.sh << 'EOF'
#!/bin/bash
export CC=/usr/bin/gcc-13
export CXX=/usr/bin/g++-13
rm -rf build
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
cmake --build build -j$(nproc)
EOF
chmod +x build.sh
```

### 阶段 3: 配置 GitHub 镜像解决网络问题
```bash
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"
```

### 阶段 4: 执行构建
```bash
./build.sh
```

### 阶段 5: 如果成功，再设为系统默认（可选）
```bash
# 按照方案 1 的 Step 3 配置 update-alternatives
```

---

## 📞 需要帮助？

如果遇到问题，请提供：
1. 错误信息
2. `gcc --version` 输出
3. CMake 配置日志

---

准备好升级了吗？我可以协助你执行这些步骤！
