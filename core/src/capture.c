#include "internal.h"

#include "ptp/ptp_constants.h"
#include "ptp/bytebuf.h"

#include <string.h>

/* 拍摄后轮询 CheckEvent 找 SDRAM 对象句柄（事件 0xC101）。 */
static uint32_t capture_find_handle(cc_session_t *s) {
    uint32_t handle = PTP_NIKON_SDRAM_OBJECT_HANDLE;
    for (int i = 0; i < 20; i++) {
        uint8_t buf[256];
        size_t len = 0;
        uint16_t rc = PTP_RC_Undefined;
        cc_status_t st = cc_transact_data(s, NK_OC_CheckEvent, NULL, 0,
                                          NULL, 0, buf, sizeof(buf), &len, &rc);
        if (st != CC_OK) break;
        if (rc == PTP_RC_OperationNotSupported || rc == PTP_RC_Undefined) break;
        if (rc != PTP_RC_OK) break;

        ptp_buf_t b;
        ptp_buf_init_read(&b, buf, len);
        uint16_t count = 0;
        if (ptp_buf_get_u16(&b, &count) != 0) break;
        for (int j = 0; j < count; j++) {
            uint16_t code = 0;
            uint32_t param = 0;
            if (ptp_buf_get_u16(&b, &code) != 0 || ptp_buf_get_u32(&b, &param) != 0) break;
            if (code == NK_EC_ObjectAddedInSDRAM) handle = param;
        }
        if (handle != PTP_NIKON_SDRAM_OBJECT_HANDLE) break;
        cc_sleep_ms(100);
    }
    return handle;
}

cc_status_t cc_capture(cc_session_t *s) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;

    uint32_t params[2] = {0xFFFFFFFFu, 1};
    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st = cc_transact(s, NK_OC_CaptureToSdram, params, 2,
                                 NULL, 0, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);

    uint32_t handle = capture_find_handle(s);
    cc_emit_event(s, CC_EVENT_CAPTURE_DONE, handle, 0);
    return CC_OK;
}

/* 进入 remote mode + M 模式 + Bulb 快门值。 */
static cc_status_t bulb_arm(cc_session_t *s) {
    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st;

    /* SetControlMode(1) 进入 remote mode */
    uint32_t one = 1;
    st = cc_transact(s, NK_OC_SetControlMode, &one, 1, NULL, 0, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);

    /* M 模式 0x500E = 1 */
    uint8_t data_out[20];
    memset(data_out, 0, sizeof(data_out));
    ptp_buf_t b;
    ptp_buf_wrap(&b, data_out, sizeof(data_out));
    ptp_buf_put_u32(&b, PTP_DPC_ExposureProgramMode); /* propertyCode */
    ptp_buf_put_u32(&b, PTP_EP_MANUAL);               /* value (M) */

    uint32_t prop = PTP_DPC_ExposureProgramMode;
    st = cc_transact(s, PTP_OC_SetDevicePropValue, &prop, 1,
                     data_out, 8, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK && rc != NK_RC_SetPropertyNotSupported) return cc_map_response(rc);

    /* Bulb 快门值 0x500D = 0xFFFFFFFF */
    uint8_t shutter[20];
    memset(shutter, 0, sizeof(shutter));
    ptp_buf_wrap(&b, shutter, sizeof(shutter));
    ptp_buf_put_u32(&b, PTP_DPC_ExposureTime);
    ptp_buf_put_u32(&b, 0xFFFFFFFFu);

    prop = PTP_DPC_ExposureTime;
    st = cc_transact(s, PTP_OC_SetDevicePropValue, &prop, 1,
                     shutter, 8, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK && rc != NK_RC_SetPropertyNotSupported) return cc_map_response(rc);

    return CC_OK;
}

cc_status_t cc_start_bulb(cc_session_t *s) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;
    cc_status_t st = bulb_arm(s);
    if (st != CC_OK) return st;

    uint32_t params[2] = {0xFFFFFFFFu, 1};
    uint16_t rc = PTP_RC_Undefined;
    st = cc_transact(s, NK_OC_CaptureToSdram, params, 2, NULL, 0, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);
    return CC_OK;
}

cc_status_t cc_end_bulb(cc_session_t *s) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;

    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st = cc_transact(s, NK_OC_TerminateCapture, NULL, 0,
                                 NULL, 0, NULL, NULL, &rc, NULL, NULL);
    /* 释放机身控制（finally 语义） */
    uint32_t zero = 0;
    cc_transact(s, NK_OC_SetControlMode, &zero, 1, NULL, 0, NULL, NULL, &rc, NULL, NULL);

    uint32_t handle = capture_find_handle(s);
    cc_emit_event(s, CC_EVENT_CAPTURE_DONE, handle, 0);
    return (st != CC_OK) ? st : CC_OK;
}

/* ------------------------------------------------------------------ */
/* 实时取景                                                            */
/* ------------------------------------------------------------------ */
cc_status_t cc_start_live_view(cc_session_t *s) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;
    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st = cc_transact(s, NK_OC_StartLiveView, NULL, 0, NULL, 0, NULL, NULL, &rc, NULL, NULL);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);
    s->live_view = 1;
    return CC_OK;
}

cc_status_t cc_get_live_view_frame(cc_session_t *s, uint8_t *buf, size_t *len) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;
    if (!buf || !len) return CC_ERR_INVALID_ARG;
    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st = cc_transact_data(s, NK_OC_GetLiveViewImg, NULL, 0,
                                      NULL, 0, buf, *len, len, &rc);
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);
    return CC_OK;
}

cc_status_t cc_end_live_view(cc_session_t *s) {
    if (!s || !s->session_open) return CC_ERR_NOT_CONNECTED;
    uint16_t rc = PTP_RC_Undefined;
    cc_status_t st = cc_transact(s, NK_OC_EndLiveView, NULL, 0, NULL, 0, NULL, NULL, &rc, NULL, NULL);
    s->live_view = 0;
    if (st != CC_OK) return st;
    if (rc != PTP_RC_OK) return cc_map_response(rc);
    return CC_OK;
}
