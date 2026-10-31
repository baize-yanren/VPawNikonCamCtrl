#include "mock_ptpip_camera.h"
#include "mock_ptp_core.h"

#include "../src/ptpip/ptpip.h"
#include "../src/ptp/ptp_constants.h"
#include "../src/ptp/container.h"
#include "../src/ptp/bytebuf.h"

#include <stdlib.h>
#include <string.h>

/* 服务端 Data 帧分片大小（模拟真实相机的多帧传输）。 */
#define PTPIP_MOCK_DATA_CHUNK (64u * 1024u)

typedef struct ptpip_mock {
    /* 命令连接 FIFO */
    uint8_t *cmd_in;
    size_t cmd_in_len, cmd_in_pos, cmd_in_cap;
    uint8_t *cmd_out;
    size_t cmd_out_len, cmd_out_pos, cmd_out_cap;
    /* 事件连接 FIFO */
    uint8_t *evt_in;
    size_t evt_in_len, evt_in_pos, evt_in_cap;
    uint8_t *evt_out;
    size_t evt_out_len, evt_out_pos, evt_out_cap;
    /* PTP 语义状态 */
    mock_cam_t cam;
    /* mock_ptp_core 输出累积（裸容器字节，待编码成帧） */
    uint8_t *ptp_out;
    size_t ptp_out_len, ptp_out_pos, ptp_out_cap;
    uint32_t conn_number;
} ptpip_mock_t;

typedef struct ptpip_link {
    ptpip_mock_t *m;
    int is_event;
} ptpip_link_t;

static void ensure_buf(void **p, size_t *cap, size_t need) {
    if (*cap >= need) return;
    size_t ncap = *cap ? *cap : 256;
    while (ncap < need) ncap *= 2;
    uint8_t *np = (uint8_t *)realloc(*p, ncap);
    if (np) {
        *p = np;
        *cap = ncap;
    }
}

static void out_put(uint8_t **p, size_t *len, size_t *cap, const uint8_t *data, size_t n) {
    ensure_buf((void **)p, cap, *len + n);
    memcpy(*p + *len, data, n);
    *len += n;
}

static int out_read(uint8_t *buf, size_t *pos, size_t len, uint8_t *dst, size_t want) {
    size_t avail = len - *pos;
    if (avail == 0) return 0;
    size_t n = avail < want ? avail : want;
    memcpy(dst, buf + *pos, n);
    *pos += n;
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* 帧发送（服务端 -> 客户端）                                            */
/* ------------------------------------------------------------------ */
static void send_init_cmd_ack(ptpip_mock_t *m) {
    static const uint8_t cam_guid[PTPIP_GUID_LEN] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    };
    const char *name = "MockNikon";
    size_t n = strlen(name);
    uint8_t buf[256];
    ptp_buf_t b;
    ptp_buf_wrap(&b, buf, sizeof(buf));
    uint32_t length = (uint32_t)(PTPIP_FRAME_HEADER_LEN + 4 + PTPIP_GUID_LEN + (n + 1) * 2);
    ptp_buf_put_u32(&b, length);
    ptp_buf_put_u32(&b, PTPIP_INIT_CMD_ACK);
    ptp_buf_put_u32(&b, 1); /* connectionNumber */
    ptp_buf_put_bytes(&b, cam_guid, PTPIP_GUID_LEN);
    for (size_t i = 0; i < n; i++) ptp_buf_put_u16(&b, (uint16_t)(uint8_t)name[i]);
    ptp_buf_put_u16(&b, 0);
    out_put(&m->cmd_out, &m->cmd_out_len, &m->cmd_out_cap, buf, b.len);
    m->conn_number = 1;
}

static void send_init_evt_ack(ptpip_mock_t *m) {
    uint8_t buf[8];
    ptp_buf_t b;
    ptp_buf_wrap(&b, buf, sizeof(buf));
    ptp_buf_put_u32(&b, 8);
    ptp_buf_put_u32(&b, PTPIP_INIT_EVT_ACK);
    out_put(&m->evt_out, &m->evt_out_len, &m->evt_out_cap, buf, 8);
}

/* mock_ptp_core 输出回调：累积裸容器字节。 */
static void mock_emit(void *ctx, const uint8_t *data, size_t len) {
    ptpip_mock_t *m = (ptpip_mock_t *)ctx;
    ensure_buf((void **)&m->ptp_out, &m->ptp_out_cap, m->ptp_out_len + len);
    memcpy(m->ptp_out + m->ptp_out_len, data, len);
    m->ptp_out_len += len;
}

/* 把 ptp_out 累积的裸容器编码成帧写入 cmd_out。 */
static void flush_ptp_out(ptpip_mock_t *m) {
    while (m->ptp_out_len - m->ptp_out_pos >= PTP_CONTAINER_HEADER_LEN) {
        ptp_buf_t b;
        ptp_buf_init_read(&b, m->ptp_out + m->ptp_out_pos, m->ptp_out_len - m->ptp_out_pos);
        uint32_t clen = 0;
        uint16_t ctype = 0, code = 0;
        uint32_t xid = 0;
        ptp_buf_get_u32(&b, &clen);
        ptp_buf_get_u16(&b, &ctype);
        ptp_buf_get_u16(&b, &code);
        ptp_buf_get_u32(&b, &xid);
        if (m->ptp_out_len - m->ptp_out_pos < clen) break; /* 不完整 */

        const uint8_t *payload = m->ptp_out + m->ptp_out_pos + PTP_CONTAINER_HEADER_LEN;
        size_t plen = clen - PTP_CONTAINER_HEADER_LEN;

        if (ctype == PTP_TYPE_RESPONSE) {
            /* 解析 params */
            uint32_t p[5] = {0, 0, 0, 0, 0};
            size_t n = 0;
            ptp_buf_t pb;
            ptp_buf_init_read(&pb, payload, plen);
            while (ptp_buf_avail_read(&pb) >= 4 && n < 5) {
                ptp_buf_get_u32(&pb, &p[n]);
                n++;
            }
            uint8_t buf[64];
            ptp_buf_t fb;
            ptp_buf_wrap(&fb, buf, sizeof(buf));
            ptpip_build_operation_response(&fb, code, xid, p, n);
            out_put(&m->cmd_out, &m->cmd_out_len, &m->cmd_out_cap, buf, fb.len);
        } else if (ctype == PTP_TYPE_DATA) {
            /* StartData + [Data]* + EndData */
            uint8_t hdr[20];
            ptp_buf_t fb;
            ptp_buf_wrap(&fb, hdr, sizeof(hdr));
            ptpip_build_start_data(&fb, xid, (uint64_t)plen);
            out_put(&m->cmd_out, &m->cmd_out_len, &m->cmd_out_cap, hdr, fb.len);

            size_t off = 0;
            while (off < plen) {
                size_t chunk = plen - off;
                if (chunk > PTPIP_MOCK_DATA_CHUNK) chunk = PTPIP_MOCK_DATA_CHUNK;
                uint8_t *dbuf = (uint8_t *)malloc(chunk + 16);
                if (!dbuf) break;
                ptp_buf_t db;
                ptp_buf_wrap(&db, dbuf, chunk + 16);
                ptpip_build_data(&db, xid, payload + off, chunk);
                out_put(&m->cmd_out, &m->cmd_out_len, &m->cmd_out_cap, dbuf, db.len);
                free(dbuf);
                off += chunk;
            }
            ptp_buf_wrap(&fb, hdr, sizeof(hdr));
            ptpip_build_end_data(&fb, xid, NULL, 0);
            out_put(&m->cmd_out, &m->cmd_out_len, &m->cmd_out_cap, hdr, fb.len);
        }

        m->ptp_out_pos += clen;
    }
    if (m->ptp_out_pos == m->ptp_out_len) {
        m->ptp_out_pos = m->ptp_out_len = 0;
    }
}

/* ------------------------------------------------------------------ */
/* 帧接收（客户端 -> 服务端）                                            */
/* ------------------------------------------------------------------ */
static void process_cmd(ptpip_mock_t *m) {
    while (m->cmd_in_len - m->cmd_in_pos >= PTPIP_FRAME_HEADER_LEN) {
        uint8_t hdr[8];
        memcpy(hdr, m->cmd_in + m->cmd_in_pos, 8);
        ptp_buf_t b;
        ptp_buf_init_read(&b, hdr, 8);
        uint32_t length = 0, type = 0;
        ptp_buf_get_u32(&b, &length);
        ptp_buf_get_u32(&b, &type);
        if (length < 8) break;
        if (m->cmd_in_len - m->cmd_in_pos < length) break; /* 帧不完整 */

        const uint8_t *payload = m->cmd_in + m->cmd_in_pos + PTPIP_FRAME_HEADER_LEN;
        size_t plen = length - PTPIP_FRAME_HEADER_LEN;

        switch (type) {
            case PTPIP_INIT_CMD_REQ:
                send_init_cmd_ack(m);
                break;
            case PTPIP_OPERATION_REQ: {
                uint16_t opcode = 0;
                uint32_t xid = 0, data_phase = 0;
                uint32_t params[5] = {0, 0, 0, 0, 0};
                size_t n = 0;
                ptpip_parse_operation_request(payload, plen, &opcode, &xid, &data_phase, params, &n);

                /* 重构成标准 command 容器交给 mock_ptp_core */
                uint8_t cbuf[12 + 5 * 4];
                ptp_buf_t cb;
                ptp_buf_wrap(&cb, cbuf, sizeof(cbuf));
                ptp_container_build_command(&cb, opcode, xid, params, n);
                mock_ptp_process(&m->cam, mock_emit, m, cbuf, cb.len);
                flush_ptp_out(m);
                break;
            }
            case PTPIP_START_DATA:
            case PTPIP_DATA:
            case PTPIP_END_DATA:
                /* data-out：mock 的 SetDevicePropValue 已忽略数据，这里直接丢弃 */
                break;
            default:
                break;
        }

        m->cmd_in_pos += length;
    }
    if (m->cmd_in_pos == m->cmd_in_len) {
        m->cmd_in_pos = m->cmd_in_len = 0;
    }
}

static void process_evt(ptpip_mock_t *m) {
    while (m->evt_in_len - m->evt_in_pos >= PTPIP_FRAME_HEADER_LEN) {
        uint8_t hdr[8];
        memcpy(hdr, m->evt_in + m->evt_in_pos, 8);
        ptp_buf_t b;
        ptp_buf_init_read(&b, hdr, 8);
        uint32_t length = 0, type = 0;
        ptp_buf_get_u32(&b, &length);
        ptp_buf_get_u32(&b, &type);
        if (length < 8) break;
        if (m->evt_in_len - m->evt_in_pos < length) break;

        if (type == PTPIP_INIT_EVT_REQ) {
            send_init_evt_ack(m);
        }
        m->evt_in_pos += length;
    }
    if (m->evt_in_pos == m->evt_in_len) {
        m->evt_in_pos = m->evt_in_len = 0;
    }
}

/* ------------------------------------------------------------------ */
/* transport ops                                                        */
/* ------------------------------------------------------------------ */
static int link_send(void *ctx, const uint8_t *data, size_t len) {
    ptpip_link_t *l = (ptpip_link_t *)ctx;
    ptpip_mock_t *m = l->m;
    if (l->is_event) {
        out_put(&m->evt_in, &m->evt_in_len, &m->evt_in_cap, data, len);
    } else {
        out_put(&m->cmd_in, &m->cmd_in_len, &m->cmd_in_cap, data, len);
    }
    return (int)len;
}

static int link_recv(void *ctx, uint8_t *data, size_t len) {
    ptpip_link_t *l = (ptpip_link_t *)ctx;
    ptpip_mock_t *m = l->m;
    if (l->is_event) {
        process_evt(m);
        return out_read(m->evt_out, &m->evt_out_pos, m->evt_out_len, data, len);
    }
    process_cmd(m);
    return out_read(m->cmd_out, &m->cmd_out_pos, m->cmd_out_len, data, len);
}

static int link_connect(void *ctx, const void *params) {
    (void)ctx;
    (void)params;
    return 0;
}

static int link_disconnect(void *ctx) {
    (void)ctx;
    return 0;
}

static int link_set_timeout(void *ctx, int timeout_ms) {
    (void)ctx;
    (void)timeout_ms;
    return 0;
}

static const cc_transport_ops_t k_link_ops = {
    link_connect, link_disconnect, link_send, link_recv, link_set_timeout,
};

static cc_transport_t *make_link(ptpip_mock_t *m, int is_event) {
    ptpip_link_t *l = (ptpip_link_t *)calloc(1, sizeof(ptpip_link_t));
    if (!l) return NULL;
    l->m = m;
    l->is_event = is_event;
    cc_transport_t *t = (cc_transport_t *)calloc(1, sizeof(cc_transport_t));
    if (!t) {
        free(l);
        return NULL;
    }
    t->ops = &k_link_ops;
    t->ctx = l;
    t->kind = CC_TRANSPORT_PTPIP;
    return t;
}

void mock_ptpip_create(cc_transport_t **cmd_out, cc_transport_t **evt_out) {
    ptpip_mock_t *m = (ptpip_mock_t *)calloc(1, sizeof(ptpip_mock_t));
    *cmd_out = make_link(m, 0);
    *evt_out = make_link(m, 1);
}

void mock_ptpip_destroy(cc_transport_t *cmd, cc_transport_t *evt) {
    ptpip_mock_t *m = NULL;
    if (cmd && cmd->ctx) m = ((ptpip_link_t *)cmd->ctx)->m;
    if (cmd) {
        free(cmd->ctx);
        free(cmd);
    }
    if (evt) {
        free(evt->ctx);
        free(evt);
    }
    if (m) {
        free(m->cmd_in);
        free(m->cmd_out);
        free(m->evt_in);
        free(m->evt_out);
        free(m->ptp_out);
        free(m);
    }
}
