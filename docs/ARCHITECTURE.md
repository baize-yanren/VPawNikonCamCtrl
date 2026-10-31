# CamCore 架构说明

## 1. 分层

```
┌─────────────┐   ┌──────────────┐
│ Android UI  │   │ HarmonyOS UI │   ← 原生：Kotlin/Compose、ArkTS/ArkUI
│ (Compose)   │   │ (ArkUI)      │
└──────┬──────┘   └──────┬───────┘
       │ JNI (NDK)       │ NAPI
       └────────┬─────────┘
         ┌──────▼───────┐
         │   CamCore     │  ← 本目录：独立 C 后端，稳定 C ABI（camcore.h）
         └──────┬───────┘
                │ Transport 抽象（USB / TCP / BLE，由平台适配层实现并注入）
         ┌─────┴─────┐
         │ USB PTP   │ PTP/IP  │ BLE  │  —— 尼康机身
```

CamCore 不含任何平台 API，只暴露 C 头文件 `include/camcore.h`（不透明句柄 + 枚举 + 异步回调）。两端通过实现 `cc_transport_ops_t`（connect/disconnect/send/recv/set_timeout）注入底层传输。

## 2. 目录结构

```
core/
├── include/camcore.h        # 公开 C API（唯一对外契约）
├── src/
│   ├── internal.h           # 内部共享：会话结构 + 事务引擎声明
│   ├── session.c            # 生命周期 + 事务引擎 + OpenSession/CloseSession
│   ├── capture.c            # 遥控快门 / B门 / 实时取景
│   ├── gallery.c            # 对象枚举 / 分块下载 / 断点续传
│   ├── params.c             # 设备属性读写
│   ├── ptp/                 # PTP 协议层
│   │   ├── ptp_constants.h  # 操作码/属性码/响应码/事件码常量
│   │   ├── bytebuf.c        # 小端序字节缓冲
│   │   ├── container.c      # 12 字节容器编解码
│   │   └── dataset.c        # 字符串/数组/UTF-16LE 解析
│   └── profile/             # 机型档案表（PID/EXPEED/ISO 范围）
├── tests/
│   ├── mock_camera.c        # 内存 mock 相机（复用 FakeNikonServer 思路）
│   └── test_main.c          # 回归测试
└── CMakeLists.txt
```

## 3. 事务引擎

`cc_transact()` 完成一次 PTP 事务：发 command →（可选 data-out）→ 收 data-in（流式喂给 sink）→ 收 response。data-in 通过 `cc_data_sink` 回调流式消费（256 KiB 分块），大文件下载不会整读进内存。

## 4. 关键设计决策

- **C ABI 面**：核心只用 C99，天然稳定 ABI；C++ 类型/异常不跨边界。
- **流式下载**：分块 4 MiB + 断点续传，但真正的内存控制靠 256 KiB 复制缓冲（见 docs/PROTOCOL.md §9）。
- **clean-room**：协议层依据 docs/PROTOCOL.md 的对照笔记重写，不并入 GPL/AGPL/无 License 代码。

## 5. 已实现 / 待实现

| 能力 | 状态 |
|---|---|
| USB-PTP 容器/事务/会话/事件 | ✅ 已实现 + mock 回归 |
| 图库枚举 + 分块下载 + 续传 | ✅ 已实现 |
| 遥控快门 / B门 / 参数读写 | ✅ 已实现（需真机验证 quirk） |
| 实时取景（JPEG 帧） | ✅ 已实现 |
| PTP/IP（WiFi）帧封装 + 350ms 握手 | ⏳ 待实现（src/ptpip/） |
| USB / BLE Transport 平台适配层 | ⏳ 待实现（Android JNI / HarmonyOS NAPI） |

## 6. 构建与测试

```bash
bash scripts/build-core.sh   # 编译核心 + 运行 mock 回归测试（无需真机）
```
