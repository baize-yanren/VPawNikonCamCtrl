#ifndef MOCK_CAMERA_H
#define MOCK_CAMERA_H

/*
 * 内存 mock 相机：模拟一台尼康 USB-PTP 相机，通过内存 FIFO 充当 transport，
 * 用于 CamCore 的主机侧回归测试（无需真机）。参考 NikonLink 的 FakeNikonServer 思路。
 */

#include "camcore.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mock_link mock_link_t;

/* 创建一个已就绪的 transport（可注入 cc_create）；mock_link 由返回的 transport 内部持有。 */
cc_transport_t *mock_create(void);

/* 销毁 mock（transport 及其内部状态）。 */
void mock_destroy(cc_transport_t *t);

/* 内部对象句柄（供测试断言使用）。 */
#define MOCK_HANDLE_JPG 0x10000001u
#define MOCK_HANDLE_NEF 0x10000002u
#define MOCK_JPG_SIZE (10u * 1024u * 1024u)  /* 10 MiB，触发分块下载 */
#define MOCK_NEF_SIZE (1024u * 1024u)

#ifdef __cplusplus
}
#endif

#endif /* MOCK_CAMERA_H */
