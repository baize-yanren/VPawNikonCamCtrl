#!/usr/bin/env bash
#
# 构建 CamCore 核心库并运行主机侧回归测试（mock 相机，无需真机）。
# 依赖：任意 C99 编译器（gcc / clang）。Windows 下使用 LLVM-MinGW 或 MSYS2 gcc。
#
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORE="$ROOT/core"
BUILD="$CORE/build"

mkdir -p "$BUILD"

EXE=""
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
esac

"${CC:-gcc}" -std=c99 -Wall -Wextra \
    -I"$CORE/include" -I"$CORE/src" -I"$CORE/src/ptp" -I"$CORE/src/profile" -I"$CORE/tests" \
    "$CORE/src/session.c" \
    "$CORE/src/capture.c" \
    "$CORE/src/gallery.c" \
    "$CORE/src/params.c" \
    "$CORE/src/ptp/bytebuf.c" \
    "$CORE/src/ptp/container.c" \
    "$CORE/src/ptp/dataset.c" \
    "$CORE/src/profile/camera_profile.c" \
    "$CORE/tests/test_main.c" \
    "$CORE/tests/mock_camera.c" \
    -o "$BUILD/camcore_tests$EXE"

echo "Build OK: $BUILD/camcore_tests$EXE"
"$BUILD/camcore_tests$EXE"
