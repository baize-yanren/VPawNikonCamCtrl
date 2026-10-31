# Nikon Connector —— 尼康相机连接器

一个面向尼康相机的跨平台连接器：**有线（USB-PTP）与无线（BLE 配对 + WiFi PTP/IP）** 下的**遥控拍摄、实时取景、图库浏览与下载**。Android 优先，HarmonyOS（鸿蒙）同步适配。MIT 协议。

> 协议层依据多个开源项目 + 公开 PTP 规范 clean-room 重写为自有 C 核心，规避 License 污染。详见 [docs/PROTOCOL.md](docs/PROTOCOL.md)。

## 架构

```
┌─────────────┐   ┌──────────────┐
│ Android UI  │   │ HarmonyOS UI │   ← Kotlin/Compose、ArkTS/ArkUI
└──────┬──────┘   └──────┬───────┘
       │ JNI (NDK)       │ NAPI
       └────────┬─────────┘
         ┌──────▼───────┐
         │   CamCore     │  ← 独立 C 后端（core/），稳定 C ABI
         └──────┬───────┘
                │ Transport 抽象（USB / TCP / BLE）
         ┌─────┴─────┐
         │ USB PTP   │ PTP/IP  │ BLE  │  —— 尼康机身
```

- **`core/`**：共享协议核心（C99，稳定 C ABI），不含平台 API。已实现 USB-PTP 容器/事务/会话/事件、图库枚举、4 MiB 分块 + 断点续传下载、遥控快门/B门、参数读写、实时取景，以及 PTP/IP（WiFi）帧编解码 + 握手适配（`src/ptpip/`）。主机侧可用 mock 相机回归测试（USB + PTP/IP 两组）。
- **`android/`**：JNI 桥 + Kotlin/Compose UI，支持 **USB（USB-PTP）** 与 **WiFi（PTP/IP，端口 15740）** 两种连接。
- **`harmony/`**：NAPI 桥 + ArkTS（`@ohos.usbManager`）。

## 快速开始

### 核心库（无需真机，无需 Android/Harmony 环境）

```bash
bash scripts/build-core.sh   # 编译 CamCore 并运行 mock 回归测试
```

依赖仅任意 C99 编译器（gcc/clang）。

### Android

用 Android Studio 打开 `android/`（NDK + CMake 已配置），或命令行构建：

```bash
# 前置：JDK 17 + Android SDK（platform-tools/platforms;android-35/build-tools;35.0.0/ndk;26.1.10909125/cmake;3.22.1）
cd android
./gradlew assembleDebug        # 或 gradlew.bat（Windows）
# 产物：app/build/outputs/apk/debug/app-debug.apk
```

- **USB**：`android/app/.../UsbTransport.kt`（USB-PTP，VID 0x04B0）。
- **WiFi**：`android/app/.../PtpIpTransport.kt`（TCP 字节流）+ native `cc_ptpip_transport`（握手/帧编解码复用 C 实现）。
- 真机需相机处于 USB PTP / WiFi PTP/IP 模式，USB 首次接入需授权。

### HarmonyOS

见 [harmony/README.md](harmony/README.md)（DevEco Studio + hvigor）。

## 支持的尼康机型（USB PID）

EXPEED 5/6/7 共 20 款：Z30/Z50/Z50II/Zfc/Z5/Z5II/Z6/Z6II/Z6III/Z7/Z7II/Z8/Z9/Zf/ZR 及 D500/D7500/D780/D850/D6。完整 PID 表见 [docs/PROTOCOL.md](docs/PROTOCOL.md) §10。

## 能力边界

- 尼康 BLE 通道仅承载配对/低带宽控制；图传与实时取景走 USB-PTP 或 WiFi PTP/IP（机身固件限制）。
- 实时取景为 Motion-JPEG 帧流，首版目标"可用监看"，视频外录仅 M-JPEG。
- 协议层已过 mock 回归，**真机 quirk 需逐台实测**（见 docs/PROTOCOL.md §11）。

## License

主体 [MIT](LICENSE)。协议层为 clean-room 重写，不并入 GPL/AGPL/无 License 代码；第三方库（如需）单独声明。

## 参考项目致谢

- [gphoto/libgphoto2](https://github.com/gphoto/libgphoto2)（LGPL，协议事实基准）
- [Tauber01/ZENCHE](https://github.com/Tauber01/ZENCHE)（MIT，架构与机型表）
- [devcxl/PhotoSync](https://github.com/devcxl/PhotoSync)、[AchooLuv/NikonLink](https://github.com/AchooLuv/NikonLink)（有线/无线实现参考）
