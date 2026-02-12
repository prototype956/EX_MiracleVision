# vcpkg 配置问题解决方案

## 问题描述

### 错误信息
```
opencv4[tbb] is only supported on '!static', which does not match x64-linux
```

## 根本原因

**vcpkg 默认使用静态链接**，但 OpenCV 的 TBB 特性不支持静态链接，必须使用动态链接。

---

## 解决方案

### 1. 更新 vcpkg.json

添加 `builtin-baseline` 字段：

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

**说明**: `builtin-baseline` 是 vcpkg 版本控制的 commit hash。

### 2. 使用动态链接 triplet

在 CMake 配置时添加参数：

```bash
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic
```

**重要**: 必须使用 `x64-linux-dynamic` 而不是默认的 `x64-linux`。

---

## 完整构建命令

```bash
# 1. 清理之前的构建
rm -rf build

# 2. 配置项目（使用动态链接）
cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/home/prototype152/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic

# 3. 编译（首次会很慢，vcpkg 需要编译所有依赖）
cmake --build build -j$(nproc)

# 4. 运行
./build/bin/MiracleVision
```

---

## 首次编译注意事项

### ⏰ 时间估计

vcpkg 首次编译所有依赖库大约需要：
- **OpenCV + 依赖**: 20-40 分钟
- **其他库**: 5-10 分钟
- **总计**: 30-60 分钟（取决于 CPU 性能）

### 📦 编译的主要包

vcpkg 会编译以下包（及其依赖）：
- OpenCV 4.x（包含 ffmpeg, dnn, contrib, tbb, eigen）
- fmt, spdlog, eigen3, yaml-cpp, nlohmann-json, tbb
- GTK3, Cairo, Pango 等 GUI 库
- FFmpeg 及其依赖

### 💾 磁盘空间

vcpkg 缓存和构建产物大约需要：
- **下载缓存**: ~2 GB
- **构建目录**: ~5-10 GB
- **安装目录**: ~3-5 GB
- **建议剩余空间**: 至少 15 GB

---

## 验证配置

配置成功后，你应该看到：

```
-- Running vcpkg install
Detecting compiler hash for triplet x64-linux-dynamic...
The following packages will be built and installed:
  * opencv4:x64-linux-dynamic@4.x.x
  * fmt:x64-linux-dynamic@x.x.x
  ...
```

---

## 常见问题

### Q1: 编译太慢怎么办？

**A**: 可以使用 vcpkg binary cache 或预编译包：

```bash
# 设置 binary cache
export VCPKG_BINARY_SOURCES="clear;files,$HOME/.vcpkg/archives,readwrite"
```

### Q2: 磁盘空间不足？

**A**: 清理 vcpkg 缓存：

```bash
cd /home/prototype152/vcpkg
./vcpkg remove --outdated
./vcpkg clean
```

### Q3: 某个包编译失败？

**A**: 查看详细日志：

```bash
cat build/vcpkg-manifest-install.log
```

### Q4: 想使用系统库而不是 vcpkg？

**A**: 修改 `cmake/Dependencies.cmake`，改为使用 `find_package()` 而不是 vcpkg。

---

## 替代方案：不使用 vcpkg

如果不想等待 vcpkg 编译，可以使用系统包：

```bash
# 安装系统依赖
sudo apt-get install -y \
    libopencv-dev \
    libfmt-dev \
    libspdlog-dev \
    libeigen3-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libtbb-dev

# 配置（不使用 vcpkg）
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build -j$(nproc)
```

**注意**: 系统 OpenCV 版本可能较旧，某些特性可能不可用。

---

## triplet 说明

### 静态链接 vs 动态链接

| Triplet | 链接方式 | 优点 | 缺点 |
|---------|---------|------|------|
| `x64-linux` | 静态 | 独立可执行文件 | OpenCV TBB 不支持 |
| `x64-linux-dynamic` | 动态 | 支持所有特性 | 需要 .so 文件 |

### 为什么 OpenCV TBB 不支持静态链接？

TBB (Threading Building Blocks) 需要在运行时动态加载线程库，静态链接会导致运行时问题。

---

## 更新 builtin-baseline

如果 vcpkg 版本更新，需要更新 baseline：

```bash
# 查看当前 vcpkg commit
cd /home/prototype152/vcpkg
git rev-parse HEAD

# 更新 vcpkg.json
# "builtin-baseline": "<新的 commit hash>"
```

---

## 总结

✅ **已解决的问题**:
- OpenCV TBB 静态链接冲突
- vcpkg baseline 配置
- 动态链接 triplet 配置

✅ **当前配置**:
- vcpkg.json: 添加 builtin-baseline
- CMake 参数: `-DVCPKG_TARGET_TRIPLET=x64-linux-dynamic`

⏳ **下一步**:
- 等待 vcpkg 编译完成（30-60 分钟）
- 编译项目
- 运行测试
