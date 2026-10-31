#ifndef CAMCORE_INTERNAL_H
#define CAMCORE_INTERNAL_H

/*
 * CamCore 内部共享头（不对外暴露）。声明会话结构与事务引擎，供各功能模块复用。
 */

#include "camcore.h"

#include <stdint.h>
#include <stddef.h>

#define CC_RX_BUF_SIZE (16u * 1024u * 1024u) /* 数据阶段最大接收缓冲 */
#define CC_TX_BUF_SIZE (1024u * 1024u)       /* 发送缓冲 */
#define CC_CHUNK_SIZE (4u * 1024u * 1024u)   /* 分块下载默认 4 MiB */
#define CC_COPY_BUF_SIZE (256u * 1024u)      /* 流式复制缓冲 256 KiB */

struct cc_session {
    cc_transport_t *transport;
    cc_event_fn event_fn;
    void *userdata;

    uint32_t xid;         /* 事务 id，从 1 起 */
    uint32_t session_id;  /* 会话 id，从 1 起 */
    int session_open;
    int live_view;

    uint8_t *tx_buf;
    uint8_t *rx_buf;
};

/* 向调用方上抛事件。 */
void cc_emit_event(cc_session_t *s, cc_event_type_t type, uint32_t arg0, uint32_t arg1);

/* PTP 响应码 → cc_status_t 映射。 */
cc_status_t cc_map_response(uint16_t rc);

/* 跨平台毫秒睡眠。 */
void cc_sleep_ms(int ms);

/* 数据阶段接收回调（流式）：返回 0 继续，非 0 中止。 */
typedef int (*cc_data_sink)(void *ctx, const uint8_t *buf, size_t len);

/*
 * 事务引擎：发送 command（+可选 data-out），接收 data-in（交给 sink）与 response。
 * resp_code / resp_params / n_resp_params 可为 NULL 表示不关心。
 */
cc_status_t cc_transact(cc_session_t *s, uint16_t opcode,
                        const uint32_t *params, size_t nparams,
                        const uint8_t *data_out, size_t data_out_len,
                        cc_data_sink sink, void *sink_ctx,
                        uint16_t *resp_code, uint32_t *resp_params, size_t *n_resp_params);

/* 小数据事务便捷封装：data-in 累积到内存缓冲，data_out 直接发送。 */
cc_status_t cc_transact_data(cc_session_t *s, uint16_t opcode,
                             const uint32_t *params, size_t nparams,
                             const uint8_t *data_out, size_t data_out_len,
                             uint8_t *data_in, size_t data_in_cap, size_t *data_in_len,
                             uint16_t *resp_code);

#endif /* CAMCORE_INTERNAL_H */
