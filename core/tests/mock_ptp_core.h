#ifndef MOCK_PTP_CORE_H
#define MOCK_PTP_CORE_H

/*
 * 共享的 mock PTP 操作处理核心：模拟一台尼康相机的 PTP 语义层，
 * 与传输（USB 容器直通 / PTP/IP 帧封装）解耦。
 *
 * 输入一个完整 PTP command 容器，执行操作，通过 emit 回调输出裸 PTP
 * 响应容器字节（Data 容器 + Response 容器）。USB mock 直接把字节写入
 * FIFO；PTP/IP mock 再把字节编码成 PTP/IP 帧。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mock_cam {
    int session_open;
    int pending_event;
} mock_cam_t;

/* 输出回调：把裸 PTP 容器字节交给传输层。 */
typedef void (*mock_emit_fn)(void *ctx, const uint8_t *data, size_t len);

/* 处理一个完整 PTP command 容器，输出响应。 */
void mock_ptp_process(mock_cam_t *cam, mock_emit_fn emit, void *emit_ctx,
                      const uint8_t *container, size_t container_len);

/* 内部对象句柄与大小（供测试断言）。 */
#define MOCK_HANDLE_JPG 0x10000001u
#define MOCK_HANDLE_NEF 0x10000002u
#define MOCK_JPG_SIZE (10u * 1024u * 1024u) /* 10 MiB，触发分块下载 */
#define MOCK_NEF_SIZE (1024u * 1024u)

#ifdef __cplusplus
}
#endif

#endif /* MOCK_PTP_CORE_H */
