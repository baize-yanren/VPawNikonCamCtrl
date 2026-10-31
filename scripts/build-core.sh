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

INCS="-I$CORE/include -I$CORE/src -I$CORE/src/ptp -I$CORE/src/ptpip -I$CORE/src/profile -I$CORE/tests"

CORE_SRCS="
    $CORE/src/session.c
    $CORE/src/capture.c
    $CORE/src/gallery.c
    $CORE/src/params.c
    $CORE/src/ptp/bytebuf.c
    $CORE/src/ptp/container.c
    $CORE/src/ptp/dataset.c
    $CORE/src/profile/camera_profile.c
"

PTPIP_SRCS="
    $CORE/src/ptpip/ptpip.c
    $CORE/src/ptpip/ptpip_transport.c
"

# 1) USB-PTP 回归测试
"${CC:-gcc}" -std=c99 -Wall -Wextra $INCS \
    $CORE_SRCS \
    "$CORE/tests/test_main.c" \
    "$CORE/tests/mock_camera.c" \
    "$CORE/tests/mock_ptp_core.c" \
    -o "$BUILD/camcore_tests$EXE"

# 2) PTP/IP（无线）回归测试
"${CC:-gcc}" -std=c99 -Wall -Wextra $INCS \
    $CORE_SRCS \
    $PTPIP_SRCS \
    "$CORE/tests/test_ptpip.c" \
    "$CORE/tests/mock_ptpip_camera.c" \
    "$CORE/tests/mock_ptp_core.c" \
    -o "$BUILD/camcore_ptpip_tests$EXE"

echo "Build OK: $BUILD/camcore_tests$EXE"
echo "Build OK: $BUILD/camcore_ptpip_tests$EXE"
echo
echo "== USB-PTP tests =="
"$BUILD/camcore_tests$EXE"
echo
echo "== PTP/IP (wireless) tests =="
"$BUILD/camcore_ptpip_tests$EXE"
