#ifndef PTP_CONSTANTS_H
#define PTP_CONSTANTS_H

/*
 * PTP / PTP/IP 协议常量（值均来自 docs/PROTOCOL.md 的 clean-room 对照笔记）。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* 容器类型                                                            */
/* ------------------------------------------------------------------ */
enum ptp_container_type {
    PTP_TYPE_COMMAND = 1,
    PTP_TYPE_DATA = 2,
    PTP_TYPE_RESPONSE = 3,
    PTP_TYPE_EVENT = 4,
};

/* ------------------------------------------------------------------ */
/* 标准 PTP 操作码                                                     */
/* ------------------------------------------------------------------ */
enum ptp_opcode {
    PTP_OC_GetDeviceInfo = 0x1001,
    PTP_OC_OpenSession = 0x1002,
    PTP_OC_CloseSession = 0x1003,
    PTP_OC_GetStorageIDs = 0x1004,
    PTP_OC_GetStorageInfo = 0x1005,
    PTP_OC_GetNumObjects = 0x1006,
    PTP_OC_GetObjectHandles = 0x1007,
    PTP_OC_GetObjectInfo = 0x1008,
    PTP_OC_GetObject = 0x1009,
    PTP_OC_GetThumb = 0x100A,
    PTP_OC_DeleteObject = 0x100B,
    PTP_OC_SendObjectInfo = 0x100C,
    PTP_OC_SendObject = 0x100D,
    PTP_OC_InitiateCapture = 0x100E,
    PTP_OC_FormatStore = 0x100F,
    PTP_OC_ResetDevice = 0x1010,
    PTP_OC_GetDevicePropDesc = 0x1014,
    PTP_OC_GetDevicePropValue = 0x1015,
    PTP_OC_SetDevicePropValue = 0x1016,
    PTP_OC_ResetDevicePropValue = 0x1017,
    PTP_OC_TerminateOpenCapture = 0x1018,
    PTP_OC_GetPartialObject = 0x101B,
};

/* ------------------------------------------------------------------ */
/* 尼康扩展操作码                                                      */
/* ------------------------------------------------------------------ */
enum nikon_opcode {
    NK_OC_GetProfileAllData = 0x9006,
    NK_OC_SendProfileData = 0x9007,
    NK_OC_AdvancedTransfer = 0x9010,
    NK_OC_GetFileInfoInBlock = 0x9011,
    NK_OC_Capture = 0x90C0,
    NK_OC_SetControlMode = 0x90C2,
    NK_OC_LargeThumb = 0x90C4,
    NK_OC_CheckEvent = 0x90C7,
    NK_OC_DeviceReady = 0x90C8,
    NK_OC_CaptureInSDRAM = 0x90CB,
    NK_OC_GetDevicePTPIPInfo = 0x90E0,
    NK_OC_GetPreviewImg = 0x9200,
    NK_OC_StartLiveView = 0x9201,
    NK_OC_EndLiveView = 0x9202,
    NK_OC_GetLiveViewImg = 0x9203,
    NK_OC_MfDrive = 0x9204,
    NK_OC_ChangeAfArea = 0x9205,
    NK_OC_AfDriveCancel = 0x9206,
    NK_OC_CaptureToSdram = 0x9207,
    NK_OC_TerminateCapture = 0x920C,
};

/* ------------------------------------------------------------------ */
/* 标准响应码                                                          */
/* ------------------------------------------------------------------ */
enum ptp_response {
    PTP_RC_Undefined = 0x2000,
    PTP_RC_OK = 0x2001,
    PTP_RC_GeneralError = 0x2002,
    PTP_RC_SessionNotOpen = 0x2003,
    PTP_RC_InvalidTransactionID = 0x2004,
    PTP_RC_OperationNotSupported = 0x2005,
    PTP_RC_ParameterNotSupported = 0x2006,
    PTP_RC_IncompleteTransfer = 0x2007,
    PTP_RC_InvalidStorageID = 0x2008,
    PTP_RC_InvalidObjectHandle = 0x2009,
    PTP_RC_DevicePropNotSupported = 0x200A,
    PTP_RC_StoreFull = 0x200C,
    PTP_RC_AccessDenied = 0x200F,
    PTP_RC_NoThumbnailPresent = 0x2010,
    PTP_RC_DeviceBusy = 0x2019,
    PTP_RC_InvalidParameter = 0x201D,
    PTP_RC_SessionAlreadyOpen = 0x201E,
    PTP_RC_TransactionCanceled = 0x201F,
};

/* 尼康扩展响应码 */
enum nikon_response {
    NK_RC_HardwareError = 0xA001,
    NK_RC_OutOfFocus = 0xA002,
    NK_RC_ChangeCameraModeFailed = 0xA003,
    NK_RC_InvalidStatus = 0xA004,
    NK_RC_SetPropertyNotSupported = 0xA005,
    NK_RC_ShutterSpeedBulb = 0xA008,
    NK_RC_NotLiveView = 0xA00B,
};

/* ------------------------------------------------------------------ */
/* 尼康事件码                                                          */
/* ------------------------------------------------------------------ */
enum nikon_event {
    NK_EC_ObjectAddedInSDRAM = 0xC101,
    NK_EC_CaptureOverflow = 0xC102,
    NK_EC_AdvancedTransfer = 0xC103,
};

/* ------------------------------------------------------------------ */
/* 设备属性码                                                          */
/* ------------------------------------------------------------------ */
enum ptp_devprop {
    PTP_DPC_BatteryLevel = 0x5001,
    PTP_DPC_WhiteBalance = 0x5005,
    PTP_DPC_FNumber = 0x5007,
    PTP_DPC_FocalLength = 0x5008,
    PTP_DPC_FocusMode = 0x500A,
    PTP_DPC_ExposureMeteringMode = 0x500B,
    PTP_DPC_ExposureTime = 0x500D,
    PTP_DPC_ExposureProgramMode = 0x500E,
    PTP_DPC_ExposureIndex = 0x500F,
    PTP_DPC_ExposureBiasCompensation = 0x5010,
    PTP_DPC_StillCaptureMode = 0x5013,
};

enum nikon_devprop {
    NK_DPC_RawCompression = 0xD016,
    NK_DPC_WhiteBalanceColorTemperature = 0xD01E,
    NK_DPC_ImageSharpening = 0xD02A,
    NK_DPC_ToneCompensation = 0xD02B,
    NK_DPC_ColorModel = 0xD02C,
    NK_DPC_ISOAuto = 0xD054,
    NK_DPC_ExposureBiasCompensation = 0xD058,
    NK_DPC_ExposureTime = 0xD100,
    NK_DPC_FlashExposureCompensation = 0xD126,
    NK_DPC_OptimizeImage = 0xD140,
    NK_DPC_Saturation = 0xD142,
    NK_DPC_MovieExposureTime = 0xD1A8,
    NK_DPC_ActivePicCtrlItem = 0xD200,
    NK_DPC_ChangePicCtrlItem = 0xD201,
};

/* ------------------------------------------------------------------ */
/* 属性值类型码                                                        */
/* ------------------------------------------------------------------ */
enum ptp_datatype {
    PTP_DT_S8 = 0x0001,
    PTP_DT_U8 = 0x0002,
    PTP_DT_S16 = 0x0003,
    PTP_DT_U16 = 0x0004,
    PTP_DT_S32 = 0x0005,
    PTP_DT_U32 = 0x0006,
    PTP_DT_S64 = 0x0007,
    PTP_DT_U64 = 0x0008,
    PTP_DT_STRING = 0xFFFF,
};

/* ------------------------------------------------------------------ */
/* 拍摄模式 / 白平衡 / 对焦模式 常用取值                                */
/* ------------------------------------------------------------------ */
enum ptp_exposure_program { /* 0x500E */
    PTP_EP_MANUAL = 1,      /* M */
    PTP_EP_PROGRAM = 2,     /* P */
    PTP_EP_APERTURE = 3,    /* A */
    PTP_EP_SHUTTER = 4,     /* S */
};

enum ptp_white_balance { /* 0x5005 */
    PTP_WB_MANUAL = 0x0001,
    PTP_WB_AUTO = 0x0002,
    PTP_WB_ONE_PUSH_AUTO = 0x0003,
    PTP_WB_DAYLIGHT = 0x0004,
    PTP_WB_FLUORESCENT = 0x0005,
    PTP_WB_TUNGSTEN = 0x0006,
    PTP_WB_FLASH = 0x0007,
    PTP_WB_CLOUDY = 0x8010,
    PTP_WB_SHADE = 0x8011,
    PTP_WB_PRESET = 0x8013,
};

enum ptp_focus_mode { /* 0x500A */
    PTP_FM_MF = 0x0001,
    PTP_FM_AF_S = 0x8010,
    PTP_FM_AF_C = 0x8011,
};

/* 特殊句柄 */
#define PTP_NIKON_SDRAM_OBJECT_HANDLE 0xFFFF0001u

/* USB 厂商 VID */
#define PTP_VID_NIKON 0x04B0u
#define PTP_VID_CANON 0x04A9u
#define PTP_VID_SONY  0x054Cu

/* PTP/IP 默认端口 */
#define PTPIP_PORT 15740

/* 尼康握手时序 quirk：TCP 建连后需等待的毫秒数 */
#define NIKON_POST_CONNECT_DELAY_MS 350

#ifdef __cplusplus
}
#endif

#endif /* PTP_CONSTANTS_H */
