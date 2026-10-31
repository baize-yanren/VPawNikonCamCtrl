package com.nikoncam.connector

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.BitmapFactory
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** USB 授权结果广播 action（UsbManager 未暴露该常量，用规范字符串）。 */
private const val ACTION_USB_PERMISSION = "android.hardware.usb.action.USB_PERMISSION"

/**
 * 相机连接器主界面（Compose）：
 * USB/WiFi 设备连接 → 实时取景 + 快门 + 参数 + 图库浏览与下载。
 * 阻塞的 PTP 调用统一放到 Dispatchers.IO，避免主线程 ANR。
 */
class MainActivity : ComponentActivity() {

    private val controller = CameraController()
    private val usbManager by lazy { getSystemService(Context.USB_SERVICE) as UsbManager }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent == null) return
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    findNikonDevice()?.let { requestPermission(it) }
                }
                ACTION_USB_PERMISSION -> {
                    val device = intent.getParcelableExtraCompat<UsbDevice>(UsbManager.EXTRA_DEVICE)
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    if (granted && device != null) {
                        Thread { controller.connectUsb(usbManager, device) }.start()
                    }
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(ACTION_USB_PERMISSION)
        }
        registerReceiver(usbReceiver, filter)
        setContent {
            MaterialTheme {
                AppScreen(controller, usbManager, ::requestPermission)
            }
        }
        findNikonDevice()?.let { requestPermission(it) }
    }

    override fun onDestroy() {
        controller.disconnect()
        unregisterReceiver(usbReceiver)
        super.onDestroy()
    }

    private fun findNikonDevice(): UsbDevice? =
        usbManager.deviceList.values.firstOrNull { it.vendorId == UsbTransport.NIKON_VID }

    private fun requestPermission(device: UsbDevice) {
        if (usbManager.hasPermission(device)) {
            Thread { controller.connectUsb(usbManager, device) }.start()
            return
        }
        val pi = PendingIntent.getBroadcast(
            this, 0,
            Intent(ACTION_USB_PERMISSION).setPackage(packageName),
            PendingIntent.FLAG_IMMUTABLE,
        )
        usbManager.requestPermission(device, pi)
    }
}

/** 兼容 minSdk 26 的 getParcelableExtra（避免 API 33 才有的泛型重载）。 */
@Suppress("DEPRECATION")
private inline fun <reified T> Intent.getParcelableExtraCompat(name: String): T? =
    getParcelableExtra(name) as? T

@Composable
fun AppScreen(
    controller: CameraController,
    usbManager: UsbManager,
    requestPermission: (UsbDevice) -> Unit,
) {
    val state by controller.state.collectAsState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var frames by remember { mutableStateOf<List<ObjectInfo>>(emptyList()) }
    var liveBitmap by remember { mutableStateOf<ImageBitmap?>(null) }
    var wifiHost by remember { mutableStateOf("192.168.1.1") }
    var liveJob by remember { mutableStateOf<Job?>(null) }

    fun refreshGallery() {
        scope.launch(Dispatchers.IO) {
            frames = controller.listObjects()
        }
    }

    fun captureOnce() {
        scope.launch(Dispatchers.IO) { controller.capture() }
    }

    fun toggleLiveView() {
        val job = liveJob
        if (job != null && job.isActive) {
            job.cancel()
            liveJob = null
            scope.launch(Dispatchers.IO) { controller.endLiveView() }
            return
        }
        liveJob = scope.launch(Dispatchers.IO) {
            if (!controller.startLiveView()) return@launch
            val buf = ByteArray(2 * 1024 * 1024)
            while (isActive) {
                val n = controller.getLiveViewFrame(buf)
                if (n > 0) {
                    val bmp = BitmapFactory.decodeByteArray(buf, 0, n)?.asImageBitmap()
                    if (bmp != null) liveBitmap = bmp
                }
                delay(100)
            }
            controller.endLiveView()
        }
    }

    fun connectWifi() {
        scope.launch(Dispatchers.IO) { controller.connectWifi(wifiHost.trim()) }
    }

    fun connectUsb() {
        val device = usbManager.deviceList.values.firstOrNull { it.vendorId == UsbTransport.NIKON_VID }
        if (device != null) requestPermission(device)
    }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = when (val s = state) {
                    is CameraController.State.Connected -> "已连接"
                    is CameraController.State.Connecting -> "连接中…"
                    is CameraController.State.Error -> "连接失败 (code=${s.code})"
                    else -> "未连接（请接入尼康相机或输入相机 IP）"
                },
                style = MaterialTheme.typography.titleMedium,
            )

            Spacer(Modifier.height(12.dp))

            Row(
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Button(onClick = { connectUsb() }, enabled = state !is CameraController.State.Connected) {
                    Text("USB 连接")
                }
            }
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                OutlinedTextField(
                    value = wifiHost,
                    onValueChange = { wifiHost = it },
                    label = { Text("相机 IP") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
                Button(
                    onClick = { connectWifi() },
                    enabled = state !is CameraController.State.Connected,
                ) { Text("WiFi 连接") }
            }

            Spacer(Modifier.height(8.dp))

            if (state is CameraController.State.Connected) {
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f),
                    contentAlignment = Alignment.Center,
                ) {
                    liveBitmap?.let { Image(it, contentDescription = "实时取景") }
                }

                Spacer(Modifier.height(8.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    Button(onClick = { toggleLiveView() }) {
                        Text(if (liveJob?.isActive == true) "停止取景" else "实时取景")
                    }
                    Button(onClick = { captureOnce() }) { Text("快门") }
                }

                Spacer(Modifier.height(8.dp))

                Button(onClick = { refreshGallery() }) { Text("刷新图库") }

                frames.take(50).forEach { obj ->
                    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        Text(obj.filename, modifier = Modifier.weight(1f))
                        Text("${obj.size / 1024} KB")
                        Button(onClick = {
                            val path = context.filesDir.absolutePath + "/" + obj.filename
                            scope.launch(Dispatchers.IO) { controller.download(obj.handle, path) }
                        }) { Text("下载") }
                    }
                }
            }
        }
    }
}
