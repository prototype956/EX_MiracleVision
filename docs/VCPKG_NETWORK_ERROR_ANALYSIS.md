# vcpkg 网络问题分析和解决方案

## 🔍 错误分析

### 问题 1: GitHub 镜像配置失败 ❌

**现象**:
```
是否配置 GitHub 镜像？(y/n): y
配置 Git 使用 GitHub 镜像...
GitHub 镜像已配置
验证配置：
https://github.com
```

**问题**: 
- 镜像配置命令执行了，但**验证输出不正确**
- 正确的输出应该是: `https://ghproxy.com/https://github.com`
- 这说明 Git 镜像**没有真正生效**

---

### 问题 2: vcpkg 下载 patchelf 失败 ❌

**现象**:
```
Downloading https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz
error: curl: (35) error:0A000126:SSL routines::unexpected eof while reading
```

**原因**:
- vcpkg 尝试从 GitHub 下载 patchelf 工具
- Git 镜像没有生效，仍然直接访问 GitHub
- 网络连接超时或 SSL 错误

---

## ✅ 解决方案

### 方案 1: 手动配置 GitHub 镜像（正确方式）

之前的配置命令有问题，需要正确配置：

```bash
# 1. 检查当前配置
git config --global --list | grep insteadof

# 2. 如果没有输出或输出不正确，重新配置
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"

# 3. 验证配置（应该输出 https://github.com）
git config --global --get url."https://ghproxy.com/https://github.com".insteadOf

# 4. 查看完整配置
git config --global --get-regexp url
```

**正确的输出应该是**:
```
url.https://ghproxy.com/https://github.com.insteadof https://github.com
```

---

### 方案 2: 手动下载 patchelf 并放到 vcpkg 缓存

如果镜像仍然不工作，手动下载：

```bash
# 1. 创建下载目录
mkdir -p /home/prototype152/vcpkg/downloads

# 2. 使用镜像下载 patchelf
cd /home/prototype152/vcpkg/downloads
wget https://ghproxy.com/https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz \
    -O patchelf-0.15.5-x86_64.tar.gz

# 3. 验证下载
ls -lh patchelf-0.15.5-x86_64.tar.gz
```

---

### 方案 3: 配置 curl 使用代理（如果有代理）

如果你有代理服务器：

```bash
# 临时设置代理
export HTTP_PROXY=http://127.0.0.1:7890
export HTTPS_PROXY=http://127.0.0.1:7890
export ALL_PROXY=socks5://127.0.0.1:7891

# 或者配置到 ~/.bashrc
echo 'export HTTP_PROXY=http://127.0.0.1:7890' >> ~/.bashrc
echo 'export HTTPS_PROXY=http://127.0.0.1:7890' >> ~/.bashrc
source ~/.bashrc
```

---

## 🚀 推荐执行步骤

### Step 1: 修复 Git 镜像配置

```bash
# 1. 删除错误的配置
git config --global --unset-all url."https://ghproxy.com/https://github.com".insteadOf

# 2. 重新配置（正确方式）
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"

# 3. 验证（必须输出 https://github.com）
git config --global --get url."https://ghproxy.com/https://github.com".insteadOf
```

### Step 2: 手动下载 patchelf

```bash
cd /home/prototype152/vcpkg/downloads

# 使用 wget 并指定 User-Agent（避免被拦截）
wget --no-check-certificate \
    --user-agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36" \
    https://ghproxy.com/https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz \
    -O patchelf-0.15.5-x86_64.tar.gz

# 验证文件大小（应该约 300KB）
ls -lh patchelf-0.15.5-x86_64.tar.gz
```

### Step 3: 清理并重试

```bash
cd ~/桌面/EX_MiracleVision

# 清理失败的构建
rm -rf build
rm -rf /home/prototype152/vcpkg/buildtrees/fmt

# 重新配置
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

---

## 🔧 快速修复脚本

创建一个快速修复脚本：

```bash
cat > fix_vcpkg_network.sh << 'EOF'
#!/bin/bash

echo "=========================================="
echo "  vcpkg 网络问题快速修复"
echo "=========================================="
echo ""

# Step 1: 修复 Git 镜像配置
echo "Step 1: 配置 Git 镜像..."
git config --global --unset-all url."https://ghproxy.com/https://github.com".insteadOf 2>/dev/null || true
git config --global url."https://ghproxy.com/https://github.com".insteadOf "https://github.com"

# 验证
MIRROR=$(git config --global --get url."https://ghproxy.com/https://github.com".insteadOf)
if [ "$MIRROR" = "https://github.com" ]; then
    echo "✅ Git 镜像配置成功"
else
    echo "❌ Git 镜像配置失败，输出: $MIRROR"
    exit 1
fi

# Step 2: 手动下载 patchelf
echo ""
echo "Step 2: 下载 patchelf 工具..."
cd /home/prototype152/vcpkg/downloads

if [ -f "patchelf-0.15.5-x86_64.tar.gz" ]; then
    echo "✅ patchelf 已存在，跳过下载"
else
    wget --no-check-certificate \
        --user-agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36" \
        https://ghproxy.com/https://github.com/NixOS/patchelf/releases/download/0.15.5/patchelf-0.15.5-x86_64.tar.gz \
        -O patchelf-0.15.5-x86_64.tar.gz
    
    if [ $? -eq 0 ]; then
        echo "✅ patchelf 下载成功"
        ls -lh patchelf-0.15.5-x86_64.tar.gz
    else
        echo "❌ patchelf 下载失败"
        exit 1
    fi
fi

# Step 3: 清理失败的构建
echo ""
echo "Step 3: 清理失败的构建..."
cd ~/桌面/EX_MiracleVision
rm -rf build
rm -rf /home/prototype152/vcpkg/buildtrees/fmt

echo ""
echo "=========================================="
echo "  ✅ 修复完成！"
echo "=========================================="
echo ""
echo "现在可以重新运行构建："
echo "  cd ~/桌面/EX_MiracleVision"
echo "  cmake -B build -S . \\"
echo "    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \\"
echo "    -DCMAKE_BUILD_TYPE=Release \\"
echo "    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic"
echo ""
EOF

chmod +x fix_vcpkg_network.sh
./fix_vcpkg_network.sh
```

---

## 📊 问题根源总结

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| Git 镜像未生效 | 配置命令执行但验证错误 | 重新正确配置 |
| patchelf 下载失败 | 直接访问 GitHub 失败 | 手动下载到缓存 |
| vcpkg 依赖下载超时 | 网络不稳定 | 使用镜像或代理 |

---

## 🎯 下一步行动

1. **执行快速修复脚本** ⭐
2. **验证 Git 镜像配置正确**
3. **手动下载 patchelf 到 vcpkg 缓存**
4. **重新运行 cmake 配置**

准备好了吗？我可以帮你创建这个修复脚本！
