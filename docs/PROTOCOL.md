# 尼康相机 PTP / PTP/IP 协议对照笔记

> 本笔记由三个参考开源项目（PhotoSync、NikonLink、ZENCHE）的源码阅读整理而成，作为 CamCore 核心库 clean-room 重写的协议事实基准。所有常量与字节布局均来自真实代码，非推测。

---

## 0. 全局约定

- **字节序**：全部**小端序（little-endian）**。
- **PTP 字符串**：`uint8 长度N（含结尾 null，即 len+1）` + `N × uint16 (UTF-16LE)`。
- **PTP 数组**：`uint32 元素数` + `元素数 × 元素`。
- **Code 高 4 位编码类型**：`0x1` OperationCode、`0x2` ResponseCode、`0x3` ObjectFormatCode、`0x4` EventCode、`0x5` DevicePropCode、`0x9` Vendor-OpCode、`0xA` Vendor-ResponseCode、`0xC` Vendor-EventCode、`0xD` Vendor-PropCode。

---

## 1. USB-PTP 容器格式（12 字节头）

```
offset 0   uint32  ContainerLength
offset 4   uint16  ContainerType   (1=Command, 2=Data, 3=Response, 4=Event)
offset 6   uint16  Code
offset 8   uint32  TransactionID
```

- Command 容器：`12 + 4*N` 字节，参数紧跟 12 字节头之后，每个参数 `uint32`（最多 3 个参数，`numParams = (length-12)/4`）。
- Data 容器：type=2、code=操作码、xid 与对应 command 一致。
- Response 容器：type=3、code=ResponseCode。

### 事务原语（transactUnsync 完整序列）

1. 发 Command（bulk OUT）。
2. **ZLP 规则**：若发送长度恰为端点 maxPacketSize 整数倍，额外发一个 0 长度包。
3. Data 阶段（IN 或 OUT，取决于方向）；IN 时校验返回头 `type==data && code==opcode && xid==xid`，超长循环读满。
4. 读 Response（bulk IN）。

### USB 类专用控制请求（Annex D）

| 常量 | 值 | 用途 |
|---|---|---|
| CLASS_CANCEL_REQ | 0x64 | 取消请求 |
| CLASS_GET_EVENT_DATA | 0x65 | 获取事件数据 |
| CLASS_DEVICE_RESET | 0x66 | 设备复位 |
| CLASS_GET_DEVICE_STATUS | 0x67 | 获取设备状态 |

---

## 2. PTP/IP 帧格式与握手

### 帧头（8 字节）

```
offset 0   uint32  总长度（含头部 8 字节）
offset 4   uint32  包类型
```

包类型：1=InitCommandRequest、2=InitCommandAck、3=InitEventRequest、4=InitEventAck、5=InitFail、6=OperationRequest、7=OperationResponse、8=Event、9=StartData、10=Data、11=CancelTransaction、12=EndData、13=ProbeRequest、14=ProbeResponse。

### 握手流程（端口 15740，两条独立 TCP 连接）

1. 命令连接 `connect(host, 15740)`。
2. **等 350ms**（尼康 quirk，见 §9）。
3. 发 InitCommandRequest，收 InitCommandAck，取 `connectionNumber`（u32）。
4. 事件连接 `connect(host, 15740)`，再等 350ms。
5. 发 InitEventRequest（携带 connectionNumber），收 InitEventAck。
6. 事件握手失败**不致命**，可降级为仅下载。

### InitCommandRequest 载荷

```
offset 0   16 字节固定 GUID: 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF
offset 16  设备名 UTF-16LE + \0\0
offset ..  uint32 协议版本 0x00010000
```

### OperationRequest 载荷

```
offset 0   uint32 dataPhase (1=无数据, 2=有上行数据)
offset 4   uint16 operation
offset 6   uint32 transactionId (自增，从 1 起，Int.MAX 回绕 1)
offset 10  各参数 uint32
```

### OperationResponse 载荷

```
offset 0   uint16 responseCode (OK=0x2001)
offset 2   uint32 transactionId
offset 6   各响应参数 uint32
```

### 命令通道 vs 事件通道

- 命令通道：OperationRequest →（上行时 StartData/EndData）→ 响应 StartData/Data/EndData → OperationResponse。
- 事件通道：仅 Event + ProbeRequest/Response。收到 ProbeRequest 立即回空 ProbeResponse 保活。

---

## 3. 会话建立（USB 与 IP 通用语义）

1. OpenSession（0x1002），参数 sessionId（从 1 起）。
2. 响应 SessionAlreadyOpen（0x201E）→ 先 CloseSession 重建重试。
3. 未开会话前仅允许 GetDeviceInfo（0x1001）与 OpenSession。
4. xid 从 1 递增；sessionId 从 1 起。

### GetDeviceInfo 数据布局

```
uint16 standardVersion
int32  vendorExtensionId
uint16 vendorExtensionVersion
string vendorExtensionDesc
uint16 functionalMode
array<uint16> operationsSupported
array<uint16> eventsSupported
array<uint16> propertiesSupported
array<uint16> captureFormats
array<uint16> imageFormats
string manufacturer / model / deviceVersion / serialNumber
```

---

## 4. 操作码（Operation Code）

### 标准 PTP

| Opcode | 值 | 说明 |
|---|---|---|
| GetDeviceInfo | 0x1001 | 设备信息 |
| OpenSession | 0x1002 | 开会话 |
| CloseSession | 0x1003 | 关会话 |
| GetStorageIDs | 0x1004 | 存储卷列表 |
| GetStorageInfo | 0x1005 | 卷信息 |
| GetNumObjects | 0x1006 | 对象数 |
| GetObjectHandles | 0x1007 | 枚举对象句柄 |
| GetObjectInfo | 0x1008 | 对象信息 |
| GetObject | 0x1009 | 下载对象 |
| GetThumb | 0x100A | 缩略图 |
| DeleteObject | 0x100B | 删除 |
| SendObjectInfo | 0x100C | 发送对象信息 |
| SendObject | 0x100D | 发送对象 |
| InitiateCapture | 0x100E | 发起拍摄 |
| FormatStore | 0x100F | 格式化 |
| ResetDevice | 0x1010 | 复位 |
| GetDevicePropDesc | 0x1014 | 属性描述 |
| GetDevicePropValue | 0x1015 | 属性值 |
| SetDevicePropValue | 0x1016 | 设置属性值 |
| ResetDevicePropValue | 0x1017 | 复位属性 |
| TerminateOpenCapture | 0x1018 | 终止拍摄 |
| GetPartialObject | 0x101B | 分段获取对象 |

### 尼康扩展 opcode（0x90xx / 0x92xx）

| Opcode | 值 | 说明 |
|---|---|---|
| NK_OC_GetProfileAllData | 0x9006 | 获取配置 |
| NK_OC_SendProfileData | 0x9007 | 发送配置 |
| NK_OC_AdvancedTransfer | 0x9010 | 高级传输 |
| NK_OC_GetFileInfoInBlock | 0x9011 | 分块文件信息 |
| NK_OC_Capture | 0x90C0 | 拍摄 |
| NK_OC_SetControlMode | 0x90C2 | 设置控制模式（B门进出 remote mode） |
| NK_OC_CheckEvent | 0x90C7 | 查询事件队列 |
| NK_OC_DeviceReady | 0x90C8 | 设备就绪 |
| NK_OC_CaptureInSDRAM | 0x90CB | 拍摄到 SDRAM |
| NK_OC_GetDevicePTPIPInfo | 0x90E0 | PTP/IP 信息 |
| NIKON_LargeThumb | 0x90C4 | 大缩略图 |
| NIKON_GetPreviewImg | 0x9200 | 预览图 |
| NIKON_StartLiveView | 0x9201 | 启动实时取景 |
| NIKON_EndLiveView | 0x9202 | 结束实时取景 |
| NIKON_GetLiveViewImg | 0x9203 | 获取实时取景帧（JPEG） |
| NIKON_MfDrive | 0x9204 | 手动对焦驱动 |
| NIKON_ChangeAfArea | 0x9205 | 改变对焦区域 |
| NIKON_AfDriveCancel | 0x9206 | 取消对焦驱动 |
| NIKON_CaptureToSdram | 0x9207 | 拍摄到 SDRAM（参数 [0xffffffff, 1]） |
| NIKON_TerminateCapture | 0x920C | 结束曝光（B门用） |

### 尼康扩展响应码（0xA0xx）

| 值 | 含义 |
|---|---|
| 0xA001 | 硬件错误 |
| 0xA002 | 未合焦 |
| 0xA003 | 切换相机模式失败 |
| 0xA004 | 非法状态 |
| 0xA005 | 属性设置不支持 |
| 0xA008 | Bulb 快门 |
| 0xA00B | 非实时取景 |

### 标准响应码（重要子集）

| 值 | 含义 |
|---|---|
| 0x2001 | OK |
| 0x2002 | GeneralError |
| 0x2003 | SessionNotOpen |
| 0x2004 | InvalidTransactionID |
| 0x2005 | OperationNotSupported |
| 0x2006 | ParameterNotSupported |
| 0x2007 | IncompleteTransfer |
| 0x2009 | InvalidObjectHandle / DeviceBusy |
| 0x200A | DevicePropNotSupported |
| 0x2010 | NoThumbnailPresent |
| 0x2019 | DeviceBusy |
| 0x201D | InvalidParameter |
| 0x201E | SessionAlreadyOpen |
| 0x201F | TransactionCanceled |

---

## 5. 设备属性码与值编码

### 标准属性码（控制拍摄用）

| 参数 | 属性码 | 类型 | 编码 |
|---|---|---|---|
| 快门速度 | 0x500D (ExposureTime) | UINT32 | 秒 × 10000 |
| 光圈 | 0x5007 (FNumber) | UINT16 | F × 100 |
| ISO | 0x500F (ExposureIndex) | UINT16 | 原值 |
| 曝光补偿 | 0x5010 (ExposureBiasCompensation) | S32 | 单位 1/1000 EV |
| 白平衡 | 0x5005 (WhiteBalance) | UINT16 | 自动=0x0002、手动预设=0x8013、日光=0x0004… |
| 对焦模式 | 0x500A (FocusMode) | UINT16 | AF-S=0x8010、AF-C=0x8011、MF=0x0001 |
| 拍摄模式 | 0x500E (ExposureProgramMode) | UINT16 | P=2、S=4、A=3、M=1 |
| Picture Control | 0xD200 (ActivePicCtrlItem) | — | 自动/标准/自然/鲜艳/单色/人像/风景/平面 = 8/1/2/3/4/5/6/7 |

### 尼康快门双编码（重要）

- **标准 PTP** `0x500D`：UINT32，`秒 × 10000`。
- **尼康专有** `0xD100`（NIKON_EXPOSURE_TIME）：打包 `(numerator << 16) | denominator`。
  - 分数（<1s）：`(1 << 16) | 分母`
  - 整数秒：`(秒 << 16) | 1`
- **视频快门**：`0xD1A8`。
- 回退链：照片 `[0xD100, 0x500D]`；视频 `[0xD1A8, 0x500D, 0xD100]`。

### 属性值类型码

| 类型 | 值 |
|---|---|
| s8 / u8 | 0x0001 / 0x0002 |
| s16 / u16 | 0x0003 / 0x0004 |
| s32 / u32 | 0x0005 / 0x0006 |
| s64 / u64 | 0x0007 / 0x0008 |
| string | 0xFFFF |

### DevicePropDesc 数据布局

```
uint16 propertyCode
uint16 dataType
uint8  getSet (1=可写)
value  factoryDefault / currentValue (按 dataType 长度)
uint8  formType (0=无约束, 1=Range, 2=枚举)
```

---

## 6. 事件模型

### 尼康 6 字节定长事件

尼康 vendor 事件由 `CheckEvent(0x90C7)` 的 Data 阶段返回，payload 布局：

```
offset 0   uint16  eventCount
随后每条固定 6 字节记录：
offset 0   uint16  eventCode
offset 2   uint32  param1
```

解析前需**剥离 Data 容器 12 字节头**。每条记录只有 1 个 32 位参数，定长 6 字节（对比佳能 EOS 变长 `u32 len + u32 code + 变长参数`）。

### 尼康事件码

| 值 | 含义 |
|---|---|
| 0xC101 | ObjectAddedInSDRAM（拍摄完成，param=对象句柄） |
| 0xC102 | CaptureOverflow |
| 0xC103 | AdvancedTransfer |

事件轮询间隔 200ms；遇到 0xC101 触发下载。拍摄后对象句柄兜底值 `0xFFFF0001`。

---

## 7. 实时取景（Live View）

- 开启 `0x9201`（StartLiveView，10s 超时）。
- 取帧 `0x9203`（GetLiveViewImage，data-in，返回 JPEG，12s 超时），扫描 FFD8/FFD9 标记提取 JPEG。
- 关闭 `0x9202`（EndLiveView）。
- 取帧循环约 10fps，单帧失败 300ms 退避重试；断连重连先停取景再关会话。

---

## 8. 遥控拍摄

### 单次拍摄

- `CaptureToSdram(0x9207)` 参数 `[0xffffffff, 1]`（60s 超时）。
- 拍摄后轮询 `DeviceReady(0x90C8)` + `CheckEvent(0x90C7)` 找 SDRAM 对象句柄。
- 无句柄 fallback `0xFFFF0001`。

### B 门

1. `SetControlMode(0x90C2)` 参数 `[1]` 进入 remote/control mode。
2. `SetDevicePropValue(0x1016)` 写 `0x500E = 1`（M 模式）。
3. 设快门值为 B 门（`0xFFFFFFFF`）。
4. `CaptureToSdram(0x9207)` 参数 `[0xffffffff, 1]`。
5. 等待曝光时长（上限 900s）。
6. `TerminateCapture(0x920C)` 结束曝光。
7. finally 中 `SetControlMode(0x90C2)` 参数 `[0]` 释放机身控制。

### 拍摄后 busy 退避（Z50 健壮性）

`GetObject` 返回 DeviceBusy（0x2009）时，不重复触发快门，指数退避 `300ms << n` 封顶 4s，最多 9 次、20s 超时，每轮重新 CheckEvent 刷新对象句柄。

---

## 9. 图库与下载

### 枚举流程

1. `GetStorageIDs(0x1004)` → 存储卷 ID。
2. 每卷 `GetStorageInfo(0x1005)` → 卷信息。
3. `GetObjectHandles(0x1007)` 参数 `[storageID, 0, 0xffffffff]` → 顶层句柄。
4. 每句柄 `GetObjectInfo(0x1008)`；关联对象（目录）递归枚举。
5. `GetThumb(0x100A)` 取缩略图。
6. `GetObject(0x1009)` 全量下载；`GetPartialObject(0x101B)` 分段下载。

### ObjectInfo 数据布局

```
uint32 storageId
uint16 format / protectionStatus
uint32 compressedSize
uint16 thumbFormat
uint32 thumbSize / thumbWidth / thumbHeight
uint32 imageWidth / imageHeight / bitDepth
uint32 parentObject
uint16 associationType
uint32 associationDesc / sequenceNumber
PTPString filename / captureDate / modificationDate
```

### 分块下载 + 断点续传（NikonLink 设计，C 核心必须复刻）

- **默认块 4 MiB**（可选 1/2/4/8 MiB），下限 512 KiB。
- **内存控制核心**：不用 4 MiB 缓冲整读，而是固定 **256 KiB** 小缓冲流式转写（`copyTo`），堆占用恒定 256 KiB 级。
- `.part` 临时文件 `{handle}_{size}.part`，续传起点 = 本地文件长度。
- 分块失败（InvalidParameter 0x201D / IncompleteTransfer 0x2007 / GeneralError 0x2002）→ 块减半重试，先回滚未完成分块。
- 完成 → 拷入正式文件 → 删 `.part` → 标记已导入。
- 网络异常不删 `.part`，下次续传。

---

## 10. 机型表（USB VID/PID）

- **VID**：尼康 `0x04B0`（1200）、佳能 `0x04A9`（1193）、索尼 `0x054C`（1356）。
- USB 接口 class=6（Still Imaging）；批量 IN/OUT + 中断端点。

| 世代 | 机型 | PID |
|---|---|---|
| EXPEED 5 | D500 | 0x043A |
| EXPEED 5 | D7500 | 0x0445 |
| EXPEED 5 | D850 | 0x044A |
| EXPEED 6 | Z7 | 0x0442 |
| EXPEED 6 | Z6 | 0x0443 |
| EXPEED 6 | Z50 | 0x0444 |
| EXPEED 6 | D780 | 0x0446 |
| EXPEED 6 | D6 | 0x0447 |
| EXPEED 6 | Z5 | 0x0448 |
| EXPEED 6 | Z7II | 0x044B |
| EXPEED 6 | Z6II | 0x044C |
| EXPEED 6 | Z fc | 0x044F |
| EXPEED 6 | Z30 | 0x0452 |
| EXPEED 7 | Z9 | 0x0450 |
| EXPEED 7 | Z8 | 0x0451 |
| EXPEED 7 | Z f | 0x0453 |
| EXPEED 7 | Z6III | 0x0454 |
| EXPEED 7 | Z50II | 0x0455 |
| EXPEED 7 | Z5II | 0x0456 |
| EXPEED 7 | ZR | 0x0457 |

---

## 11. 关键 quirk 清单（C 核心必须实现）

1. **PTP/IP 握手后 350ms 延迟**（Z50II 实测：TCP 建连后立即发包会被机身静默断连，命令连接与事件连接都要等）。
2. **ZLP**：USB 发送长度恰为端点 maxPacketSize 整数倍时补 0 长度包。
3. **尼康事件 = 6 字节定长**（u16 code + u32 param），前缀 u16 count，解析前剥 12 字节 Data 头。
4. **快门双编码**：标准 `0x500D`（秒×10000）与尼康专有 `0xD100`（分子<<16|分母），需按回退链尝试。
5. **拍摄后 DeviceBusy 退避**：指数退避 + 重刷 CheckEvent，避免重复快门。
6. **B 门必须进出 remote mode**（SetControlMode 1/0）+ M 模式 + TerminateCapture，失败必须释放机身控制。
7. **NikonLink 无蓝牙认证配对**：应用层配对 = WiFi 连接 + PTP/IP 握手（携带固定 GUID 00 11 22 … FF 让机身认作已知主机）。

---

## 12. 参考项目定位与 License 边界

| 项目 | License | 可借鉴 | 不能做 |
|---|---|---|---|
| ZENCHE | MIT | 架构、机型表、B门/快门/退避逻辑、属性码表 | 无（MIT 可并，但我们选择 clean-room 保持独立） |
| PhotoSync | 无 License | 有线 USB PTP 容器/事务/事件实现细节 | 直接搬运代码 |
| NikonLink | 无 License | 无线 PTP/IP 握手/分块/续传设计 | 直接搬运代码 |
| libgphoto2 | LGPL-2.1 | 协议权威实现（对照） | 静态并入（如动态链接需守 LGPL） |

> CamCore 以本笔记为 clean-room 依据重写为自有 C/C++，整体 MIT，不并入任何 GPL/AGPL/无 License 代码。
