#ifndef CAMCORE_H
#define CAMCORE_H

/*
 * CamCore —— 尼康相机连接器共享协议核心库
 *
 * 设计原则（见 docs/PROTOCOL.md）：
 *   1. 稳定 C ABI：本头文件是唯一对外契约，只暴露不透明句柄 + 枚举 + 回调，不跨边界暴露 C++ 类型/异常。
 *   2. Transport 抽象注入：核心不含任何平台 API；USB/TCP/BLE 由两端适配层实现 cc_transport_ops_t 后注入。
 *   3. 异步事件模型：核心自持线程，事件经回调上抛；上层永不阻塞在 PTP 调用上。
 *
 * License: MIT
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAMCORE_VERSION_MAJOR 0
#define CAMCORE_VERSION_MINOR 1
#define CAMCORE_VERSION_PATCH 0

/* ------------------------------------------------------------------ */
/* 状态码                                                              */
/* ------------------------------------------------------------------ */
typedef enum cc_status {
    CC_OK = 0,
    CC_ERR_INVALID_ARG = -1,
    CC_ERR_NOT_CONNECTED = -2,
    CC_ERR_TIMEOUT = -3,
    CC_ERR_PROTOCOL = -4,
    CC_ERR_DEVICE_BUSY = -5,
    CC_ERR_UNSUPPORTED = -6,
    CC_ERR_IO = -7,
    CC_ERR_SESSION = -8,
    CC_ERR_NO_MEMORY = -9,
} cc_status_t;

/* ------------------------------------------------------------------ */
/* 传输类型                                                            */
/* ------------------------------------------------------------------ */
typedef enum cc_transport_kind {
    CC_TRANSPORT_USB = 1,   /* USB-PTP（有线） */
    CC_TRANSPORT_PTPIP = 2, /* PTP/IP（WiFi，端口 15740） */
} cc_transport_kind_t;

/* ------------------------------------------------------------------ */
/* 事件                                                                */
/* ------------------------------------------------------------------ */
typedef enum cc_event_type {
    CC_EVENT_CONNECTED = 1,
    CC_EVENT_DISCONNECTED = 2,
    CC_EVENT_CAPTURE_DONE = 3,  /* 拍摄完成，arg0 = 对象句柄 */
    CC_EVENT_OBJECT_ADDED = 4,  /* 新对象，arg0 = 对象句柄 */
    CC_EVENT_PROGRESS = 5,      /* 下载进度，arg0 = 已下载字节，arg1 = 总字节 */
    CC_EVENT_ERROR = 6,         /* 错误，arg0 = cc_status_t */
} cc_event_type_t;

typedef struct cc_event {
    cc_event_type_t type;
    uint32_t arg0;
    uint32_t arg1;
    const void *data;
    size_t data_len;
} cc_event_t;

typedef void (*cc_event_fn)(void *userdata, const cc_event_t *event);

/* ------------------------------------------------------------------ */
/* Transport 抽象（平台适配层实现，注入核心）                            */
/* ------------------------------------------------------------------ */
typedef struct cc_transport cc_transport_t;

typedef struct cc_transport_ops {
    /* 建立底层连接；connect_params 语义由 transport_kind 决定（USB 传 usb_handle，PTP/IP 传 host 字符串）。 */
    int (*connect)(void *transport_ctx, const void *connect_params);
    int (*disconnect)(void *transport_ctx);
    /* 发送字节；返回实际发送字节数，负数为错误。 */
    int (*send)(void *transport_ctx, const uint8_t *data, size_t len);
    /* 接收字节；返回实际接收字节数（0 = EOF），负数为错误。 */
    int (*recv)(void *transport_ctx, uint8_t *data, size_t len);
    /* 设置收发超时（毫秒）。 */
    int (*set_timeout)(void *transport_ctx, int timeout_ms);
} cc_transport_ops_t;

struct cc_transport {
    const cc_transport_ops_t *ops;
    void *ctx;
    cc_transport_kind_t kind;
};

/* ------------------------------------------------------------------ */
/* 连接参数                                                            */
/* ------------------------------------------------------------------ */
typedef struct cc_connect_params {
    cc_transport_kind_t kind;
    union {
        const char *host;      /* PTP/IP：相机 IP（如 192.168.1.1） */
        void *usb_handle;      /* USB：平台原生 USB 设备句柄 */
    } u;
    uint16_t port;             /* PTP/IP 端口，默认 15740 */
} cc_connect_params_t;

/* ------------------------------------------------------------------ */
/* 对象信息                                                            */
/* ------------------------------------------------------------------ */
typedef struct cc_object_info {
    uint32_t handle;
    uint32_t compressed_size;
    uint16_t format;         /* PTP ObjectFormatCode */
    char filename[256];
    char capture_date[64];
    int is_association;      /* 目录/关联对象 */
} cc_object_info_t;

/* ------------------------------------------------------------------ */
/* 核心实例                                                            */
/* ------------------------------------------------------------------ */
typedef struct cc_session cc_session_t;

/* 版本号（返回 "MAJOR.MINOR.PATCH"）。 */
const char *cc_version(void);

/* 创建实例；transport 由调用方注入并保持有效直到 cc_destroy。 */
cc_session_t *cc_create(cc_transport_t *transport, cc_event_fn event_fn, void *userdata);

/* 销毁实例，释放资源（含未完成连接）。 */
void cc_destroy(cc_session_t *s);

/* 连接相机（USB 或 PTP/IP），完成 OpenSession。 */
cc_status_t cc_connect(cc_session_t *s, const cc_connect_params_t *params);

/* 断开连接（CloseSession + 底层断开）。 */
cc_status_t cc_disconnect(cc_session_t *s);

/* ------------------------------------------------------------------ */
/* 遥控拍摄                                                            */
/* ------------------------------------------------------------------ */
/* 单次拍摄；拍摄完成后经 CC_EVENT_CAPTURE_DONE 回调对象句柄。 */
cc_status_t cc_capture(cc_session_t *s);

/* B 门：开始曝光（自动进入 remote mode + M 模式 + Bulb）。 */
cc_status_t cc_start_bulb(cc_session_t *s);

/* B 门：结束曝光（TerminateCapture + 释放机身控制）。 */
cc_status_t cc_end_bulb(cc_session_t *s);

/* ------------------------------------------------------------------ */
/* 实时取景                                                            */
/* ------------------------------------------------------------------ */
cc_status_t cc_start_live_view(cc_session_t *s);
/* 取一帧 JPEG；buf 由调用方分配，*len 传入容量、传出实际字节数。 */
cc_status_t cc_get_live_view_frame(cc_session_t *s, uint8_t *buf, size_t *len);
cc_status_t cc_end_live_view(cc_session_t *s);

/* ------------------------------------------------------------------ */
/* 图库                                                                */
/* ------------------------------------------------------------------ */
/* 枚举对象；out 由调用方分配（count 传入容量），返回实际数量。 */
cc_status_t cc_list_objects(cc_session_t *s, cc_object_info_t *out, size_t *count);

/* 下载对象到本地路径（分块 + 断点续传，进度经 CC_EVENT_PROGRESS 回调）。 */
cc_status_t cc_download(cc_session_t *s, uint32_t handle, const char *path);

/* ------------------------------------------------------------------ */
/* 参数                                                                */
/* ------------------------------------------------------------------ */
/* 读取设备属性值；value 按 PTP 类型编码（u32）。 */
cc_status_t cc_get_param(cc_session_t *s, uint16_t prop_code, uint32_t *value);

/* 设置设备属性值。 */
cc_status_t cc_set_param(cc_session_t *s, uint16_t prop_code, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* CAMCORE_H */
