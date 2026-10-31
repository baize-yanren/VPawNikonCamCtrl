#ifndef PTPIP_TRANSPORT_H
#define PTPIP_TRANSPORT_H

/*
 * PTP/IP transport 适配器。
 *
 * 把两条底层「字节流 transport」（命令连接 + 事件连接，均已建立连接）包装成一个
 * 实现 PTP/IP 握手与帧编解码的 transport，向上仍暴露「裸 PTP 容器字节流」，
 * 因此核心 cc_transact / cc_connect 无需感知 PTP/IP 差异。
 *
 * 底层字节流（TCP socket 封装）由平台适配层实现（Android JNI / HarmonyOS NAPI），
 * 或由测试用内存 FIFO 提供。本适配器不拥有底层字节流，free 时亦不释放它们。
 *
 * License: MIT
 */

#include "camcore.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cc_ptpip_streams {
    cc_transport_t *command; /* 命令连接字节流（已 connect） */
    cc_transport_t *event;   /* 事件连接字节流（已 connect，可为 NULL 降级） */
} cc_ptpip_streams_t;

/* 构造 PTP/IP transport。handshake_delay_ms 为尼康 350ms quirk（测试传 0）。 */
cc_transport_t *cc_ptpip_transport_new(const cc_ptpip_streams_t *streams,
                                       int handshake_delay_ms);

/* 释放 PTP/IP transport（不释放底层字节流）。 */
void cc_ptpip_transport_free(cc_transport_t *t);

/* 事件通道握手是否成功（失败时降级为仅命令通道，返回 0）。 */
int cc_ptpip_transport_event_ok(const cc_transport_t *t);

#ifdef __cplusplus
}
#endif

#endif /* PTPIP_TRANSPORT_H */
