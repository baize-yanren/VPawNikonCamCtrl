#include "dataset.h"

#include <stdint.h>
#include <stddef.h>

/* UTF-16LE 码元追加为 UTF-8（仅处理 BMP，够用于机型名/文件名）。 */
static int append_utf8(char *dst, size_t dst_cap, size_t *len, uint32_t cp) {
    char buf[4];
    int n = 0;
    if (cp < 0x80) {
        buf[n++] = (char)cp;
    } else if (cp < 0x800) {
        buf[n++] = (char)(0xC0 | (cp >> 6));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[n++] = (char)(0xE0 | (cp >> 12));
        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[n++] = (char)(0x80 | (cp & 0x3F));
    }
    if (*len + n + 1 > dst_cap) return -1;
    for (int i = 0; i < n; i++) dst[(*len)++] = buf[i];
    dst[*len] = '\0';
    return 0;
}

int ptp_read_string(ptp_buf_t *b, char *dst, size_t dst_cap) {
    uint8_t count;
    if (ptp_buf_get_u8(b, &count) != 0) return -1;
    size_t written = 0;
    dst[0] = '\0';
    for (int i = 0; i < count; i++) {
        uint16_t unit;
        if (ptp_buf_get_u16(b, &unit) != 0) return -1;
        if (unit == 0) continue; /* 终止符 */
        if (append_utf8(dst, dst_cap, &written, unit) != 0) return -1;
    }
    return 0;
}

int ptp_skip_string(ptp_buf_t *b) {
    uint8_t count;
    if (ptp_buf_get_u8(b, &count) != 0) return -1;
    if (ptp_buf_skip(b, (size_t)count * 2) != 0) return -1;
    return 0;
}

int ptp_read_u32_array(ptp_buf_t *b, uint32_t *dst, size_t max) {
    uint32_t count;
    if (ptp_buf_get_u32(b, &count) != 0) return -1;
    size_t n = count < max ? (size_t)count : max;
    for (size_t i = 0; i < n; i++) {
        if (ptp_buf_get_u32(b, &dst[i]) != 0) return -1;
    }
    /* 跳过超出 max 的部分 */
    for (size_t i = n; i < (size_t)count; i++) {
        uint32_t tmp;
        if (ptp_buf_get_u32(b, &tmp) != 0) return -1;
    }
    return (int)n;
}

int ptp_read_u16_array(ptp_buf_t *b, uint16_t *dst, size_t max) {
    uint32_t count;
    if (ptp_buf_get_u32(b, &count) != 0) return -1;
    size_t n = count < max ? (size_t)count : max;
    for (size_t i = 0; i < n; i++) {
        if (ptp_buf_get_u16(b, &dst[i]) != 0) return -1;
    }
    for (size_t i = n; i < (size_t)count; i++) {
        uint16_t tmp;
        if (ptp_buf_get_u16(b, &tmp) != 0) return -1;
    }
    return (int)n;
}
