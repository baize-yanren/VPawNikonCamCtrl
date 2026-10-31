package com.nikoncam.connector

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import android.graphics.BitmapFactory

/**
 * 相机连接器主界面（Compose）：
 * USB 设备发现 → 连接 → 实时取景 + 快门 + 参数 + 图库。
 * 这是可运行的 UI 骨架，供真机联调。
 */
class MainActivity : ComponentActivity() {

    private val controller = CameraController()
    private val usbManager by lazy { getSystemService(Context.USB_SERVICE) as UsbManager }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
                findNikonDevice()?.let { requestPermission(it) }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        registerReceiver(usbReceiver, IntentFilter(UsbManager.ACTION_USB_DEVICE_ATTACHED))
        setContent {
            MaterialTheme {
                AppScreen(controller)
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
        val flags = PendingIntentFlagsCompat
        // 简化：直接尝试连接（部分机型/系统可无需显式授权对话框）
        if (usbManager.hasPermission(device)) {
            controller.connectUsb(usbManager, device)
        }
        // 完整实现需用 PendingIntent + ACTION_USB_PERMISSION；此处省略以保持骨架简洁。
    }
}

private object PendingIntentFlagsCompat { const val NONE = 0 }

@Composable
fun AppScreen(controller: CameraController) {
    val state by controller.state.collectAsState()
    val context = LocalContext.current

    var frames by remember { mutableStateOf<List<ObjectInfo>>(emptyList()) }
    var liveBitmap by remember { mutableStateOf<ImageBitmap?>(null) }

    Scaffold { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = when (state) {
                    is CameraController.State.Connected -> "已连接"
                    is CameraController.State.Connecting -> "连接中…"
                    is CameraController.State.Error -> "连接失败 (code=${(state as CameraController.State.Error).code})"
                    else -> "未连接（请接入尼康相机）"
                },
                style = MaterialTheme.typography.titleMedium,
            )

            Spacer(Modifier.height(12.dp))

            if (state is CameraController.State.Connected) {
                // 实时取景预览
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
                    Button(onClick = {
                        controller.startLiveView()
                        val buf = ByteArray(2 * 1024 * 1024)
                        val n = controller.getLiveViewFrame(buf)
                        if (n > 0) {
                            liveBitmap = BitmapFactory.decodeByteArray(buf, 0, n)?.asImageBitmap()
                        }
                    }) { Text("取景") }
                    Button(onClick = { controller.capture() }) { Text("快门") }
                }

                Spacer(Modifier.height(8.dp))

                Button(onClick = {
                    frames = controller.listObjects()
                }) { Text("刷新图库") }

                frames.take(50).forEach { obj ->
                    Row(modifier = Modifier.fillMaxWidth()) {
                        Text("${obj.filename}", modifier = Modifier.weight(1f))
                        Text("${obj.size / 1024} KB")
                        Button(onClick = {
                            controller.download(obj.handle, context.filesDir.absolutePath + "/" + obj.filename)
                        }) { Text("下载") }
                    }
                }
            }
        }
    }
}
