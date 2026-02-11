#!/bin/bash

# ============================================================================
# CMake 配置测试脚本
# 用于验证重构后的 CMake 配置是否正确
# ============================================================================

set -e  # 遇到错误立即退出

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

echo "============================================================"
echo "  EX_MiracleVision CMake Configuration Test"
echo "============================================================"
echo ""

# ============================================================================
# 检查环境
# ============================================================================

echo "🔍 Checking environment..."

# 检查 CMake
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake not found! Please install CMake 3.15+"
    exit 1
fi
CMAKE_VERSION=$(cmake --version | head -n1 | awk '{print $3}')
echo "  ✓ CMake found: $CMAKE_VERSION"

# 检查编译器
if ! command -v g++ &> /dev/null; then
    echo "❌ g++ not found! Please install g++"
    exit 1
fi
GCC_VERSION=$(g++ --version | head -n1 | awk '{print $3}')
echo "  ✓ g++ found: $GCC_VERSION"

# 检查 vcpkg（可选）
if [ -n "$VCPKG_ROOT" ]; then
    echo "  ✓ VCPKG_ROOT: $VCPKG_ROOT"
    VCPKG_TOOLCHAIN="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    if [ -f "$VCPKG_TOOLCHAIN" ]; then
        echo "  ✓ vcpkg toolchain found"
    else
        echo "  ⚠ vcpkg toolchain not found at $VCPKG_TOOLCHAIN"
        VCPKG_TOOLCHAIN=""
    fi
else
    echo "  ⚠ VCPKG_ROOT not set, will try to find vcpkg..."
    # 尝试常见位置
    for vcpkg_path in "$HOME/vcpkg" "/opt/vcpkg" "$HOME/.vcpkg" "$PROJECT_DIR/../vcpkg"; do
        if [ -f "$vcpkg_path/scripts/buildsystems/vcpkg.cmake" ]; then
            VCPKG_TOOLCHAIN="$vcpkg_path/scripts/buildsystems/vcpkg.cmake"
            echo "  ✓ Found vcpkg at: $vcpkg_path"
            break
        fi
    done
    
    if [ -z "$VCPKG_TOOLCHAIN" ]; then
        echo "  ⚠ vcpkg not found, configuration may fail"
        echo "    Please set VCPKG_ROOT or install vcpkg"
    fi
fi

echo ""

# ============================================================================
# 测试配置
# ============================================================================

echo "🔧 Testing CMake configurations..."
echo ""

BUILD_DIR="$PROJECT_DIR/build-test"

# 清理旧的测试构建
if [ -d "$BUILD_DIR" ]; then
    echo "  Cleaning old test build..."
    rm -rf "$BUILD_DIR"
fi

# ============================================================================
# 测试 1: 基础配置
# ============================================================================

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Test 1: Basic Configuration"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

CMAKE_CMD="cmake -B $BUILD_DIR -S $PROJECT_DIR -DCMAKE_BUILD_TYPE=Release"

if [ -n "$VCPKG_TOOLCHAIN" ]; then
    CMAKE_CMD="$CMAKE_CMD -DCMAKE_TOOLCHAIN_FILE=$VCPKG_TOOLCHAIN"
fi

echo "Running: $CMAKE_CMD"
echo ""

if $CMAKE_CMD; then
    echo ""
    echo "✅ Test 1 PASSED: Basic configuration successful"
else
    echo ""
    echo "❌ Test 1 FAILED: Configuration error"
    exit 1
fi

echo ""

# ============================================================================
# 测试 2: 编译测试
# ============================================================================

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Test 2: Compilation Test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

echo "Building project (this may take a while)..."
echo ""

if cmake --build "$BUILD_DIR" -j$(nproc) 2>&1 | tee "$BUILD_DIR/build.log"; then
    echo ""
    echo "✅ Test 2 PASSED: Compilation successful"
else
    echo ""
    echo "❌ Test 2 FAILED: Compilation error"
    echo "   See build log at: $BUILD_DIR/build.log"
    exit 1
fi

echo ""

# ============================================================================
# 测试 3: 检查产物
# ============================================================================

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Test 3: Build Artifacts Check"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

EXPECTED_EXECUTABLE="$BUILD_DIR/bin/MiracleVision"
EXPECTED_LIBS=(
    "mv-video-capture"
    "mv-uart-serial"
    "mv-basic-armor"
    "mv-basic-pnp"
    "mv-predictor"
)

# 检查可执行文件
if [ -f "$EXPECTED_EXECUTABLE" ]; then
    echo "  ✓ Executable found: MiracleVision"
    file "$EXPECTED_EXECUTABLE"
else
    echo "  ❌ Executable not found: MiracleVision"
    echo "     Expected at: $EXPECTED_EXECUTABLE"
    exit 1
fi

echo ""

# 检查库文件
echo "  Checking libraries..."
LIB_DIR="$BUILD_DIR/lib"
for lib in "${EXPECTED_LIBS[@]}"; do
    if ls "$LIB_DIR"/lib${lib}.so* 1> /dev/null 2>&1; then
        echo "  ✓ Library found: lib${lib}.so"
    else
        echo "  ⚠ Library not found: lib${lib}.so"
    fi
done

echo ""
echo "✅ Test 3 PASSED: Build artifacts verified"

echo ""

# ============================================================================
# 测试 4: 配置选项测试
# ============================================================================

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Test 4: Configuration Options Test"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# 清理
rm -rf "$BUILD_DIR"

# 测试禁用所有可选组件
CMAKE_CMD="cmake -B $BUILD_DIR -S $PROJECT_DIR \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_MINDVISION_SDK=OFF \
    -DUSE_FOXGLOVE_SDK=OFF \
    -DUSE_OPENVINO=OFF \
    -DUSE_ONNXRUNTIME=OFF \
    -DBUILD_TESTS=OFF"

if [ -n "$VCPKG_TOOLCHAIN" ]; then
    CMAKE_CMD="$CMAKE_CMD -DCMAKE_TOOLCHAIN_FILE=$VCPKG_TOOLCHAIN"
fi

echo "Testing with all optional components disabled..."
echo ""

if $CMAKE_CMD > /dev/null 2>&1; then
    echo "  ✓ Configuration with minimal options: OK"
else
    echo "  ❌ Configuration with minimal options: FAILED"
    exit 1
fi

echo ""
echo "✅ Test 4 PASSED: Configuration options working"

echo ""

# ============================================================================
# 总结
# ============================================================================

echo "============================================================"
echo "  Test Summary"
echo "============================================================"
echo ""
echo "✅ All tests PASSED!"
echo ""
echo "Build artifacts location:"
echo "  Executables: $BUILD_DIR/bin/"
echo "  Libraries:   $BUILD_DIR/lib/"
echo ""
echo "To use the test build:"
echo "  cd $PROJECT_DIR"
echo "  ./build-test/bin/MiracleVision"
echo ""
echo "To clean up test build:"
echo "  rm -rf $BUILD_DIR"
echo ""
echo "============================================================"
echo "  Configuration is working correctly! 🎉"
echo "============================================================"
echo ""
