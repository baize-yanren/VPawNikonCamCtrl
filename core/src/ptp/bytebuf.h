#ifndef PTP_BYTEBUF_H
#define PTP_BYTEBUF_H

/*
 * 小端序字节缓冲：用于 PTP 容器与数据集的编解码。
 * 所有多字节整数一律小端序（LE），见 docs/PROTOCOL.md。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ptp_buf {
    uint8_t *data;
    size_t cap;   /* 缓冲区容量 */
    size_t len;   /* 已写入的有效长度 */
    size_t pos;   /* 读取游标 */
} ptp_buf_t;

/* 将外部内存包装为缓冲（不拷贝，不拥有内存）。 */
void ptp_buf_wrap(ptp_buf_t *b, uint8_t *data, size_t cap);

/* 将已有数据包装为只读缓冲（len 置为 data 长度，pos 置 0）。 */
void ptp_buf_init_read(ptp_buf_t *b, const uint8_t *data, size_t len);

/* 重置读写游标（数据长度清零）。 */
void ptp_buf_reset(ptp_buf_t *b);

/* 剩余可写 / 可读字节数。 */
size_t ptp_buf_avail_write(const ptp_buf_t *b);
size_t ptp_buf_avail_read(const ptp_buf_t *b);

/* 写入：返回 0 成功，-1 空间不足。 */
int ptp_buf_put_u8(ptp_buf_t *b, uint8_t v);
int ptp_buf_put_u16(ptp_buf_t *b, uint16_t v);
int ptp_buf_put_u32(ptp_buf_t *b, uint32_t v);
int ptp_buf_put_u64(ptp_buf_t *b, uint64_t v);
int ptp_buf_put_bytes(ptp_buf_t *b, const void *src, size_t n);

/* 读取：返回 0 成功，-1 数据不足。 */
int ptp_buf_get_u8(ptp_buf_t *b, uint8_t *v);
int ptp_buf_get_u16(ptp_buf_t *b, uint16_t *v);
int ptp_buf_get_u32(ptp_buf_t *b, uint32_t *v);
int ptp_buf_get_u64(ptp_buf_t *b, uint64_t *v);
int ptp_buf_get_bytes(ptp_buf_t *b, void *dst, size_t n);

/* 跳过 n 字节读取。 */
int ptp_buf_skip(ptp_buf_t *b, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PTP_BYTEBUF_H */
