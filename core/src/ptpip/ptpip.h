#ifndef PTPIP_H
#define PTPIP_H

/*
 * PTP/IP（CIPA DC-005）帧编解码层 —— 纯函数，无 socket / 无平台依赖。
 *
 * 帧头（8 字节，小端序）：
 *   offset 0  uint32  总长度（含 8 字节头）
 *   offset 4  uint32  包类型（见 ptpip_packet_type）
 *
 * 本层只负责「帧的字节布局」的构建与解析；握手时序、双连接管理、
 * 容器 <-> 帧的翻译由 ptpip_transport.c 负责。
 *
 * License: MIT
 */

#include <stdint.h>
#include <stddef.h>

#include "../ptp/bytebuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 包类型                                                              */
/* ------------------------------------------------------------------ */
enum ptpip_packet_type {
    PTPIP_INIT_CMD_REQ = 1,   /* InitCommandRequest */
    PTPIP_INIT_CMD_ACK = 2,   /* InitCommandAck */
    PTPIP_INIT_EVT_REQ = 3,   /* InitEventRequest */
    PTPIP_INIT_EVT_ACK = 4,   /* InitEventAck */
    PTPIP_INIT_FAIL = 5,      /* InitFail */
    PTPIP_OPERATION_REQ = 6,  /* OperationRequest */
    PTPIP_OPERATION_RESP = 7, /* OperationResponse */
    PTPIP_EVENT = 8,          /* Event */
    PTPIP_START_DATA = 9,     /* StartData */
    PTPIP_DATA = 10,          /* Data */
    PTPIP_CANCEL = 11,        /* CancelTransaction */
    PTPIP_END_DATA = 12,      /* EndData */
    PTPIP_PROBE_REQ = 13,     /* ProbeRequest */
    PTPIP_PROBE_RESP = 14,    /* ProbeResponse */
};

/* 帧头长度 */
#define PTPIP_FRAME_HEADER_LEN 8

/* 客户端 GUID（固定，让尼康机身认作已知主机，见 PROTOCOL.md §11.7）。 */
#define PTPIP_GUID_LEN 16
#define PTPIP_PROTO_VERSION 0x00010000u

/* DataPhaseInfo：1 = 无数据阶段 / 下行数据；2 = 上行数据（data-out）。 */
#define PTPIP_DATAPHASE_NONE 1
#define PTPIP_DATAPHASE_SEND 2

extern const uint8_t ptpip_client_guid[PTPIP_GUID_LEN];

/* ------------------------------------------------------------------ */
/* 帧构建（写入 ptp_buf，返回 0 成功，-1 空间不足）                      */
/* ------------------------------------------------------------------ */

/* InitCommandRequest：GUID + 设备名(UTF-16LE+\0\0) + 协议版本。 */
int ptpip_build_init_command_request(ptp_buf_t *b, const char *name);

/* InitEventRequest：connectionNumber。 */
int ptpip_build_init_event_request(ptp_buf_t *b, uint32_t conn_number);

/* OperationRequest：dataPhase + operation(u16) + xid + params。 */
int ptpip_build_operation_request(ptp_buf_t *b, uint16_t opcode, uint32_t xid,
                                  const uint32_t *params, size_t nparams,
                                  uint32_t data_phase);

/* OperationResponse：responseCode(u16) + xid + params。 */
int ptpip_build_operation_response(ptp_buf_t *b, uint16_t resp_code, uint32_t xid,
                                   const uint32_t *params, size_t nparams);

/* StartData：xid + totalDataLength(u64)。 */
int ptpip_build_start_data(ptp_buf_t *b, uint32_t xid, uint64_t total_len);

/* Data / EndData：xid + 数据。 */
int ptpip_build_data(ptp_buf_t *b, uint32_t xid, const uint8_t *data, size_t len);
int ptpip_build_end_data(ptp_buf_t *b, uint32_t xid, const uint8_t *data, size_t len);

/* ------------------------------------------------------------------ */
/* 帧解析（从 payload 字节数组，不含 8 字节帧头）                        */
/* ------------------------------------------------------------------ */

/* InitCommandAck payload：connectionNumber(u32) + GUID(16B) + name(...)。 */
int ptpip_parse_init_command_ack(const uint8_t *payload, size_t len,
                                 uint32_t *conn_number, uint8_t guid[PTPIP_GUID_LEN]);

/* OperationResponse payload：responseCode(u16) + xid(u32) + params。 */
int ptpip_parse_operation_response(const uint8_t *payload, size_t len,
                                   uint16_t *resp_code, uint32_t *xid,
                                   uint32_t *params, size_t *nparams);

/* StartData payload：xid(u32) + totalDataLength(u64)。 */
int ptpip_parse_start_data(const uint8_t *payload, size_t len,
                           uint32_t *xid, uint64_t *total_len);

/* OperationRequest payload 解析（mock 服务端用）。 */
int ptpip_parse_operation_request(const uint8_t *payload, size_t len,
                                  uint16_t *opcode, uint32_t *xid,
                                  uint32_t *data_phase,
                                  uint32_t *params, size_t *nparams);

#ifdef __cplusplus
}
#endif

#endif /* PTPIP_H */
