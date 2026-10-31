#ifndef MOCK_CAMERA_H
#define MOCK_CAMERA_H

/*
 * 内存 mock 相机（USB-PTP 版）：通过内存 FIFO 充当 transport，
 * 供 CamCore 主机侧回归测试（无需真机）。PTP 操作语义复用 mock_ptp_core。
 */

#include "camcore.h"
#include "mock_ptp_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mock_link mock_link_t;

/* 创建一个已就绪的 USB transport（可注入 cc_create）。 */
cc_transport_t *mock_create(void);

/* 销毁 mock（transport 及其内部状态）。 */
void mock_destroy(cc_transport_t *t);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_CAMERA_H */
