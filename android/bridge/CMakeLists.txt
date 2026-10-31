# NDK 构建：编译 CamCore 核心（静态库）+ JNI 桥 → libcamcore.so
cmake_minimum_required(VERSION 3.18)
project(camcore_bridge C)

set(CMAKE_C_STANDARD 99)
set(CAMCORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../core")

# CamCore 核心静态库
add_library(camcore_static STATIC
    ${CAMCORE_ROOT}/src/session.c
    ${CAMCORE_ROOT}/src/capture.c
    ${CAMCORE_ROOT}/src/gallery.c
    ${CAMCORE_ROOT}/src/params.c
    ${CAMCORE_ROOT}/src/ptp/bytebuf.c
    ${CAMCORE_ROOT}/src/ptp/container.c
    ${CAMCORE_ROOT}/src/ptp/dataset.c
    ${CAMCORE_ROOT}/src/ptpip/ptpip.c
    ${CAMCORE_ROOT}/src/ptpip/ptpip_transport.c
    ${CAMCORE_ROOT}/src/profile/camera_profile.c
)
target_include_directories(camcore_static PUBLIC ${CAMCORE_ROOT}/include)
target_include_directories(camcore_static PRIVATE ${CAMCORE_ROOT}/src)

# JNI 桥 → libcamcore.so（Kotlin System.loadLibrary("camcore")）
add_library(camcore SHARED camcore_jni.c)
target_link_libraries(camcore camcore_static)
target_include_directories(camcore PRIVATE
    ${CAMCORE_ROOT}/include
    ${CAMCORE_ROOT}/src
    ${CAMCORE_ROOT}/src/ptp
    ${CAMCORE_ROOT}/src/ptpip
)
