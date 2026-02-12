#!/bin/bash

# vcpkg 网络问题快速修复脚本

set -e

echo "=========================================="
echo "  vcpkg 网络问题快速修复"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Step 1: 修复 Git 镜像配置
echo "Step 1: 配置 Git 镜像..."
git config --global --unset-all url."https://ghproxy.com/https://github.com".insteadOf 2>/dev/null || true
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"

# 验证
MIRROR=$(git config --global --get url."https://ghproxy.com/https://github.com".insteadOf 2>/dev/null || echo "")
if [ "$MIRROR" = "https://github.com" ]; then
    echo -e "${GREEN}✅ Git 镜像配置成功${NC}"
    echo "   配置: url.https://ghproxy.com/https://github.com.insteadOf = https://github.com"
else
    echo -e "${RED}❌ Git 镜像配置失败${NC}"
    echo "   输出: $MIRROR"
    echo ""
    echo "请手动执行："
    echo "  git config --global url.\"https://ghproxy.com/https://github.com\".insteadOf \"https://github.com\""
    exit 1
fi

# Step 2: 手动下载 patchelf
echo ""
echo "Step 2: 下载 patchelf 工具..."

# 确保下载目录存在
mkdir -p /home/prototype152/vcpkg/downloads
cd /home/prototype152/vcpkg/downloads

if [ -f "patchelf-0.15.5-x86_64.tar.gz" ]; then
    FILE_SIZE=$(stat -f%z "patchelf-0.15.5-x86_64.tar.gz" 2>/dev/null || stat -c%s "patchelf-0.15.5-x86_64.tar.gz" 2>/dev/null)
    if [ "$FILE_SIZE" -gt 100000 ]; then
        echo -e "${GREEN}✅ patchelf 已存在且有效，跳过下载${NC}"
        ls -lh patchelf-0.15.5-x86_64.tar.gz
    else
        echo -e "${YELLOW}⚠️  patchelf 文件损坏，重新下载${NC}"
        rm -f patchelf-0.15.5-x86_64.tar.gz
    fi
fi

if [ ! -f "patchelf-0.15.5-x86_64.tar.gz" ]; then
    echo "正在下载 patchelf（使用 GitHub 镜像）..."
    
    # 尝试多个镜像
    MIRRORS=(
        "https://ghproxy.com/https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz"
        "https://mirror.ghproxy.com/https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz"
        "https://gh-proxy.com/https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz"
    )
    
    SUCCESS=false
    for MIRROR_URL in "${MIRRORS[@]}"; do
        echo "尝试镜像: $MIRROR_URL"
        
        if wget --no-check-certificate \
            --timeout=30 \
            --tries=3 \
            --user-agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36" \
            "$MIRROR_URL" \
            -O patchelf-0.15.5-x86_64.tar.gz 2>&1 | grep -E "saved|100%"; then
            
            FILE_SIZE=$(stat -f%z "patchelf-0.15.5-x86_64.tar.gz" 2>/dev/null || stat -c%s "patchelf-0.15.5-x86_64.tar.gz" 2>/dev/null)
            if [ "$FILE_SIZE" -gt 100000 ]; then
                echo -e "${GREEN}✅ patchelf 下载成功${NC}"
                ls -lh patchelf-0.15.5-x86_64.tar.gz
                SUCCESS=true
                break
            else
                echo -e "${YELLOW}⚠️  文件太小，可能下载失败${NC}"
                rm -f patchelf-0.15.5-x86_64.tar.gz
            fi
        else
            echo -e "${YELLOW}⚠️  此镜像失败，尝试下一个${NC}"
            rm -f patchelf-0.15.5-x86_64.tar.gz
        fi
    done
    
    if [ "$SUCCESS" = false ]; then
        echo -e "${RED}❌ 所有镜像都失败了${NC}"
        echo ""
        echo "请手动下载:"
        echo "1. 访问: https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz"
        echo "2. 保存到: /home/prototype152/vcpkg/downloads/patchelf-0.15.5-x86_64.tar.gz"
        exit 1
    fi
fi

# Step 3: 下载 fmt 源码（预先下载）
echo ""
echo "Step 3: 预先下载 fmt 源码..."
cd /home/prototype152/vcpkg/downloads

if [ -f "fmtlib-fmt-12.1.0.tar.gz" ]; then
    echo -e "${GREEN}✅ fmt 源码已存在，跳过下载${NC}"
else
    echo "正在下载 fmt 12.1.0..."
    
    MIRRORS=(
        "https://ghproxy.com/https://github.com/fmtlib/fmt/archive/12.1.0.tar.gz"
        "https://mirror.ghproxy.com/https://github.com/fmtlib/fmt/archive/12.1.0.tar.gz"
    )
    
    SUCCESS=false
    for MIRROR_URL in "${MIRRORS[@]}"; do
        echo "尝试镜像: $MIRROR_URL"
        
        if wget --no-check-certificate \
            --timeout=30 \
            --tries=3 \
            --user-agent="Mozilla/5.0" \
            "$MIRROR_URL" \
            -O fmtlib-fmt-12.1.0.tar.gz 2>&1 | grep -E "saved|100%"; then
            
            echo -e "${GREEN}✅ fmt 源码下载成功${NC}"
            ls -lh fmtlib-fmt-12.1.0.tar.gz
            SUCCESS=true
            break
        else
            echo -e "${YELLOW}⚠️  此镜像失败，尝试下一个${NC}"
            rm -f fmtlib-fmt-12.1.0.tar.gz
        fi
    done
    
    if [ "$SUCCESS" = false ]; then
        echo -e "${YELLOW}⚠️  fmt 预下载失败，但不影响继续（vcpkg 会重试）${NC}"
    fi
fi

# Step 4: 清理失败的构建
echo ""
echo "Step 4: 清理失败的构建..."
cd ~/桌面/EX_MiracleVision

if [ -d "build" ]; then
    rm -rf build
    echo -e "${GREEN}✅ build 目录已清理${NC}"
fi

if [ -d "/home/prototype152/vcpkg/buildtrees/fmt" ]; then
    rm -rf /home/prototype152/vcpkg/buildtrees/fmt
    echo -e "${GREEN}✅ vcpkg fmt 缓存已清理${NC}"
fi

# Step 5: 测试 Git 镜像是否工作
echo ""
echo "Step 5: 测试 Git 镜像..."
TEST_OUTPUT=$(git ls-remote https://github.com/microsoft/vcpkg.git HEAD 2>&1 | head -1)
if echo "$TEST_OUTPUT" | grep -q "^[0-9a-f]\{40\}"; then
    echo -e "${GREEN}✅ Git 镜像工作正常${NC}"
else
    echo -e "${YELLOW}⚠️  Git 镜像可能不工作，但已配置${NC}"
fi

echo ""
echo "=========================================="
echo -e "  ${GREEN}✅ 修复完成！${NC}"
echo "=========================================="
echo ""
echo "现在可以重新运行构建："
echo ""
echo -e "${YELLOW}cd ~/桌面/EX_MiracleVision${NC}"
echo -e "${YELLOW}cmake -B build -S . \\${NC}"
echo -e "${YELLOW}  -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \\${NC}"
echo -e "${YELLOW}  -DCMAKE_BUILD_TYPE=Release \\${NC}"
echo -e "${YELLOW}  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic${NC}"
echo ""
echo "或者直接运行:"
echo -e "${YELLOW}./build_project.sh${NC}"
echo ""
