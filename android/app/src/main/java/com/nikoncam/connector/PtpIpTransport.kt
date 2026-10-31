package com.nikoncam.connector

import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException

/**
 * WiFi PTP/IP 传输（命令连接 TCP 字节流，端口 15740）。
 *
 * 本类只提供「命令连接字节流」的收发能力；PTP/IP 握手（InitCommandRequest/Ack）
 * 与帧编解码（OperationRequest/StartData/Data/EndData）由 native CamCore 的
 * `cc_ptpip_transport` 完成，因此与 USB 一样向核心暴露「裸 PTP 容器字节流」。
 *
 * 事件连接（端口 15740 的第二个连接）当前在 native 侧降级为 NULL，拍摄完成后
 * 通过 CheckEvent 轮询对象句柄，无需主动事件推送。
 */
class PtpIpTransport(
    private val host: String,
    private val port: Int = 15740,
    private val connectTimeoutMs: Int = 5000,
) : NativeTransport {

    private var socket: Socket? = null
    private var timeout = 2000

    override fun nativeConnect(): Int {
        return try {
            val s = Socket()
            s.tcpNoDelay = true
            s.connect(InetSocketAddress(host, port), connectTimeoutMs)
            s.soTimeout = timeout
            socket = s
            0
        } catch (e: Exception) {
            -1
        }
    }

    override fun nativeDisconnect(): Int {
        return try {
            socket?.close()
            socket = null
            0
        } catch (e: Exception) {
            -1
        }
    }

    override fun nativeSend(data: ByteArray): Int {
        val s = socket ?: return -1
        return try {
            s.getOutputStream().write(data)
            s.getOutputStream().flush()
            data.size
        } catch (e: Exception) {
            -1
        }
    }

    override fun nativeRecv(buf: ByteArray): Int {
        val s = socket ?: return -1
        return try {
            val n = s.getInputStream().read(buf)
            if (n < 0) 0 else n // 0 = EOF
        } catch (e: SocketTimeoutException) {
            0 // 超时按 EOF 处理，由核心判定为事务超时
        } catch (e: Exception) {
            -1
        }
    }

    override fun nativeSetTimeout(timeoutMs: Int): Int {
        timeout = timeoutMs
        return try {
            socket?.soTimeout = timeoutMs
            0
        } catch (e: Exception) {
            -1
        }
    }
}
