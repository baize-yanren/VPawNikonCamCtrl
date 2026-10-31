#include "ptpip_transport.h"
#include "ptpip.h"

#include "../internal.h"
#include "../ptp/container.h"
#include "../ptp/ptp_constants.h"

#include <stdlib.h>
#include <string.h>

/* 发送缓冲：帧构建用（data-out 单帧上限；参数设置类 data-out 远小于此）。 */
#define PTPIP_TX_CAP (1u * 1024u * 1024u)

typedef struct ptpip_ctx {
    cc_transport_t *cmd; /* 命令字节流 */
    cc_transport_t *evt; /* 事件字节流 */
    int delay_ms;
    int event_ok;
    uint32_t conn_number;
    uint8_t cam_guid[PTPIP_GUID_LEN];

    /* 发送：待发 command（延迟到 dataPhase 判断完成） */
    uint8_t pending_cmd[128];
    size_t pending_cmd_len;
    int has_pending;

    /* 当前活跃事务（响应 DATA 容器 code 用） */
    uint16_t active_opcode;
    uint32_t active_xid;

    /* 接收 FIFO：解码后的裸 PTP 容器字节 */
    uint8_t *fifo;
    size_t fifo_len, fifo_pos, fifo_cap;

    /* 发送帧构建缓冲 */
    uint8_t *tx_buf;
} ptpip_ctx_t;

/* ------------------------------------------------------------------ */
/* 底层字节流原语                                                       */
/* ------------------------------------------------------------------ */
static int stream_send_all(cc_transport_t *st, const uint8_t *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = st->ops->send(st->ctx, data + sent, len - sent);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int stream_recv_exact(cc_transport_t *st, uint8_t *data, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = st->ops->recv(st->ctx, data + got, len - got);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

/* 读一个帧头，返回 length 与 type。 */
static int recv_frame_header(cc_transport_t *st, uint32_t *length, uint32_t *type) {
    uint8_t hdr[8];
    if (stream_recv_exact(st, hdr, 8) != 0) return -1;
    ptp_buf_t b;
    ptp_buf_init_read(&b, hdr, 8);
    if (ptp_buf_get_u32(&b, length) != 0) return -1;
    if (ptp_buf_get_u32(&b, type) != 0) return -1;
    if (*length < 8) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 接收 FIFO                                                            */
/* ------------------------------------------------------------------ */
static int fifo_append(ptpip_ctx_t *p, const uint8_t *data, size_t len) {
    size_t need = p->fifo_len + len;
    if (need > p->fifo_cap) {
        size_t ncap = p->fifo_cap ? p->fifo_cap : 4096;
        while (ncap < need) ncap *= 2;
        uint8_t *np = (uint8_t *)realloc(p->fifo, ncap);
        if (!np) return -1;
        p->fifo = np;
        p->fifo_cap = ncap;
    }
    memcpy(p->fifo + p->fifo_len, data, len);
    p->fifo_len += len;
    return 0;
}

static void fifo_compact(ptpip_ctx_t *p) {
    if (p->fifo_pos == p->fifo_len) {
        p->fifo_pos = p->fifo_len = 0;
    }
}

static int fifo_append_response(ptpip_ctx_t *p, uint16_t resp_code, uint32_t xid,
                                const uint32_t *params, size_t nparams) {
    uint32_t length = (uint32_t)(12 + 4 * nparams);
    uint8_t hdr[12];
    ptp_buf_t b;
    ptp_buf_wrap(&b, hdr, sizeof(hdr));
    if (ptp_container_write_header(&b, length, PTP_TYPE_RESPONSE, resp_code, xid) != 0)
        return -1;
    if (fifo_append(p, hdr, 12) != 0) return -1;
    for (size_t i = 0; i < nparams; i++) {
        uint8_t pbuf[4];
        ptp_buf_t pb;
        ptp_buf_wrap(&pb, pbuf, 4);
        ptp_buf_put_u32(&pb, params[i]);
        if (fifo_append(p, pbuf, 4) != 0) return -1;
    }
    return 0;
}

static int fifo_append_data_header(ptpip_ctx_t *p, uint32_t xid, uint64_t total) {
    if (total > 0xFFFFFFF0u) return -1; /* 防御 4GB 溢出 */
    uint32_t length = (uint32_t)(12 + total);
    uint8_t hdr[12];
    ptp_buf_t b;
    ptp_buf_wrap(&b, hdr, sizeof(hdr));
    if (ptp_container_write_header(&b, length, PTP_TYPE_DATA, p->active_opcode, xid) != 0)
        return -1;
    return fifo_append(p, hdr, 12);
}

/* ------------------------------------------------------------------ */
/* 发送：容器 -> 帧                                                     */
/* ------------------------------------------------------------------ */

/* 把 pending 的 command 容器编码为 OperationRequest 帧发出。 */
static int send_pending(ptpip_ctx_t *p, uint32_t data_phase) {
    ptp_buf_t b;
    ptp_buf_init_read(&b, p->pending_cmd, p->pending_cmd_len);
    uint32_t clen;
    uint16_t type, opcode;
    uint32_t xid;
    if (ptp_buf_get_u32(&b, &clen) != 0) return -1;
    if (ptp_buf_get_u16(&b, &type) != 0) return -1;
    if (ptp_buf_get_u16(&b, &opcode) != 0) return -1;
    if (ptp_buf_get_u32(&b, &xid) != 0) return -1;

    uint32_t params[5];
    size_t nparams = 0;
    while (ptp_buf_avail_read(&b) >= 4 && nparams < 5) {
        if (ptp_buf_get_u32(&b, &params[nparams]) != 0) return -1;
        nparams++;
    }

    ptp_buf_t tb;
    ptp_buf_wrap(&tb, p->tx_buf, PTPIP_TX_CAP);
    if (ptpip_build_operation_request(&tb, opcode, xid, params, nparams, data_phase) != 0)
        return -1;

    p->active_opcode = opcode;
    p->active_xid = xid;

    return stream_send_all(p->cmd, p->tx_buf, tb.len);
}

/* data-out：StartData + Data + EndData。 */
static int send_data_frames(ptpip_ctx_t *p, uint32_t xid, const uint8_t *data, size_t len) {
    ptp_buf_t tb;
    ptp_buf_wrap(&tb, p->tx_buf, PTPIP_TX_CAP);
    if (ptpip_build_start_data(&tb, xid, (uint64_t)len) != 0) return -1;
    if (stream_send_all(p->cmd, p->tx_buf, tb.len) != 0) return -1;

    ptp_buf_wrap(&tb, p->tx_buf, PTPIP_TX_CAP);
    if (ptpip_build_data(&tb, xid, data, len) != 0) return -1;
    if (stream_send_all(p->cmd, p->tx_buf, tb.len) != 0) return -1;

    ptp_buf_wrap(&tb, p->tx_buf, PTPIP_TX_CAP);
    if (ptpip_build_end_data(&tb, xid, NULL, 0) != 0) return -1;
    if (stream_send_all(p->cmd, p->tx_buf, tb.len) != 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 接收：帧 -> 容器（读一个完整响应单元：OperationResponse 或 Data+Response） */
/* ------------------------------------------------------------------ */
static int read_response_unit(ptpip_ctx_t *p) {
    uint32_t length, type;

    if (recv_frame_header(p->cmd, &length, &type) != 0) return -1;
    uint32_t plen = length - 8;

    if (type == PTPIP_OPERATION_RESP) {
        uint8_t pbuf[128];
        if (plen > sizeof(pbuf)) return -1;
        if (stream_recv_exact(p->cmd, pbuf, plen) != 0) return -1;
        uint16_t resp_code = 0;
        uint32_t xid = 0;
        uint32_t params[5] = {0, 0, 0, 0, 0};
        size_t n = 0;
        if (ptpip_parse_operation_response(pbuf, plen, &resp_code, &xid, params, &n) != 0)
            return -1;
        return fifo_append_response(p, resp_code, xid, params, n);
    }

    if (type == PTPIP_START_DATA) {
        uint8_t pbuf[12];
        if (plen < 12) return -1;
        if (stream_recv_exact(p->cmd, pbuf, 12) != 0) return -1;
        uint32_t xid = 0;
        uint64_t total = 0;
        if (ptpip_parse_start_data(pbuf, 12, &xid, &total) != 0) return -1;
        if (fifo_append_data_header(p, xid, total) != 0) return -1;

        /* 循环读 Data 帧直到 EndData */
        uint64_t remaining = total;
        for (;;) {
            uint32_t dlen, dtype;
            if (recv_frame_header(p->cmd, &dlen, &dtype) != 0) return -1;
            uint32_t dplen = dlen - 8;
            if (dtype == PTPIP_DATA || dtype == PTPIP_END_DATA) {
                uint8_t xbuf[4];
                if (dplen < 4) return -1;
                if (stream_recv_exact(p->cmd, xbuf, 4) != 0) return -1;
                size_t data_len = (size_t)(dplen - 4);
                if (data_len > 0) {
                    uint8_t tmp[CC_COPY_BUF_SIZE];
                    size_t left = data_len;
                    while (left > 0) {
                        size_t w = left < sizeof(tmp) ? left : sizeof(tmp);
                        if (stream_recv_exact(p->cmd, tmp, w) != 0) return -1;
                        if (fifo_append(p, tmp, w) != 0) return -1;
                        left -= w;
                    }
                }
                if (data_len <= remaining) {
                    remaining -= data_len;
                } else {
                    remaining = 0;
                }
                if (dtype == PTPIP_END_DATA) break;
            } else {
                return -1;
            }
        }

        /* 数据阶段结束，读 OperationResponse */
        if (recv_frame_header(p->cmd, &length, &type) != 0) return -1;
        if (type != PTPIP_OPERATION_RESP) return -1;
        uint32_t rplen = length - 8;
        uint8_t rspbuf[128];
        if (rplen > sizeof(rspbuf)) return -1;
        if (stream_recv_exact(p->cmd, rspbuf, rplen) != 0) return -1;
        uint16_t resp_code = 0;
        uint32_t xid2 = 0;
        uint32_t params[5] = {0, 0, 0, 0, 0};
        size_t n = 0;
        if (ptpip_parse_operation_response(rspbuf, rplen, &resp_code, &xid2, params, &n) != 0)
            return -1;
        return fifo_append_response(p, resp_code, xid2, params, n);
    }

    return -1; /* 意外帧类型 */
}

/* ------------------------------------------------------------------ */
/* transport ops                                                        */
/* ------------------------------------------------------------------ */
static int ptpip_connect(void *ctx, const void *params) {
    (void)params;
    ptpip_ctx_t *p = (ptpip_ctx_t *)ctx;

    /* 0. 建立底层字节流连接（TCP socket 等由平台层实现；mock 为 no-op）。 */
    if (p->cmd->ops->connect && p->cmd->ops->connect(p->cmd->ctx, params) != 0)
        return -1;
    if (p->evt && p->evt->ops->connect && p->evt->ops->connect(p->evt->ctx, params) != 0) {
        p->evt = NULL; /* 事件通道连接失败则降级 */
    }

    /* 1. 命令连接握手：InitCommandRequest -> InitCommandAck */
    if (p->delay_ms > 0) cc_sleep_ms(p->delay_ms);
    ptp_buf_t b;
    ptp_buf_wrap(&b, p->tx_buf, PTPIP_TX_CAP);
    if (ptpip_build_init_command_request(&b, "CamCore") != 0) return -1;
    if (stream_send_all(p->cmd, p->tx_buf, b.len) != 0) return -1;

    uint32_t length = 0, type = 0;
    if (recv_frame_header(p->cmd, &length, &type) != 0) return -1;
    if (type != PTPIP_INIT_CMD_ACK) return -1;
    uint32_t plen = length - 8;
    uint8_t ackbuf[512];
    if (plen > sizeof(ackbuf)) return -1;
    if (stream_recv_exact(p->cmd, ackbuf, plen) != 0) return -1;
    if (ptpip_parse_init_command_ack(ackbuf, plen, &p->conn_number, p->cam_guid) != 0)
        return -1;

    /* 2. 事件连接握手（可选，失败降级为仅命令通道） */
    if (p->evt) {
        if (p->delay_ms > 0) cc_sleep_ms(p->delay_ms);
        ptp_buf_wrap(&b, p->tx_buf, PTPIP_TX_CAP);
        if (ptpip_build_init_event_request(&b, p->conn_number) != 0) return -1;
        if (stream_send_all(p->evt, p->tx_buf, b.len) != 0) {
            p->evt = NULL;
            return 0;
        }
        uint32_t elen = 0, etype = 0;
        if (recv_frame_header(p->evt, &elen, &etype) != 0 || etype != PTPIP_INIT_EVT_ACK) {
            p->evt = NULL;
            return 0;
        }
        uint32_t eplen = elen - 8;
        while (eplen > 0) {
            uint8_t tmp[64];
            uint32_t w = eplen < (uint32_t)sizeof(tmp) ? eplen : (uint32_t)sizeof(tmp);
            if (stream_recv_exact(p->evt, tmp, w) != 0) {
                p->evt = NULL;
                return 0;
            }
            eplen -= w;
        }
        p->event_ok = 1;
    }
    return 0;
}

static int ptpip_disconnect(void *ctx) {
    ptpip_ctx_t *p = (ptpip_ctx_t *)ctx;
    if (p->cmd && p->cmd->ops->disconnect) p->cmd->ops->disconnect(p->cmd->ctx);
    if (p->evt && p->evt->ops->disconnect) p->evt->ops->disconnect(p->evt->ctx);
    return 0;
}

static int ptpip_send(void *ctx, const uint8_t *data, size_t len) {
    ptpip_ctx_t *p = (ptpip_ctx_t *)ctx;
    if (len < PTP_CONTAINER_HEADER_LEN) return -1;

    ptp_buf_t b;
    ptp_buf_init_read(&b, data, len);
    uint32_t clen = 0;
    uint16_t ctype = 0, code = 0;
    uint32_t xid = 0;
    if (ptp_buf_get_u32(&b, &clen) != 0) return -1;
    if (ptp_buf_get_u16(&b, &ctype) != 0) return -1;
    if (ptp_buf_get_u16(&b, &code) != 0) return -1;
    if (ptp_buf_get_u32(&b, &xid) != 0) return -1;
    if (clen != len) return -1; /* 必须是完整容器 */

    if (ctype == PTP_TYPE_COMMAND) {
        if (len > sizeof(p->pending_cmd)) return -1;
        memcpy(p->pending_cmd, data, len);
        p->pending_cmd_len = len;
        p->has_pending = 1;
        return (int)len;
    }
    if (ctype == PTP_TYPE_DATA) {
        if (!p->has_pending) return -1;
        if (send_pending(p, PTPIP_DATAPHASE_SEND) != 0) return -1;
        p->has_pending = 0;
        size_t dlen = len - PTP_CONTAINER_HEADER_LEN;
        if (send_data_frames(p, xid, data + PTP_CONTAINER_HEADER_LEN, dlen) != 0)
            return -1;
        return (int)len;
    }
    return -1;
}

static int ptpip_recv(void *ctx, uint8_t *data, size_t len) {
    ptpip_ctx_t *p = (ptpip_ctx_t *)ctx;

    /* 若有待发 command（无 data-out 事务），先以 dataPhase=NONE 发出 */
    if (p->has_pending) {
        if (send_pending(p, PTPIP_DATAPHASE_NONE) != 0) return -1;
        p->has_pending = 0;
    }

    while (p->fifo_pos >= p->fifo_len) {
        fifo_compact(p);
        if (read_response_unit(p) != 0) return -1;
    }

    size_t avail = p->fifo_len - p->fifo_pos;
    size_t n = avail < len ? avail : len;
    memcpy(data, p->fifo + p->fifo_pos, n);
    p->fifo_pos += n;
    return (int)n;
}

static int ptpip_set_timeout(void *ctx, int timeout_ms) {
    ptpip_ctx_t *p = (ptpip_ctx_t *)ctx;
    if (p->cmd && p->cmd->ops->set_timeout) p->cmd->ops->set_timeout(p->cmd->ctx, timeout_ms);
    if (p->evt && p->evt->ops->set_timeout) p->evt->ops->set_timeout(p->evt->ctx, timeout_ms);
    return 0;
}

static const cc_transport_ops_t k_ptpip_ops = {
    ptpip_connect, ptpip_disconnect, ptpip_send, ptpip_recv, ptpip_set_timeout,
};

/* ------------------------------------------------------------------ */
/* 构造 / 释放                                                          */
/* ------------------------------------------------------------------ */
cc_transport_t *cc_ptpip_transport_new(const cc_ptpip_streams_t *streams,
                                       int handshake_delay_ms) {
    if (!streams || !streams->command) return NULL;
    ptpip_ctx_t *p = (ptpip_ctx_t *)calloc(1, sizeof(ptpip_ctx_t));
    if (!p) return NULL;
    p->cmd = streams->command;
    p->evt = streams->event;
    p->delay_ms = handshake_delay_ms;
    p->tx_buf = (uint8_t *)malloc(PTPIP_TX_CAP);
    if (!p->tx_buf) {
        free(p);
        return NULL;
    }
    cc_transport_t *t = (cc_transport_t *)calloc(1, sizeof(cc_transport_t));
    if (!t) {
        free(p->tx_buf);
        free(p);
        return NULL;
    }
    t->ops = &k_ptpip_ops;
    t->ctx = p;
    t->kind = CC_TRANSPORT_PTPIP;
    return t;
}

void cc_ptpip_transport_free(cc_transport_t *t) {
    if (!t) return;
    ptpip_ctx_t *p = (ptpip_ctx_t *)t->ctx;
    if (p) {
        free(p->tx_buf);
        free(p->fifo);
        free(p);
    }
    free(t);
}

int cc_ptpip_transport_event_ok(const cc_transport_t *t) {
    if (!t || !t->ctx) return 0;
    return ((const ptpip_ctx_t *)t->ctx)->event_ok;
}
