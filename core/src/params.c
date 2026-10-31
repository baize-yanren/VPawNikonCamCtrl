#include "internal.h"

#include "ptp/ptp_constants.h"
#include "ptp/bytebuf.h"

cc_status_t cc_get_param(cc_session_t *s, uint16_t prop_code, uint32_t *value) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;
    if (!value) return CC_ERR_INVALID_ARG;

    uint32_t params[1] = {prop_code};
    uint16_t rc = PTP_RC_Undefined;
    size_t len = 0;
    uint8_t tmp[8];
    cc_status_t st = cc_transact_data(s, PTP_OC_GetDevicePropValue, params, 1,
                                      NULL, 0, tmp, sizeof(tmp), &len, &rc);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);

    ptp_buf_t b;
    ptp_buf_init_read(&b, tmp, len);
    if (len >= 4) {
        if (ptp_buf_get_u32(&b, value) != 0) return CC_ERR_PROTOCOL;
    } else if (len >= 2) {
        uint16_t v;
        if (ptp_buf_get_u16(&b, &v) != 0) return CC_ERR_PROTOCOL;
        *value = v;
    } else if (len >= 1) {
        uint8_t v;
        if (ptp_buf_get_u8(&b, &v) != 0) return CC_ERR_PROTOCOL;
        *value = v;
    } else {
        *value = 0;
    }
    return CC_OK;
}

cc_status_t cc_set_param(cc_session_t *s, uint16_t prop_code, uint32_t value) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;

    uint8_t data[4];
    ptp_buf_t b;
    ptp_buf_wrap(&b, data, sizeof(data));
    ptp_buf_put_u32(&b, value);

    uint32_t params[1] = {prop_code};
    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st = cc_transact(s, PTP_OC_SetDevicePropValue, params, 1,
                                 data, 4, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);
    return CC_OK;
}
