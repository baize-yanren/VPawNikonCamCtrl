package com.nikoncam.connector

/**
 * CamCore 的 JNI 声明与常量。
 * native 实现在 android/bridge/camcore_jni.c，由 NDK 编译并链接 CamCore 静态库。
 */
object CamCoreJni {

    init {
        System.loadLibrary("camcore")
    }

    /* 事件类型（与 camcore.h cc_event_type_t 对应） */
    const val EVENT_CONNECTED = 1
    const val EVENT_DISCONNECTED = 2
    const val EVENT_CAPTURE_DONE = 3
    const val EVENT_OBJECT_ADDED = 4
    const val EVENT_PROGRESS = 5
    const val EVENT_ERROR = 6

    /* 状态码（与 camcore.h cc_status_t 对应） */
    const val CC_OK = 0
    const val CC_ERR_NOT_CONNECTED = -2
    const val CC_ERR_TIMEOUT = -3
    const val CC_ERR_PROTOCOL = -4
    const val CC_ERR_DEVICE_BUSY = -5
    const val CC_ERR_UNSUPPORTED = -6
    const val CC_ERR_IO = -7

    /* 传输类型 */
    const val TRANSPORT_USB = 1
    const val TRANSPORT_PTPIP = 2

    external fun nativeSetTransport(transport: NativeTransport, listener: NativeEventListener)
    external fun nativeCreate(kind: Int): Int
    /** 以 PTP/IP（WiFi）方式创建会话：包装 TCP 命令字节流，握手与帧编解码在 native 完成。 */
    external fun nativeCreatePtpIp(transport: NativeTransport, listener: NativeEventListener): Int
    external fun nativeConnect(): Int
    external fun nativeDisconnect(): Int
    external fun nativeCapture(): Int
    external fun nativeStartLiveView(): Int
    external fun nativeGetLiveViewFrame(out: ByteArray): Int
    external fun nativeEndLiveView(): Int
    external fun nativeDownload(handle: Int, path: String): Int
    external fun nativeListObjects(): Array<ObjectInfo>?
    external fun nativeGetParam(prop: Int): Int
    external fun nativeSetParam(prop: Int, value: Int): Int
    external fun nativeDestroy()
}

/** Transport 接口：由 USB / WiFi 适配实现，注入 native。 */
interface NativeTransport {
    fun nativeConnect(): Int
    fun nativeDisconnect(): Int
    fun nativeSend(data: ByteArray): Int
    fun nativeRecv(buf: ByteArray): Int
    fun nativeSetTimeout(timeoutMs: Int): Int
}

/** 事件回调：native 经此回传连接/拍摄/下载进度。 */
interface NativeEventListener {
    fun onNativeEvent(type: Int, arg0: Int, arg1: Int)
}
