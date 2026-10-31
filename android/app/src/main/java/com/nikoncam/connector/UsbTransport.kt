package com.nikoncam.connector

import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager

/**
 * USB-PTP Transport：用 UsbDeviceConnection.bulkTransfer 收发容器字节。
 * 接口 class=6（Still Imaging），批量 IN/OUT 端点，见 docs/PROTOCOL.md §10。
 */
class UsbTransport(
    private val device: UsbDevice,
    private val connection: UsbDeviceConnection,
    private val ptpInterface: UsbInterface,
    private val epIn: UsbEndpoint,
    private val epOut: UsbEndpoint,
) : NativeTransport {

    private var timeout = 1000

    override fun nativeConnect(): Int {
        if (!connection.claimInterface(ptpInterface, true)) return -1
        return 0
    }

    override fun nativeDisconnect(): Int {
        connection.releaseInterface(ptpInterface)
        connection.close()
        return 0
    }

    override fun nativeSend(data: ByteArray): Int {
        val n = connection.bulkTransfer(epOut, data, data.size, timeout)
        return if (n < 0) -1 else n
    }

    override fun nativeRecv(buf: ByteArray): Int {
        val n = connection.bulkTransfer(epIn, buf, buf.size, timeout)
        return if (n < 0) -1 else n
    }

    override fun nativeSetTimeout(timeoutMs: Int): Int {
        timeout = timeoutMs
        return 0
    }

    companion object {
        const val NIKON_VID = 0x04B0
        const val STILL_IMAGE_CLASS = 6

        /** 打开尼康 USB-PTP 设备；失败返回 null。 */
        fun open(usbManager: UsbManager, device: UsbDevice): UsbTransport? {
            if (device.vendorId != NIKON_VID) return null

            val itf = (0 until device.interfaceCount)
                .map { device.getInterface(it) }
                .firstOrNull { it.interfaceClass == STILL_IMAGE_CLASS }
                ?: return null

            var epIn: UsbEndpoint? = null
            var epOut: UsbEndpoint? = null
            for (i in 0 until itf.endpointCount) {
                val ep = itf.getEndpoint(i)
                when (ep.type) {
                    UsbConstants.USB_ENDPOINT_XFER_BULK -> {
                        if (ep.direction == UsbConstants.USB_DIR_IN) epIn = ep
                        else epOut = ep
                    }
                }
            }
            if (epIn == null || epOut == null) return null

            val conn = usbManager.openDevice(device) ?: return null
            return UsbTransport(device, conn, itf, epIn!!, epOut!!)
        }
    }
}
