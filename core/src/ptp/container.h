#ifndef PTP_CONTAINER_H
#define PTP_CONTAINER_H

/*
 * PTP 容器（USB-PTP 12 字节头）。PTP/IP 在容器外再包 8 字节帧，见 docs/PROTOCOL.md。
 */

#include <stdint.h>
#include <stddef.h>
#include "bytebuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PTP_CONTAINER_HEADER_LEN 12
#define PTP_MAX_PARAMS 5

typedef struct ptp_container {
    uint32_t length;        /* 总长度（含 12 字节头） */
    uint16_t type;          /* 1=Command 2=Data 3=Response 4=Event */
    uint16_t code;          /* 操作码 / 响应码 / 事件码 / 属性码 */
    uint32_t transaction;   /* TransactionID */
} ptp_container_t;

/* 从 bytebuf 当前读位置解析 12 字节容器头。 */
int ptp_container_read_header(ptp_buf_t *b, ptp_container_t *c);

/* 向 bytebuf 写入 12 字节容器头。 */
int ptp_container_write_header(ptp_buf_t *b, uint32_t length, uint16_t type,
                               uint16_t code, uint32_t transaction);

/* 构造一个 Command 容器（含参数），结果写入 b。返回 0 成功。 */
int ptp_container_build_command(ptp_buf_t *b, uint16_t code, uint32_t transaction,
                                const uint32_t *params, size_t nparams);

/* 构造一个 Data 容器（含 payload），结果写入 b。 */
int ptp_container_build_data(ptp_buf_t *b, uint16_t code, uint32_t transaction,
                             const void *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* PTP_CONTAINER_H */
