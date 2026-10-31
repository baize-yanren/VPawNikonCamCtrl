#include "mock_camera.h"

#include "../src/ptp/ptp_constants.h"
#include "../src/ptp/container.h"

#include <stdlib.h>
#include <string.h>

struct mock_link {
    /* host -> camera */
    uint8_t *in;
    size_t in_len, in_cap;
    /* camera -> host */
    uint8_t *out;
    size_t out_len, out_pos, out_cap;
    /* PTP 语义状态 */
    mock_cam_t cam;
};

static void mock_ensure(void **p, size_t *cap, size_t need) {
    if (*cap >= need) return;
    size_t ncap = *cap ? *cap : 256;
    while (ncap < need) ncap *= 2;
    uint8_t *np = (uint8_t *)realloc(*p, ncap);
    if (np) {
        *p = np;
        *cap = ncap;
    }
}

/* 把 mock_ptp_core 输出的裸容器字节写入 out FIFO。 */
static void mock_emit(void *ctx, const uint8_t *data, size_t len) {
    mock_link_t *m = (mock_link_t *)ctx;
    mock_ensure((void **)&m->out, &m->out_cap, m->out_len + len);
    memcpy(m->out + m->out_len, data, len);
    m->out_len += len;
}

/* 处理累积输入中的一个事务，生成响应到 out FIFO。 */
static void mock_process(mock_link_t *m) {
    if (m->in_len < PTP_CONTAINER_HEADER_LEN) return;
    mock_ptp_process(&m->cam, mock_emit, m, m->in, m->in_len);
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
        mock_process(m);
    }
    size_t avail = m->out_len - m->out_pos;
    size_t n = avail < len ? avail : len;
    memcpy(data, m->out + m->out_pos, n);
    m->out_pos += n;
    return (int)n;
}

static int mock_connect(void *ctx, const void *params) {
    (void)ctx;
    (void)params;
    return 0;
}

static int mock_disconnect(void *ctx) {
    (void)ctx;
    return 0;
}

static int mock_set_timeout(void *ctx, int timeout_ms) {
    (void)ctx;
    (void)timeout_ms;
    return 0;
}

static const cc_transport_ops_t k_mock_ops = {
    mock_connect, mock_disconnect, mock_send, mock_recv, mock_set_timeout,
};

cc_transport_t *mock_create(void) {
    mock_link_t *m = (mock_link_t *)calloc(1, sizeof(mock_link_t));
    if (!m) return NULL;
    cc_transport_t *t = (cc_transport_t *)calloc(1, sizeof(cc_transport_t));
    if (!t) {
        free(m);
        return NULL;
    }
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
