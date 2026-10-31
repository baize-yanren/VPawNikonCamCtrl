# HarmonyOS 适配说明

## 集成方式

与 Android 同构：**ArkTS 实现 Transport + NAPI 桥接共享 CamCore 核心**。

- `napi/camcore_napi.cpp`：NAPI 桥，把 CamCore C API 暴露为 `libcamcore.so`，transport 经 napi 回调 ArkTS。
- `entry/src/main/ets/connector/CameraController.ets`：ArkTS 的 USB Transport（`@ohos.usbManager`）+ 高层控制器。

## 构建（DevEco Studio）

1. 用 DevEco Studio 打开 `harmony/`（或新建 Stage 工程后拷贝 `entry` 与 `napi`）。
2. `entry/src/main/cpp/CMakeLists.txt` 引用 `napi/camcore_napi.cpp` 并链接 `core/` 源码（同 Android bridge 的 CMake 配置）。
3. 在 `oh-package.json5` 声明 NAPI 模块 `libcamcore.so`，在 `module.json5` 声明权限：
   - `ohos.permission.INTERNET`、`ohos.permission.GET_NETWORK_INFO`、`ohos.permission.GET_WIFI_INFO`
   - `ohos.permission.ACCESS_BLUETOOTH`、`ohos.permission.LOCATION`（可选，GPS 标签）
4. `hvigor` 构建产出 `.hap`；真机运行需 DevEco 开发者签名 + Profile。

## USB 注意点

- `@ohos.usbManager.bulkTransfer` 单次 < 200KB，需 192KB 分块重组（见 docs/PROTOCOL.md）。
- USB 权限：`usbManager.hasRight()` / `requestRight()`，首次接入需用户授权。

## 后续增强

- PTP/IP（WiFi）：ArkTS `socket` + `connection`，需 `netHandle.bindSocket()` 绑定相机 Wi-Fi 网卡。
- BLE 配对：`@ohos.bluetooth`（首版可仅做发现/配对，见 docs/PROTOCOL.md §8）。
