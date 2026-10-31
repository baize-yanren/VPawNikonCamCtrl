cmake_minimum_required(VERSION 3.16)
project(camcore C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# CamCore 静态库（稳定 C ABI）
add_library(camcore STATIC
    src/session.c
    src/capture.c
    src/gallery.c
    src/params.c
    src/ptp/bytebuf.c
    src/ptp/container.c
    src/ptp/dataset.c
    src/ptpip/ptpip.c
    src/ptpip/ptpip_transport.c
    src/profile/camera_profile.c
)
target_include_directories(camcore PUBLIC include)
target_include_directories(camcore PRIVATE src)

# 主机侧回归测试（mock 相机，无需真机）
enable_testing()

add_executable(camcore_tests
    tests/test_main.c
    tests/mock_camera.c
    tests/mock_ptp_core.c
)
target_link_libraries(camcore_tests camcore)
target_include_directories(camcore_tests PRIVATE tests src)
add_test(NAME camcore_tests COMMAND camcore_tests)

add_executable(camcore_ptpip_tests
    tests/test_ptpip.c
    tests/mock_ptpip_camera.c
    tests/mock_ptp_core.c
)
target_link_libraries(camcore_ptpip_tests camcore)
target_include_directories(camcore_ptpip_tests PRIVATE tests src)
add_test(NAME camcore_ptpip_tests COMMAND camcore_ptpip_tests)
