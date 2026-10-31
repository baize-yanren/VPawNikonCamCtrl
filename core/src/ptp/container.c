#include "container.h"
#include "ptp_constants.h"

int ptp_container_read_header(ptp_buf_t *b, ptp_container_t *c) {
    if (ptp_buf_avail_read(b) < PTP_CONTAINER_HEADER_LEN) return -1;
    if (ptp_buf_get_u32(b, &c->length) != 0) return -1;
    if (ptp_buf_get_u16(b, &c->type) != 0) return -1;
    if (ptp_buf_get_u16(b, &c->code) != 0) return -1;
    if (ptp_buf_get_u32(b, &c->transaction) != 0) return -1;
    return 0;
}

int ptp_container_write_header(ptp_buf_t *b, uint32_t length, uint16_t type,
                               uint16_t code, uint32_t transaction) {
    if (ptp_buf_put_u32(b, length) != 0) return -1;
    if (ptp_buf_put_u16(b, type) != 0) return -1;
    if (ptp_buf_put_u16(b, code) != 0) return -1;
    if (ptp_buf_put_u32(b, transaction) != 0) return -1;
    return 0;
}

int ptp_container_build_command(ptp_buf_t *b, uint16_t code, uint32_t transaction,
                                const uint32_t *params, size_t nparams) {
    if (nparams > PTP_MAX_PARAMS) return -1;
    uint32_t length = (uint32_t)(PTP_CONTAINER_HEADER_LEN + nparams * 4);
    if (ptp_container_write_header(b, length, PTP_TYPE_COMMAND, code, transaction) != 0)
        return -1;
    for (size_t i = 0; i < nparams; i++) {
        if (ptp_buf_put_u32(b, params[i]) != 0) return -1;
    }
    return 0;
}

int ptp_container_build_data(ptp_buf_t *b, uint16_t code, uint32_t transaction,
                             const void *payload, size_t payload_len) {
    uint32_t length = (uint32_t)(PTP_CONTAINER_HEADER_LEN + payload_len);
    if (ptp_container_write_header(b, length, PTP_TYPE_DATA, code, transaction) != 0)
        return -1;
    if (payload_len > 0 && ptp_buf_put_bytes(b, payload, payload_len) != 0) return -1;
    return 0;
}
