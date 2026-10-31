#include "mock_ptp_core.h"

#include "../src/ptp/ptp_constants.h"
#include "../src/ptp/bytebuf.h"
#include "../src/ptp/container.h"

#include <stdlib.h>
#include <string.h>

/* 输出累积缓冲 */
typedef struct outbuf {
    uint8_t *data;
    size_t len, cap;
} outbuf_t;

static void ob_ensure(outbuf_t *o, size_t need) {
    if (o->cap >= need) return;
    size_t ncap = o->cap ? o->cap : 256;
    while (ncap < need) ncap *= 2;
    uint8_t *np = (uint8_t *)realloc(o->data, ncap);
    if (np) {
        o->data = np;
        o->cap = ncap;
    }
}

static void ob_put(outbuf_t *o, const uint8_t *data, size_t len) {
    ob_ensure(o, o->len + len);
    memcpy(o->data + o->len, data, len);
    o->len += len;
}

static void ob_u8(outbuf_t *o, uint8_t v) { ob_put(o, &v, 1); }

static void ob_u16(outbuf_t *o, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
    ob_put(o, b, 2);
}

static void ob_u32(outbuf_t *o, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8),
                    (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    ob_put(o, b, 4);
}

static void ob_header(outbuf_t *o, uint32_t len, uint16_t type, uint16_t code, uint32_t xid) {
    ob_u32(o, len);
    ob_u16(o, type);
    ob_u16(o, code);
    ob_u32(o, xid);
}

static void ob_string(outbuf_t *o, const char *s) {
    size_t n = strlen(s);
    ob_u8(o, (uint8_t)(n + 1));
    for (size_t i = 0; i < n; i++) ob_u16(o, (uint16_t)(uint8_t)s[i]);
    ob_u16(o, 0);
}

static void ob_ok(outbuf_t *o, uint32_t xid) {
    ob_header(o, 12, PTP_TYPE_RESPONSE, PTP_RC_OK, xid);
}

/* 可预测的对象字节（用于续传/下载正确性验证）。 */
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

static void send_object_data(outbuf_t *o, uint32_t handle, uint32_t offset,
                             uint32_t length, uint32_t xid) {
    uint32_t total = length;
    ob_header(o, 12 + total, PTP_TYPE_DATA, PTP_OC_GetObject, xid);
    for (uint32_t i = 0; i < total; i++) {
        ob_u8(o, mock_byte(handle, offset + i));
    }
    ob_ok(o, xid);
}

void mock_ptp_process(mock_cam_t *cam, mock_emit_fn emit, void *emit_ctx,
                      const uint8_t *container, size_t container_len) {
    outbuf_t o = {NULL, 0, 0};
    if (container_len < PTP_CONTAINER_HEADER_LEN) return;

    ptp_buf_t b;
    ptp_buf_init_read(&b, container, container_len);
    ptp_container_t c;
    if (ptp_container_read_header(&b, &c) != 0) return;

    uint32_t params[3] = {0, 0, 0};
    size_t nparams = (c.length >= PTP_CONTAINER_HEADER_LEN)
                         ? (c.length - PTP_CONTAINER_HEADER_LEN) / 4
                         : 0;
    if (nparams > 3) nparams = 3;
    for (size_t i = 0; i < nparams; i++) ptp_buf_get_u32(&b, &params[i]);

    uint32_t xid = c.transaction;
    uint16_t opcode = c.code;

    switch (opcode) {
        case PTP_OC_OpenSession:
            cam->session_open = 1;
            ob_ok(&o, xid);
            break;
        case PTP_OC_CloseSession:
            cam->session_open = 0;
            ob_ok(&o, xid);
            break;
        case PTP_OC_GetStorageIDs: {
            uint32_t sid = 0x00010001u;
            ob_header(&o, 12 + 8, PTP_TYPE_DATA, opcode, xid);
            ob_u32(&o, 1);   /* count */
            ob_u32(&o, sid); /* 单个存储卷 */
            ob_ok(&o, xid);
            break;
        }
        case PTP_OC_GetObjectHandles: {
            uint32_t hs[2] = {MOCK_HANDLE_JPG, MOCK_HANDLE_NEF};
            ob_header(&o, 12 + 4 + 8, PTP_TYPE_DATA, opcode, xid);
            ob_u32(&o, 2);
            ob_u32(&o, hs[0]);
            ob_u32(&o, hs[1]);
            ob_ok(&o, xid);
            break;
        }
        case PTP_OC_GetObjectInfo: {
            uint32_t handle = params[0];
            uint32_t size = mock_object_size(handle);
            const char *name = mock_object_name(handle);
            size_t plen = 0;
            plen += 4 + 2 + 2 + 4;
            plen += 2 + 4 + 4 + 4;
            plen += 4 + 4 + 4;
            plen += 4 + 2 + 4 + 4;
            plen += 1 + (strlen(name) + 1) * 2;
            plen += 1 + 2; /* captureDate 空串 */
            plen += 1 + 2; /* modificationDate 空串 */

            ob_header(&o, (uint32_t)(12 + plen), PTP_TYPE_DATA, opcode, xid);
            ob_u32(&o, 0x00010001u); /* storageId */
            ob_u16(&o, 0x3801);      /* format = JPEG */
            ob_u16(&o, 0);           /* protectionStatus */
            ob_u32(&o, size);
            ob_u16(&o, 0); /* thumbFormat */
            ob_u32(&o, 0);
            ob_u32(&o, 0);
            ob_u32(&o, 0);
            ob_u32(&o, 0);
            ob_u32(&o, 0);
            ob_u32(&o, 0);
            ob_u32(&o, 0); /* parent */
            ob_u16(&o, 0); /* associationType */
            ob_u32(&o, 0);
            ob_u32(&o, 0);
            ob_string(&o, name);
            ob_string(&o, "");
            ob_string(&o, "");
            ob_ok(&o, xid);
            break;
        }
        case PTP_OC_GetObject: {
            uint32_t handle = params[0];
            send_object_data(&o, handle, 0, mock_object_size(handle), xid);
            break;
        }
        case PTP_OC_GetPartialObject: {
            uint32_t handle = params[0];
            uint32_t offset = params[1];
            uint32_t length = params[2];
            ob_header(&o, 12 + length, PTP_TYPE_DATA, opcode, xid);
            for (uint32_t i = 0; i < length; i++) {
                ob_u8(&o, mock_byte(handle, offset + i));
            }
            ob_ok(&o, xid);
            break;
        }
        case PTP_OC_GetDevicePropValue: {
            uint16_t prop = (uint16_t)params[0];
            uint32_t val = 0;
            if (prop == PTP_DPC_ExposureTime) val = 10000; /* 1s */
            else if (prop == PTP_DPC_FNumber) val = 800;   /* f/8 */
            else if (prop == PTP_DPC_ExposureIndex) val = 100;
            ob_header(&o, 12 + 4, PTP_TYPE_DATA, opcode, xid);
            ob_u32(&o, val);
            ob_ok(&o, xid);
            break;
        }
        case PTP_OC_SetDevicePropValue: {
            /* data-out 事务：command 后跟 Data 容器；本层只看到 command，忽略其数据 */
            ob_ok(&o, xid);
            break;
        }
        case NK_OC_CaptureToSdram:
            cam->pending_event = 1;
            ob_ok(&o, xid);
            break;
        case NK_OC_CheckEvent: {
            ob_header(&o, 12 + 2 + 6, PTP_TYPE_DATA, opcode, xid);
            ob_u16(&o, cam->pending_event ? 1 : 0);
            if (cam->pending_event) {
                ob_u16(&o, NK_EC_ObjectAddedInSDRAM);
                ob_u32(&o, MOCK_HANDLE_JPG);
                cam->pending_event = 0;
            }
            ob_ok(&o, xid);
            break;
        }
        case NK_OC_StartLiveView:
        case NK_OC_EndLiveView:
        case NK_OC_SetControlMode:
        case NK_OC_TerminateCapture:
            ob_ok(&o, xid);
            break;
        case NK_OC_GetLiveViewImg: {
            static const uint8_t jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x00, 0xFF, 0xD9};
            ob_header(&o, 12 + sizeof(jpeg), PTP_TYPE_DATA, opcode, xid);
            ob_put(&o, jpeg, sizeof(jpeg));
            ob_ok(&o, xid);
            break;
        }
        default:
            ob_header(&o, 12, PTP_TYPE_RESPONSE, PTP_RC_OperationNotSupported, xid);
            break;
    }

    if (o.len > 0 && emit) emit(emit_ctx, o.data, o.len);
    free(o.data);
}
