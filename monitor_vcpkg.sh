#!/bin/bash

# vcpkg 编译进度监控脚本

echo "=========================================="
echo "  vcpkg 编译进度监控"
echo "=========================================="
echo ""

# 检查 vcpkg 进程
if pgrep -f "vcpkg install" > /dev/null; then
    echo "✓ vcpkg 正在运行"
else
    echo "✗ vcpkg 未运行"
fi

echo ""
echo "=========================================="
echo "  已安装的包"
echo "=========================================="

# 统计已安装的包
if [ -d "build/vcpkg_installed/x64-linux-dynamic/lib" ]; then
    installed=$(ls -1 build/vcpkg_installed/x64-linux-dynamic/lib/*.so 2>/dev/null | wc -l)
    echo "动态库数量: $installed"
    echo ""
    echo "已安装的库："
    ls -1 build/vcpkg_installed/x64-linux-dynamic/lib/*.so 2>/dev/null | head -20 | sed 's/.*\//  - /'
else
    echo "安装目录还未创建"
fi

echo ""
echo "=========================================="
echo "  正在编译的包"
echo "=========================================="

# 检查正在编译的包
if [ -d "/home/prototype152/vcpkg/buildtrees" ]; then
    building=$(find /home/prototype152/vcpkg/buildtrees -maxdepth 1 -type d -mmin -5 | wc -l)
    echo "最近 5 分钟修改的构建目录: $building"
    echo ""
    find /home/prototype152/vcpkg/buildtrees -maxdepth 1 -type d -mmin -5 | tail -10 | sed 's/.*\//  - /'
fi

echo ""
echo "=========================================="
echo "  磁盘使用"
echo "=========================================="

if [ -d "build" ]; then
    build_size=$(du -sh build 2>/dev/null | awk '{print $1}')
    echo "build 目录: $build_size"
fi

vcpkg_size=$(du -sh /home/prototype152/vcpkg/buildtrees 2>/dev/null | awk '{print $1}')
echo "vcpkg buildtrees: $vcpkg_size"

echo ""
echo "=========================================="
echo "  内存使用"
echo "=========================================="
free -h | grep -E "内存|交换"

echo ""
echo "=========================================="
echo "  CPU 负载"
echo "=========================================="
uptime

echo ""
echo "=========================================="
echo "提示：运行 'watch -n 5 ./monitor_vcpkg.sh' 实时监控"
echo "=========================================="
