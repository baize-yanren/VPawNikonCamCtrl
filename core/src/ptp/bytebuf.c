#include "bytebuf.h"

#include <string.h>

void ptp_buf_wrap(ptp_buf_t *b, uint8_t *data, size_t cap) {
    b->data = data;
    b->cap = cap;
    b->len = 0;
    b->pos = 0;
}

void ptp_buf_init_read(ptp_buf_t *b, const uint8_t *data, size_t len) {
    b->data = (uint8_t *)data;
    b->cap = len;
    b->len = len;
    b->pos = 0;
}

void ptp_buf_reset(ptp_buf_t *b) {
    b->len = 0;
    b->pos = 0;
}

size_t ptp_buf_avail_write(const ptp_buf_t *b) { return b->cap - b->len; }
size_t ptp_buf_avail_read(const ptp_buf_t *b) { return b->len - b->pos; }

int ptp_buf_put_u8(ptp_buf_t *b, uint8_t v) {
    if (ptp_buf_avail_write(b) < 1) return -1;
    b->data[b->len++] = v;
    return 0;
}

int ptp_buf_put_u16(ptp_buf_t *b, uint16_t v) {
    if (ptp_buf_avail_write(b) < 2) return -1;
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    return 0;
}

int ptp_buf_put_u32(ptp_buf_t *b, uint32_t v) {
    if (ptp_buf_avail_write(b) < 4) return -1;
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 24) & 0xFF);
    return 0;
}

int ptp_buf_put_u64(ptp_buf_t *b, uint64_t v) {
    if (ptp_buf_avail_write(b) < 8) return -1;
    for (int i = 0; i < 8; i++) {
        b->data[b->len++] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
    return 0;
}

int ptp_buf_put_bytes(ptp_buf_t *b, const void *src, size_t n) {
    if (ptp_buf_avail_write(b) < n) return -1;
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

int ptp_buf_get_u8(ptp_buf_t *b, uint8_t *v) {
    if (ptp_buf_avail_read(b) < 1) return -1;
    *v = b->data[b->pos++];
    return 0;
}

int ptp_buf_get_u16(ptp_buf_t *b, uint16_t *v) {
    if (ptp_buf_avail_read(b) < 2) return -1;
    *v = (uint16_t)(b->data[b->pos] | (b->data[b->pos + 1] << 8));
    b->pos += 2;
    return 0;
}

int ptp_buf_get_u32(ptp_buf_t *b, uint32_t *v) {
    if (ptp_buf_avail_read(b) < 4) return -1;
    *v = (uint32_t)(b->data[b->pos] |
                    (b->data[b->pos + 1] << 8) |
                    (b->data[b->pos + 2] << 16) |
                    ((uint32_t)b->data[b->pos + 3] << 24));
    b->pos += 4;
    return 0;
}

int ptp_buf_get_u64(ptp_buf_t *b, uint64_t *v) {
    if (ptp_buf_avail_read(b) < 8) return -1;
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) {
        r |= ((uint64_t)b->data[b->pos + i]) << (i * 8);
    }
    b->pos += 8;
    *v = r;
    return 0;
}

int ptp_buf_get_bytes(ptp_buf_t *b, void *dst, size_t n) {
    if (ptp_buf_avail_read(b) < n) return -1;
    memcpy(dst, b->data + b->pos, n);
    b->pos += n;
    return 0;
}

int ptp_buf_skip(ptp_buf_t *b, size_t n) {
    if (ptp_buf_avail_read(b) < n) return -1;
    b->pos += n;
    return 0;
}
