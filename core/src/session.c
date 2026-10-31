#include "internal.h"

#include "ptp/ptp_constants.h"
#include "ptp/bytebuf.h"
#include "ptp/container.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

void cc_sleep_ms(int ms) {
    if (ms <= 0) return;
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

/* ------------------------------------------------------------------ */
/* 版本与生命周期                                                       */
/* ------------------------------------------------------------------ */
const char *cc_version(void) {
    return "0.1.0";
}

cc_session_t *cc_create(cc_transport_t *transport, cc_event_fn event_fn, void *userdata) {
    if (!transport || !transport->ops) return NULL;
    cc_session_t *s = (cc_session_t *)calloc(1, sizeof(cc_session_t));
    if (!s) return NULL;
    s->transport = transport;
    s->event_fn = event_fn;
    s->userdata = userdata;
    s->xid = 0;
    s->session_id = 0;
    s->tx_buf = (uint8_t *)malloc(CC_TX_BUF_SIZE);
    s->rx_buf = (uint8_t *)malloc(CC_RX_BUF_SIZE);
    if (!s->tx_buf || !s->rx_buf) {
        cc_destroy(s);
        return NULL;
    }
    return s;
}

void cc_destroy(cc_session_t *s) {
    if (!s) return;
    if (s->session_open && s->transport && s->transport->ops) {
        cc_disconnect(s);
    }
    free(s->tx_buf);
    free(s->rx_buf);
    free(s);
}

void cc_emit_event(cc_session_t *s, cc_event_type_t type, uint32_t arg0, uint32_t arg1) {
    if (!s || !s->event_fn) return;
    cc_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.arg0 = arg0;
    ev.arg1 = arg1;
    s->event_fn(s->userdata, &ev);
}

/* ------------------------------------------------------------------ */
/* Transport 收发原语                                                   */
/* ------------------------------------------------------------------ */
static int s_send_all(cc_session_t *s, const uint8_t *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = s->transport->ops->send(s->transport->ctx, data + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int s_recv_exact(cc_session_t *s, uint8_t *data, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = s->transport->ops->recv(s->transport->ctx, data + got, len - got);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 响应码映射                                                           */
/* ------------------------------------------------------------------ */
cc_status_t cc_map_response(uint16_t rc) {
    switch (rc) {
        case PTP_RC_OK: return CC_OK;
        case PTP_RC_DeviceBusy:
        case PTP_RC_InvalidObjectHandle: /* 尼康拍摄后常用 0x2009 表示 busy */
            return CC_ERR_DEVICE_BUSY;
        case PTP_RC_OperationNotSupported:
        case PTP_RC_DevicePropNotSupported:
            return CC_ERR_UNSUPPORTED;
        case PTP_RC_SessionNotOpen: return CC_ERR_SESSION;
        case PTP_RC_InvalidTransactionID: return CC_ERR_PROTOCOL;
        case PTP_RC_IncompleteTransfer: return CC_ERR_IO;
        default: return CC_ERR_PROTOCOL;
    }
}

/* ------------------------------------------------------------------ */
/* 事务引擎                                                             */
/* ------------------------------------------------------------------ */
cc_status_t cc_transact(cc_session_t *s, uint16_t opcode,
                        const uint32_t *params, size_t nparams,
                        const uint8_t *data_out, size_t data_out_len,
                        cc_data_sink sink, void *sink_ctx,
                        uint16_t *resp_code, uint32_t *resp_params, size_t *n_resp_params) {
    if (!s || !s->transport || !s->transport->ops) return CC_ERR_INVALID_ARG;

    /* 分配事务 id */
    uint32_t xid = (s->xid == 0) ? 1 : s->xid;
    s->xid = (xid == 0xFFFFFFFFu) ? 1 : xid + 1;

    /* 1. 发送 command */
    ptp_buf_t tx;
    ptp_buf_wrap(&tx, s->tx_buf, CC_TX_BUF_SIZE);
    if (ptp_container_build_command(&tx, opcode, xid, params, nparams) != 0)
        return CC_ERR_NO_MEMORY;
    if (s_send_all(s, s->tx_buf, tx.len) != 0) return CC_ERR_IO;

    /* 2. 可选 data-out 阶段 */
    if (data_out && data_out_len > 0) {
        ptp_buf_wrap(&tx, s->tx_buf, CC_TX_BUF_SIZE);
        if (ptp_container_build_data(&tx, opcode, xid, data_out, data_out_len) != 0)
            return CC_ERR_NO_MEMORY;
        if (s_send_all(s, s->tx_buf, tx.len) != 0) return CC_ERR_IO;
    }

    /* 3. 读响应头（可能是 DATA 阶段，也可能是 RESPONSE） */
    for (;;) {
        uint8_t hdr[PTP_CONTAINER_HEADER_LEN];
        if (s_recv_exact(s, hdr, PTP_CONTAINER_HEADER_LEN) != 0) return CC_ERR_IO;

        ptp_buf_t rb;
        ptp_buf_init_read(&rb, hdr, sizeof(hdr));
        ptp_container_t c;
        if (ptp_container_read_header(&rb, &c) != 0) return CC_ERR_PROTOCOL;

        if (c.type == PTP_TYPE_DATA) {
            /* DATA 阶段：流式读取 length-12 字节，喂给 sink 或丢弃 */
            size_t remaining = c.length > PTP_CONTAINER_HEADER_LEN
                                   ? c.length - PTP_CONTAINER_HEADER_LEN
                                   : 0;
            uint8_t *chunk = (uint8_t *)malloc(CC_COPY_BUF_SIZE);
            if (!chunk) return CC_ERR_NO_MEMORY;
            while (remaining > 0) {
                size_t want = remaining < CC_COPY_BUF_SIZE ? remaining : CC_COPY_BUF_SIZE;
                if (s_recv_exact(s, chunk, want) != 0) {
                    free(chunk);
                    return CC_ERR_IO;
                }
                if (sink) {
                    if (sink(sink_ctx, chunk, want) != 0) {
                        free(chunk);
                        return CC_ERR_IO;
                    }
                }
                remaining -= want;
            }
            free(chunk);
            /* DATA 后应跟 RESPONSE，继续循环读 */
            continue;
        }

        if (c.type == PTP_TYPE_RESPONSE) {
            /* 读参数部分 */
            size_t plen = c.length > PTP_CONTAINER_HEADER_LEN
                              ? c.length - PTP_CONTAINER_HEADER_LEN
                              : 0;
            if (plen > 0) {
                if (plen > CC_RX_BUF_SIZE) return CC_ERR_PROTOCOL;
                if (s_recv_exact(s, s->rx_buf, plen) != 0) return CC_ERR_IO;
            }
            if (resp_code) *resp_code = c.code;
            if (resp_params && n_resp_params) {
                ptp_buf_t pb;
                ptp_buf_init_read(&pb, s->rx_buf, plen);
                size_t n = 0;
                while (ptp_buf_avail_read(&pb) >= 4 && n < 5) {
                    uint32_t v;
                    ptp_buf_get_u32(&pb, &v);
                    resp_params[n++] = v;
                }
                *n_resp_params = n;
            }
            return CC_OK;
        }

        /* command 出现在响应位置 = 协议错误 */
        return CC_ERR_PROTOCOL;
    }
}

/* 小数据事务封装：data-in 累积到内存。 */
typedef struct mem_sink {
    uint8_t *buf;
    size_t cap;
    size_t off;
} mem_sink_t;

static int mem_sink_write(void *ctx, const uint8_t *buf, size_t len) {
    mem_sink_t *m = (mem_sink_t *)ctx;
    if (!m->buf || m->off + len > m->cap) return -1;
    memcpy(m->buf + m->off, buf, len);
    m->off += len;
    return 0;
}

cc_status_t cc_transact_data(cc_session_t *s, uint16_t opcode,
                             const uint32_t *params, size_t nparams,
                             const uint8_t *data_out, size_t data_out_len,
                             uint8_t *data_in, size_t data_in_cap, size_t *data_in_len,
                             uint16_t *resp_code) {
    cc_status_t st;

    if (data_in_len) *data_in_len = 0;

    mem_sink_t ms = {data_in, data_in_cap, 0};

    st = cc_transact(s, opcode, params, nparams, data_out, data_out_len,
                     mem_sink_write, &ms, resp_code, NULL, NULL);
    if (st != CC_OK) return st;
    if (data_in_len) *data_in_len = ms.off;
    return CC_OK;
}

/* ------------------------------------------------------------------ */
/* 连接 / 断开                                                          */
/* ------------------------------------------------------------------ */
cc_status_t cc_connect(cc_session_t *s, const cc_connect_params_t *params) {
    if (!s || !params) return CC_ERR_INVALID_ARG;

    /* 底层连接（由 transport 适配层负责，USB 或 PTP/IP） */
    if (s->transport->ops->connect) {
        if (s->transport->ops->connect(s->transport->ctx, params) != 0)
            return CC_ERR_IO;
    }

    /* PTP OpenSession */
    uint32_t sid = (s->session_id == 0) ? 1 : s->session_id;
    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st = cc_transact(s, PTP_OC_OpenSession, &sid, 1,
                                 NULL, 0, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;

    if (rc == PTP_RC_SessionAlreadyOpen) {
        /* 先关闭再重试一次 */
        cc_transact(s, PTP_OC_CloseSession, &sid, 1, NULL, 0, NULL, NULL, &rc, NULL, NULL);
        rc = PTP_RC_Undefined;
        st = cc_transact(s, PTP_OC_OpenSession, &sid, 1, NULL, 0, NULL, NULL, &rc, NULL, NULL);
        if (st != CC_OK) return st;
    }

    if (rc != PTP_RC_OK) return cc_map_response(rc);

    s->session_open = 1;
    s->session_id = sid;
    s->xid = 1;
    cc_emit_event(s, CC_EVENT_CONNECTED, 0, 0);
    return CC_OK;
}

cc_status_t cc_disconnect(cc_session_t *s) {
    if (!s) return CC_ERR_INVALID_ARG;
    if (s->session_open) {
        uint16_t rc = PTP_RC_Undefined;
        cc_transact(s, PTP_OC_CloseSession, &s->session_id, 1, NULL, 0, NULL, NULL, &rc, NULL, NULL);
        s->session_open = 0;
    }
    if (s->transport->ops->disconnect) {
        s->transport->ops->disconnect(s->transport->ctx);
    }
    cc_emit_event(s, CC_EVENT_DISCONNECTED, 0, 0);
    return CC_OK;
}
