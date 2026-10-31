#include "ptpip.h"

#include <string.h>

const uint8_t ptpip_client_guid[PTPIP_GUID_LEN] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
};

/* ------------------------------------------------------------------ */
/* 帧构建                                                              */
/* ------------------------------------------------------------------ */

int ptpip_build_init_command_request(ptp_buf_t *b, const char *name) {
    size_t n = name ? strlen(name) : 0;
    size_t name_units = n + 1; /* 含终止符 */
    uint32_t length = (uint32_t)(PTPIP_FRAME_HEADER_LEN + PTPIP_GUID_LEN +
                                 name_units * 2 + 4);
    if (ptp_buf_put_u32(b, length) != 0) return -1;
    if (ptp_buf_put_u32(b, PTPIP_INIT_CMD_REQ) != 0) return -1;
    if (ptp_buf_put_bytes(b, ptpip_client_guid, PTPIP_GUID_LEN) != 0) return -1;
    for (size_t i = 0; i < n; i++) {
        if (ptp_buf_put_u16(b, (uint16_t)(uint8_t)name[i]) != 0) return -1;
    }
    if (ptp_buf_put_u16(b, 0) != 0) return -1; /* 终止符 */
    if (ptp_buf_put_u32(b, PTPIP_PROTO_VERSION) != 0) return -1;
    return 0;
}

int ptpip_build_init_event_request(ptp_buf_t *b, uint32_t conn_number) {
    if (ptp_buf_put_u32(b, PTPIP_FRAME_HEADER_LEN + 4) != 0) return -1;
    if (ptp_buf_put_u32(b, PTPIP_INIT_EVT_REQ) != 0) return -1;
    if (ptp_buf_put_u32(b, conn_number) != 0) return -1;
    return 0;
}

int ptpip_build_operation_request(ptp_buf_t *b, uint16_t opcode, uint32_t xid,
                                  const uint32_t *params, size_t nparams,
                                  uint32_t data_phase) {
    uint32_t length = (uint32_t)(PTPIP_FRAME_HEADER_LEN + 4 + 2 + 4 + 4 * nparams);
    if (ptp_buf_put_u32(b, length) != 0) return -1;
    if (ptp_buf_put_u32(b, PTPIP_OPERATION_REQ) != 0) return -1;
    if (ptp_buf_put_u32(b, data_phase) != 0) return -1;
    if (ptp_buf_put_u16(b, opcode) != 0) return -1;
    if (ptp_buf_put_u32(b, xid) != 0) return -1;
    for (size_t i = 0; i < nparams; i++) {
        if (ptp_buf_put_u32(b, params[i]) != 0) return -1;
    }
    return 0;
}

int ptpip_build_operation_response(ptp_buf_t *b, uint16_t resp_code, uint32_t xid,
                                   const uint32_t *params, size_t nparams) {
    uint32_t length = (uint32_t)(PTPIP_FRAME_HEADER_LEN + 2 + 4 + 4 * nparams);
    if (ptp_buf_put_u32(b, length) != 0) return -1;
    if (ptp_buf_put_u32(b, PTPIP_OPERATION_RESP) != 0) return -1;
    if (ptp_buf_put_u16(b, resp_code) != 0) return -1;
    if (ptp_buf_put_u32(b, xid) != 0) return -1;
    for (size_t i = 0; i < nparams; i++) {
        if (ptp_buf_put_u32(b, params[i]) != 0) return -1;
    }
    return 0;
}

int ptpip_build_start_data(ptp_buf_t *b, uint32_t xid, uint64_t total_len) {
    if (ptp_buf_put_u32(b, PTPIP_FRAME_HEADER_LEN + 4 + 8) != 0) return -1;
    if (ptp_buf_put_u32(b, PTPIP_START_DATA) != 0) return -1;
    if (ptp_buf_put_u32(b, xid) != 0) return -1;
    if (ptp_buf_put_u64(b, total_len) != 0) return -1;
    return 0;
}

int ptpip_build_data(ptp_buf_t *b, uint32_t xid, const uint8_t *data, size_t len) {
    if (ptp_buf_put_u32(b, (uint32_t)(PTPIP_FRAME_HEADER_LEN + 4 + len)) != 0) return -1;
    if (ptp_buf_put_u32(b, PTPIP_DATA) != 0) return -1;
    if (ptp_buf_put_u32(b, xid) != 0) return -1;
    if (len > 0 && ptp_buf_put_bytes(b, data, len) != 0) return -1;
    return 0;
}

int ptpip_build_end_data(ptp_buf_t *b, uint32_t xid, const uint8_t *data, size_t len) {
    if (ptp_buf_put_u32(b, (uint32_t)(PTPIP_FRAME_HEADER_LEN + 4 + len)) != 0) return -1;
    if (ptp_buf_put_u32(b, PTPIP_END_DATA) != 0) return -1;
    if (ptp_buf_put_u32(b, xid) != 0) return -1;
    if (len > 0 && ptp_buf_put_bytes(b, data, len) != 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 帧解析                                                              */
/* ------------------------------------------------------------------ */

int ptpip_parse_init_command_ack(const uint8_t *payload, size_t len,
                                 uint32_t *conn_number, uint8_t guid[PTPIP_GUID_LEN]) {
    if (len < 4 + PTPIP_GUID_LEN) return -1;
    ptp_buf_t b;
    ptp_buf_init_read(&b, payload, len);
    if (ptp_buf_get_u32(&b, conn_number) != 0) return -1;
    if (ptp_buf_get_bytes(&b, guid, PTPIP_GUID_LEN) != 0) return -1;
    return 0;
}

int ptpip_parse_operation_response(const uint8_t *payload, size_t len,
                                   uint16_t *resp_code, uint32_t *xid,
                                   uint32_t *params, size_t *nparams) {
    if (len < 6) return -1;
    ptp_buf_t b;
    ptp_buf_init_read(&b, payload, len);
    if (ptp_buf_get_u16(&b, resp_code) != 0) return -1;
    if (ptp_buf_get_u32(&b, xid) != 0) return -1;
    size_t n = 0;
    while (ptp_buf_avail_read(&b) >= 4 && n < 5) {
        if (ptp_buf_get_u32(&b, &params[n]) != 0) return -1;
        n++;
    }
    if (nparams) *nparams = n;
    return 0;
}

int ptpip_parse_start_data(const uint8_t *payload, size_t len,
                           uint32_t *xid, uint64_t *total_len) {
    if (len < 12) return -1;
    ptp_buf_t b;
    ptp_buf_init_read(&b, payload, len);
    if (ptp_buf_get_u32(&b, xid) != 0) return -1;
    if (ptp_buf_get_u64(&b, total_len) != 0) return -1;
    return 0;
}

int ptpip_parse_operation_request(const uint8_t *payload, size_t len,
                                  uint16_t *opcode, uint32_t *xid,
                                  uint32_t *data_phase,
                                  uint32_t *params, size_t *nparams) {
    if (len < 10) return -1;
    ptp_buf_t b;
    ptp_buf_init_read(&b, payload, len);
    if (ptp_buf_get_u32(&b, data_phase) != 0) return -1;
    if (ptp_buf_get_u16(&b, opcode) != 0) return -1;
    if (ptp_buf_get_u32(&b, xid) != 0) return -1;
    size_t n = 0;
    while (ptp_buf_avail_read(&b) >= 4 && n < 5) {
        if (ptp_buf_get_u32(&b, &params[n]) != 0) return -1;
        n++;
    }
    if (nparams) *nparams = n;
    return 0;
}
