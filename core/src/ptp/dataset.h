#ifndef PTP_DATASET_H
#define PTP_DATASET_H

/*
 * PTP 数据集解析辅助：字符串（u8 长度 + UTF-16LE）、u16/u32 数组。
 */

#include <stdint.h>
#include <stddef.h>
#include "bytebuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 从 bytebuf 读取 PTP 字符串（u8 count + count*u16 UTF-16LE），转 UTF-8 写入 dst。
 * 返回 0 成功，-1 失败。 */
int ptp_read_string(ptp_buf_t *b, char *dst, size_t dst_cap);

/* 从 bytebuf 读取 u32 数组（u32 count + count*u32），写入 dst（最多 max 个）。
 * 返回实际元素数，失败返回 -1。 */
int ptp_read_u32_array(ptp_buf_t *b, uint32_t *dst, size_t max);

/* 从 bytebuf 读取 u16 数组（u32 count + count*u16）。 */
int ptp_read_u16_array(ptp_buf_t *b, uint16_t *dst, size_t max);

/* 跳过 PTP 字符串。 */
int ptp_skip_string(ptp_buf_t *b);

#ifdef __cplusplus
}
#endif

#endif /* PTP_DATASET_H */
