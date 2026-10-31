#include "mock_camera.h"

#include "../src/ptp/ptp_constants.h"
#include "../src/ptp/bytebuf.h"
#include "../src/ptp/container.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct mock_link {
    /* host -> camera */
    uint8_t *in;
    size_t in_len, in_cap;
    /* camera -> host */
    uint8_t *out;
    size_t out_len, out_pos, out_cap;
    /* 状态 */
    int session_open;
    int pending_event;
};

static void mock_ensure(void **p, size_t *cap, size_t need) {
    if (*cap >= need) return;
    size_t ncap = *cap ? *cap : 256;
    while (ncap < need) ncap *= 2;
    uint8_t *np = (uint8_t *)realloc(*p, ncap);
    if (np) { *p = np; *cap = ncap; }
}

static void mock_out_put(mock_link_t *m, const uint8_t *data, size_t len) {
    mock_ensure((void **)&m->out, &m->out_cap, m->out_len + len);
    memcpy(m->out + m->out_len, data, len);
    m->out_len += len;
}

static void mock_out_u8(mock_link_t *m, uint8_t v) { mock_out_put(m, &v, 1); }
static void mock_out_u16(mock_link_t *m, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
    mock_out_put(m, b, 2);
}
static void mock_out_u32(mock_link_t *m, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8),
                    (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    mock_out_put(m, b, 4);
}

/* 写一个容器头（12 字节小端）。 */
static void mock_out_header(mock_link_t *m, uint32_t len, uint16_t type, uint16_t code, uint32_t xid) {
    mock_out_u32(m, len);
    mock_out_u16(m, type);
    mock_out_u16(m, code);
    mock_out_u32(m, xid);
}

/* 写一个 PTP 字符串。 */
static void mock_out_string(mock_link_t *m, const char *s) {
    size_t n = strlen(s);
    mock_out_u8(m, (uint8_t)(n + 1));
    for (size_t i = 0; i < n; i++) {
        mock_out_u16(m, (uint16_t)(uint8_t)s[i]);
    }
    mock_out_u16(m, 0); /* 终止符 */
}

/* 可预测的对象字节（用于续传正确性验证）。 */
static uint8_t mock_byte(uint32_t handle, uint32_t offset) {
    return (uint8_t)((offset * 7u + handle + 13u) & 0xFFu);
}

static uint32_t mock_object_size(uint32_t handle) {
    if (handle == MOCK_HANDLE_JPG) return MOCK_JPG_SIZE;
    if (handle == MOCK_HANDLE_NEF) return MOCK_NEF_SIZE;
    return 0;
}

static const char *mock_object_name(uint32_t handle) {
    if (handle == MOCK_HANDLE_JPG) return "DSC_0001.JPG";
    if (handle == MOCK_HANDLE_NEF) return "DSC_0001.NEF";
    return "UNKNOWN";
}

/* 发送一个 Data 容器 + Response(OK)。 */
static void mock_send_object_data(mock_link_t *m, uint32_t handle, uint32_t offset,
                                  uint32_t length, uint32_t xid) {
    uint32_t total = (uint32_t)length;
    mock_out_header(m, 12 + total, PTP_TYPE_DATA, 0x1009, xid);
    for (uint32_t i = 0; i < total; i++) {
        mock_out_u8(m, mock_byte(handle, offset + i));
    }
    mock_out_header(m, 12, PTP_TYPE_RESPONSE, PTP_RC_OK, xid);
}

static void mock_send_ok(mock_link_t *m, uint32_t xid) {
    mock_out_header(m, 12, PTP_TYPE_RESPONSE, PTP_RC_OK, xid);
}

/* 处理 in_buf 中累积的一个事务，生成响应到 out_buf。 */
static void mock_process(mock_link_t *m) {
    if (m->in_len < 12) return; /* 不足一个容器头 */

    ptp_buf_t b;
    ptp_buf_init_read(&b, m->in, m->in_len);
    ptp_container_t c;
    if (ptp_container_read_header(&b, &c) != 0) return;

    /* 读取参数 */
    uint32_t params[3] = {0, 0, 0};
    size_t nparams = (c.length >= 12) ? (c.length - 12) / 4 : 0;
    if (nparams > 3) nparams = 3;
    for (size_t i = 0; i < nparams; i++) ptp_buf_get_u32(&b, &params[i]);

    uint32_t xid = c.transaction;
    uint16_t opcode = c.code;

    switch (opcode) {
        case PTP_OC_OpenSession:
            m->session_open = 1;
            mock_send_ok(m, xid);
            break;
        case PTP_OC_CloseSession:
            m->session_open = 0;
            mock_send_ok(m, xid);
            break;
        case PTP_OC_GetStorageIDs: {
            uint32_t sid = 0x00010001u;
            mock_out_header(m, 12 + 8, PTP_TYPE_DATA, opcode, xid);
            mock_out_u32(m, 1);      /* count */
            mock_out_u32(m, sid);    /* 单个存储卷 */
            mock_send_ok(m, xid);
            break;
        }
        case PTP_OC_GetObjectHandles: {
            uint32_t hs[2] = {MOCK_HANDLE_JPG, MOCK_HANDLE_NEF};
            mock_out_header(m, 12 + 4 + 8, PTP_TYPE_DATA, opcode, xid);
            mock_out_u32(m, 2);
            mock_out_u32(m, hs[0]);
            mock_out_u32(m, hs[1]);
            mock_send_ok(m, xid);
            break;
        }
        case PTP_OC_GetObjectInfo: {
            uint32_t handle = params[0];
            uint32_t size = mock_object_size(handle);
            const char *name = mock_object_name(handle);
            /* payload 长度：先算字符串长度 */
            size_t plen = 0;
            plen += 4 + 2 + 2 + 4;   /* storageId, format, protection, size */
            plen += 2 + 4 + 4 + 4;   /* thumbFormat, thumbSize, thumbW, thumbH */
            plen += 4 + 4 + 4;       /* imgW, imgH, bitDepth */
            plen += 4 + 2 + 4 + 4;   /* parent, assocType, assocDesc, seq */
            plen += 1 + (strlen(name) + 1) * 2; /* filename */
            plen += 1 + 2;           /* captureDate: 空串 */
            plen += 1 + 2;           /* modificationDate: 空串 */

            mock_out_header(m, (uint32_t)(12 + plen), PTP_TYPE_DATA, opcode, xid);
            mock_out_u32(m, 0x00010001u); /* storageId */
            mock_out_u16(m, 0x3801);      /* format = JPEG */
            mock_out_u16(m, 0);           /* protectionStatus */
            mock_out_u32(m, size);
            mock_out_u16(m, 0);           /* thumbFormat */
            mock_out_u32(m, 0);
            mock_out_u32(m, 0);
            mock_out_u32(m, 0);
            mock_out_u32(m, 0);
            mock_out_u32(m, 0);
            mock_out_u32(m, 0);
            mock_out_u32(m, 0);           /* parent */
            mock_out_u16(m, 0);           /* associationType */
            mock_out_u32(m, 0);
            mock_out_u32(m, 0);
            mock_out_string(m, name);
            mock_out_string(m, "");
            mock_out_string(m, "");
            mock_send_ok(m, xid);
            break;
        }
        case PTP_OC_GetObject: {
            uint32_t handle = params[0];
            mock_send_object_data(m, handle, 0, mock_object_size(handle), xid);
            break;
        }
        case PTP_OC_GetPartialObject: {
            uint32_t handle = params[0];
            uint32_t offset = params[1];
            uint32_t length = params[2];
            mock_out_header(m, 12 + length, PTP_TYPE_DATA, opcode, xid);
            for (uint32_t i = 0; i < length; i++) {
                mock_out_u8(m, mock_byte(handle, offset + i));
            }
            mock_send_ok(m, xid);
            break;
        }
        case PTP_OC_GetDevicePropValue: {
            uint16_t prop = (uint16_t)params[0];
            uint32_t val = 0;
            if (prop == PTP_DPC_ExposureTime) val = 10000; /* 1s */
            else if (prop == PTP_DPC_FNumber) val = 800;   /* f/8 */
            else if (prop == PTP_DPC_ExposureIndex) val = 100;
            mock_out_header(m, 12 + 4, PTP_TYPE_DATA, opcode, xid);
            mock_out_u32(m, val);
            mock_send_ok(m, xid);
            break;
        }
        case PTP_OC_SetDevicePropValue: {
            /* data-out 事务：command 后跟一个 Data 容器，这里直接忽略其内容 */
            mock_send_ok(m, xid);
            break;
        }
        case NK_OC_CaptureToSdram:
            m->pending_event = 1;
            mock_send_ok(m, xid);
            break;
        case NK_OC_CheckEvent: {
            mock_out_header(m, 12 + 2 + 6, PTP_TYPE_DATA, opcode, xid);
            mock_out_u16(m, m->pending_event ? 1 : 0);
            if (m->pending_event) {
                mock_out_u16(m, NK_EC_ObjectAddedInSDRAM);
                mock_out_u32(m, MOCK_HANDLE_JPG);
                m->pending_event = 0;
            }
            mock_send_ok(m, xid);
            break;
        }
        case NK_OC_StartLiveView:
        case NK_OC_EndLiveView:
        case NK_OC_SetControlMode:
        case NK_OC_TerminateCapture:
            mock_send_ok(m, xid);
            break;
        case NK_OC_GetLiveViewImg: {
            /* 返回一个伪 JPEG（最小 FFD8...FFD9） */
            static const uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x00, 0xFF, 0xD9};
            mock_out_header(m, 12 + sizeof(jpeg), PTP_TYPE_DATA, opcode, xid);
            mock_out_put(m, jpeg, sizeof(jpeg));
            mock_send_ok(m, xid);
            break;
        }
        default:
            mock_out_header(m, 12, PTP_TYPE_RESPONSE, PTP_RC_OperationNotSupported, xid);
            break;
    }

    /* 消费已处理的输入 */
    m->in_len = 0;
}

/* ------------------------------------------------------------------ */
/* transport ops                                                        */
/* ------------------------------------------------------------------ */
static int mock_send(void *ctx, const uint8_t *data, size_t len) {
    mock_link_t *m = (mock_link_t *)ctx;
    mock_ensure((void **)&m->in, &m->in_cap, m->in_len + len);
    memcpy(m->in + m->in_len, data, len);
    m->in_len += len;
    return (int)len;
}

static int mock_recv(void *ctx, uint8_t *data, size_t len) {
    mock_link_t *m = (mock_link_t *)ctx;
    if (m->out_pos >= m->out_len) {
        /* 无待读数据：处理累积的输入，生成响应 */
        mock_process(m);
    }
    size_t avail = m->out_len - m->out_pos;
    size_t n = avail < len ? avail : len;
    memcpy(data, m->out + m->out_pos, n);
    m->out_pos += n;
    return (int)n;
}

static int mock_connect(void *ctx, const void *params) {
    (void)ctx; (void)params;
    return 0;
}

static int mock_disconnect(void *ctx) {
    (void)ctx;
    return 0;
}

static int mock_set_timeout(void *ctx, int timeout_ms) {
    (void)ctx; (void)timeout_ms;
    return 0;
}

static const cc_transport_ops_t k_mock_ops = {
    mock_connect, mock_disconnect, mock_send, mock_recv, mock_set_timeout,
};

cc_transport_t *mock_create(void) {
    mock_link_t *m = (mock_link_t *)calloc(1, sizeof(mock_link_t));
    if (!m) return NULL;
    cc_transport_t *t = (cc_transport_t *)calloc(1, sizeof(cc_transport_t));
    if (!t) { free(m); return NULL; }
    t->ops = &k_mock_ops;
    t->ctx = m;
    t->kind = CC_TRANSPORT_USB;
    return t;
}

void mock_destroy(cc_transport_t *t) {
    if (!t) return;
    mock_link_t *m = (mock_link_t *)t->ctx;
    if (m) {
        free(m->in);
        free(m->out);
        free(m);
    }
    free(t);
}
