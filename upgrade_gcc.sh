#!/bin/bash

# GCC 13 升级和项目构建自动化脚本
# 用途：升级 GCC 11 到 GCC 13，并使用新编译器构建项目

set -e  # 遇到错误立即退出

echo "=========================================="
echo "  GCC 13 升级和项目构建脚本"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查是否为 root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${YELLOW}注意：某些步骤需要 sudo 权限${NC}"
fi

# Step 1: 检查当前 GCC 版本
echo ""
echo "=========================================="
echo "  Step 1: 检查当前环境"
echo "=========================================="
echo ""

echo "当前 GCC 版本："
gcc --version | head -1
echo ""

echo "系统信息："
lsb_release -d | awk -F: '{print $2}' | xargs
echo ""

# Step 2: 询问用户选择
echo "=========================================="
echo "  Step 2: 选择安装方式"
echo "=========================================="
echo ""
echo "请选择安装方式："
echo "  1) 安装 GCC 13 并设为系统默认（推荐）"
echo "  2) 仅安装 GCC 13，不改变系统默认（保守）"
echo "  3) 安装 GCC 14（最新特性，C++23）"
echo "  4) 退出"
echo ""
read -p "请输入选择 (1-4): " choice

case $choice in
    1)
        GCC_VERSION=13
        SET_DEFAULT=true
        ;;
    2)
        GCC_VERSION=13
        SET_DEFAULT=false
        ;;
    3)
        GCC_VERSION=14
        SET_DEFAULT=true
        ;;
    4)
        echo "退出脚本"
        exit 0
        ;;
    *)
        echo -e "${RED}无效选择，退出${NC}"
        exit 1
        ;;
esac

# Step 3: 安装 GCC
echo ""
echo "=========================================="
echo "  Step 3: 安装 GCC $GCC_VERSION"
echo "=========================================="
echo ""

# 检查是否已安装
if command -v gcc-$GCC_VERSION &> /dev/null; then
    echo -e "${GREEN}GCC $GCC_VERSION 已安装${NC}"
    gcc-$GCC_VERSION --version | head -1
else
    echo "添加 Ubuntu Toolchain PPA..."
    sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
    
    echo "更新包列表..."
    sudo apt-get update
    
    echo "安装 GCC $GCC_VERSION 和 G++ $GCC_VERSION..."
    sudo apt-get install -y gcc-$GCC_VERSION g++-$GCC_VERSION
    
    echo -e "${GREEN}GCC $GCC_VERSION 安装完成${NC}"
    gcc-$GCC_VERSION --version | head -1
fi

# Step 4: 配置默认编译器（可选）
if [ "$SET_DEFAULT" = true ]; then
    echo ""
    echo "=========================================="
    echo "  Step 4: 配置 GCC $GCC_VERSION 为默认编译器"
    echo "=========================================="
    echo ""
    
    # 注册 GCC 11
    if command -v gcc-11 &> /dev/null; then
        sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 110 || true
        sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 110 || true
        sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-11 110 || true
        sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-11 110 || true
    fi
    
    # 注册 GCC $GCC_VERSION（更高优先级）
    PRIORITY=$((GCC_VERSION * 10))
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-$GCC_VERSION $PRIORITY
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-$GCC_VERSION $PRIORITY
    sudo update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-$GCC_VERSION $PRIORITY
    sudo update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-$GCC_VERSION $PRIORITY
    
    # 自动选择最高优先级
    sudo update-alternatives --auto gcc
    sudo update-alternatives --auto g++
    sudo update-alternatives --auto c++
    sudo update-alternatives --auto cc
    
    echo -e "${GREEN}默认编译器已设置${NC}"
else
    echo ""
    echo -e "${YELLOW}未修改系统默认编译器${NC}"
fi

# Step 5: 验证安装
echo ""
echo "=========================================="
echo "  Step 5: 验证安装"
echo "=========================================="
echo ""

if [ "$SET_DEFAULT" = true ]; then
    echo "系统默认编译器："
    gcc --version | head -1
    g++ --version | head -1
else
    echo "GCC $GCC_VERSION 编译器："
    gcc-$GCC_VERSION --version | head -1
    g++-$GCC_VERSION --version | head -1
fi

# Step 6: 询问是否配置 GitHub 镜像
echo ""
echo "=========================================="
echo "  Step 6: GitHub 镜像配置（可选）"
echo "=========================================="
echo ""
echo "vcpkg 需要从 GitHub 下载依赖包。"
echo "如果网络不稳定，建议配置 GitHub 镜像。"
echo ""
read -p "是否配置 GitHub 镜像？(y/n): " configure_mirror

if [[ $configure_mirror =~ ^[Yy]$ ]]; then
    echo "配置 Git 使用 GitHub 镜像..."
    git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"
    echo -e "${GREEN}GitHub 镜像已配置${NC}"
    echo "验证配置："
    git config --global --get url.https://ghproxy.com/https://github.com.insteadof || echo "配置成功"
fi

# Step 7: 更新 vcpkg.json
echo ""
echo "=========================================="
echo "  Step 7: 更新项目配置"
echo "=========================================="
echo ""

echo "GCC $GCC_VERSION 可以编译最新版本的 fmt，无需版本约束。"
read -p "是否移除 vcpkg.json 中的 fmt 版本约束？(y/n): " remove_override

if [[ $remove_override =~ ^[Yy]$ ]]; then
    if [ -f "vcpkg.json" ]; then
        # 备份
        cp vcpkg.json vcpkg.json.backup
        echo "已备份 vcpkg.json -> vcpkg.json.backup"
        
        echo -e "${YELLOW}请手动编辑 vcpkg.json，移除 'overrides' 部分${NC}"
        echo "按 Enter 继续..."
        read
    else
        echo -e "${RED}错误：找不到 vcpkg.json${NC}"
    fi
fi

# Step 8: 更新 C++ 标准
echo ""
read -p "是否将 C++ 标准更新为 C++20？(y/n): " update_cpp_standard

if [[ $update_cpp_standard =~ ^[Yy]$ ]]; then
    if [ -f "cmake/CompilerOptions.cmake" ]; then
        cp cmake/CompilerOptions.cmake cmake/CompilerOptions.cmake.backup
        sed -i 's/set(CMAKE_CXX_STANDARD 17)/set(CMAKE_CXX_STANDARD 20)/' cmake/CompilerOptions.cmake
        echo -e "${GREEN}C++ 标准已更新为 C++20${NC}"
        echo "已备份 cmake/CompilerOptions.cmake"
    else
        echo -e "${YELLOW}未找到 cmake/CompilerOptions.cmake，请手动修改${NC}"
    fi
fi

# Step 9: 清理旧构建
echo ""
echo "=========================================="
echo "  Step 9: 清理旧构建文件"
echo "=========================================="
echo ""

read -p "是否清理 build 目录和 vcpkg 缓存？(y/n): " clean_build

if [[ $clean_build =~ ^[Yy]$ ]]; then
    echo "清理构建目录..."
    rm -rf build
    echo -e "${GREEN}build 目录已清理${NC}"
    
    if [ -d "/home/prototype152/vcpkg/buildtrees/fmt" ]; then
        echo "清理 vcpkg fmt 缓存..."
        rm -rf /home/prototype152/vcpkg/buildtrees/fmt
        rm -rf /home/prototype152/vcpkg/packages/fmt* 2>/dev/null || true
        echo -e "${GREEN}vcpkg fmt 缓存已清理${NC}"
    fi
fi

# Step 10: 构建项目
echo ""
echo "=========================================="
echo "  Step 10: 构建项目"
echo "=========================================="
echo ""

read -p "是否立即构建项目？(y/n): " build_now

if [[ $build_now =~ ^[Yy]$ ]]; then
    echo "准备构建配置..."
    
    # 设置编译器
    if [ "$SET_DEFAULT" = false ]; then
        export CC=/usr/bin/gcc-$GCC_VERSION
        export CXX=/usr/bin/g++-$GCC_VERSION
        echo "使用编译器: $CC, $CXX"
    fi
    
    # 配置项目
    echo ""
    echo "运行 CMake 配置..."
    cmake -B build -S . \
        -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}CMake 配置成功${NC}"
        echo ""
        echo "开始编译..."
        cmake --build build -j$(nproc)
        
        if [ $? -eq 0 ]; then
            echo ""
            echo -e "${GREEN}=========================================="
            echo "  ✅ 构建成功！"
            echo "==========================================${NC}"
            echo ""
            echo "可执行文件位置："
            find build -type f -executable -name "MiracleVision" 2>/dev/null || echo "build/bin/MiracleVision"
        else
            echo ""
            echo -e "${RED}=========================================="
            echo "  ❌ 构建失败"
            echo "==========================================${NC}"
            echo ""
            echo "请检查错误信息"
        fi
    else
        echo -e "${RED}CMake 配置失败${NC}"
    fi
else
    echo ""
    echo -e "${YELLOW}跳过构建步骤${NC}"
    echo ""
    echo "稍后可以手动运行："
    if [ "$SET_DEFAULT" = false ]; then
        echo "  export CC=/usr/bin/gcc-$GCC_VERSION"
        echo "  export CXX=/usr/bin/g++-$GCC_VERSION"
    fi
    echo "  cmake -B build -S . \\"
    echo "    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \\"
    echo "    -DCMAKE_BUILD_TYPE=Release \\"
    echo "    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic"
    echo "  cmake --build build -j\$(nproc)"
fi

# 完成
echo ""
echo "=========================================="
echo "  🎉 升级完成！"
echo "=========================================="
echo ""
echo "当前编译器版本："
if [ "$SET_DEFAULT" = true ]; then
    gcc --version | head -1
else
    gcc-$GCC_VERSION --version | head -1
fi
echo ""
echo "如需切换编译器版本："
echo "  sudo update-alternatives --config gcc"
echo "  sudo update-alternatives --config g++"
echo ""
echo "详细文档: docs/GCC_UPGRADE_GUIDE.md"
