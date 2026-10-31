#ifndef MOCK_PTPIP_CAMERA_H
#define MOCK_PTPIP_CAMERA_H

/*
 * PTP/IP mock 相机：模拟一台尼康相机的 PTP/IP 服务端（命令连接 + 事件连接），
 * 通过内存 FIFO 充当两条字节流 transport，供 CamCore 主机侧回归测试。
 * 握手与帧编解码与真实相机一致，PTP 操作语义复用 mock_ptp_core。
 */

#include "camcore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 创建 PTP/IP mock，返回命令流与事件流两个「已连接」字节流 transport。 */
void mock_ptpip_create(cc_transport_t **cmd_out, cc_transport_t **evt_out);

/* 销毁 mock 及其两个 transport。 */
void mock_ptpip_destroy(cc_transport_t *cmd, cc_transport_t *evt);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_PTPIP_CAMERA_H */
