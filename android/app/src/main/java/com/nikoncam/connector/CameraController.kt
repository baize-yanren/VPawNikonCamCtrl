package com.nikoncam.connector

import android.hardware.usb.UsbManager
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** 相机中的对象（照片/视频）信息，由 native 层返回。 */
data class ObjectInfo(
    val handle: Int,
    val size: Long,
    val filename: String,
)

/** 尼康 PTP 设备属性码（与 core/src/ptp/ptp_constants.h 对应）。 */
object Props {
    const val EXPOSURE_TIME = 0x500D      // 快门（秒×10000）
    const val F_NUMBER = 0x5007           // 光圈（F×100）
    const val ISO = 0x500F                // ISO
    const val EXPOSURE_BIAS = 0x5010      // 曝光补偿（1/1000 EV）
    const val WHITE_BALANCE = 0x5005      // 白平衡
    const val FOCUS_MODE = 0x500A         // 对焦模式
    const val EXPOSURE_PROGRAM = 0x500E   // P/S/A/M
    const val PICTURE_CONTROL = 0xD200    // Picture Control
}

/**
 * 高层控制器：持有 USB transport + CamCore 会话，
 * 暴露连接 / 拍摄 / 实时取景 / 图库 / 参数 能力。
 */
class CameraController : NativeEventListener {

    sealed class State {
        object Idle : State()
        object Connecting : State()
        object Connected : State()
        data class Error(val code: Int) : State()
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state

    private var _lastCaptureHandle: Int = -1
    private var _lastError: Int = 0

    /** 连接 USB 相机。 */
    fun connectUsb(usbManager: UsbManager, device: android.hardware.usb.UsbDevice): Boolean {
        val t = UsbTransport.open(usbManager, device) ?: run {
            _state.value = State.Error(CamCoreJni.CC_ERR_IO)
            return false
        }
        CamCoreJni.nativeSetTransport(t, this)
        val created = CamCoreJni.nativeCreate(CamCoreJni.TRANSPORT_USB)
        if (created != CamCoreJni.CC_OK) {
            _state.value = State.Error(created)
            return false
        }
        _state.value = State.Connecting
        val rc = CamCoreJni.nativeConnect()
        if (rc != CamCoreJni.CC_OK) {
            _state.value = State.Error(rc)
            return false
        }
        _state.value = State.Connected
        return true
    }

    fun disconnect() {
        CamCoreJni.nativeDisconnect()
        _state.value = State.Idle
    }

    fun capture(): Boolean {
        val rc = CamCoreJni.nativeCapture()
        if (rc != CamCoreJni.CC_OK) { _lastError = rc; return false }
        return true
    }

    fun startLiveView(): Boolean = CamCoreJni.nativeStartLiveView() == CamCoreJni.CC_OK

    fun getLiveViewFrame(out: ByteArray): Int = CamCoreJni.nativeGetLiveViewFrame(out)

    fun endLiveView(): Boolean = CamCoreJni.nativeEndLiveView() == CamCoreJni.CC_OK

    fun listObjects(): List<ObjectInfo> =
        CamCoreJni.nativeListObjects().toList()

    fun download(handle: Int, path: String): Boolean =
        CamCoreJni.nativeDownload(handle, path) == CamCoreJni.CC_OK

    fun getParam(prop: Int): Int = CamCoreJni.nativeGetParam(prop)

    fun setParam(prop: Int, value: Int): Boolean =
        CamCoreJni.nativeSetParam(prop, value) == CamCoreJni.CC_OK

    /** 最近一次拍摄产生的对象句柄。 */
    val lastCaptureHandle: Int get() = _lastCaptureHandle

    /** 最近一次错误码。 */
    val lastError: Int get() = _lastError

    override fun onNativeEvent(type: Int, arg0: Int, arg1: Int) {
        when (type) {
            CamCoreJni.EVENT_CAPTURE_DONE -> _lastCaptureHandle = arg0
            CamCoreJni.EVENT_ERROR -> _lastError = arg0
            CamCoreJni.EVENT_CONNECTED -> _state.value = State.Connected
            CamCoreJni.EVENT_DISCONNECTED -> _state.value = State.Idle
        }
    }
}
