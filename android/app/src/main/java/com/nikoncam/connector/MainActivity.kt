package com.nikoncam.connector

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** USB 授权结果广播 action（UsbManager 未暴露该常量，用规范字符串）。 */
private const val ACTION_USB_PERMISSION = "android.hardware.usb.action.USB_PERMISSION"

/**
 * 相机连接器主界面：悬浮胶囊 Dock（连接/遥控/图库/设置）+ 内容区限宽 480dp。
 * 视觉范式对齐 VPawCamCtrl：单一主题色驱动、16 圆角卡片、胶囊按钮、Dock 独占投影。
 */
class MainActivity : ComponentActivity() {

    private val controller = CameraController()
    private val usbManager by lazy { getSystemService(Context.USB_SERVICE) as UsbManager }

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action != ACTION_USB_PERMISSION) return
            val device = intent.getParcelableExtraCompat<UsbDevice>(UsbManager.EXTRA_DEVICE)
            val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
            if (granted && device != null) {
                Thread { controller.connectUsb(usbManager, device) }.start()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // Android 14+ 动态注册自定义广播必须显式指定 exported flag，否则抛 SecurityException 闪退。
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(usbReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            registerReceiver(usbReceiver, filter)
        }

        setContent {
            val theme = remember { AppThemeState() }
            NikonTheme(theme) {
                AppRoot(controller, usbManager, ::requestPermission)
            }
        }
        findNikonDevice()?.let { requestPermission(it) }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.action == UsbManager.ACTION_USB_DEVICE_ATTACHED) {
            val device = intent.getParcelableExtraCompat<UsbDevice>(UsbManager.EXTRA_DEVICE)
                ?: findNikonDevice()
            device?.let { requestPermission(it) }
        }
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

/** 应用根：内容区（限宽 480dp）+ 悬浮胶囊 Dock。 */
@Composable
fun AppRoot(
    controller: CameraController,
    usbManager: UsbManager,
    requestPermission: (UsbDevice) -> Unit,
) {
    val colors = LocalNikonColors.current
    var currentTab by remember { mutableStateOf(0) }

    Box(Modifier.fillMaxSize().background(colors.bgPage)) {
        Box(
            Modifier
                .fillMaxSize()
                .statusBarsPadding()
                .widthIn(max = 480.dp)
                .align(Alignment.TopCenter)
        ) {
            when (currentTab) {
                0 -> ConnectScreen(controller, usbManager, requestPermission)
                1 -> RemoteScreen(controller, onGoConnect = { currentTab = 0 })
                2 -> GalleryScreen(controller, onGoConnect = { currentTab = 0 })
                3 -> SettingsScreen()
            }
        }
        DockBar(currentTab, onSelect = { currentTab = it }, Modifier.align(Alignment.BottomCenter))
    }
}

/** 悬浮胶囊 Dock：底部横向，4 个 Tab 均分。 */
@Composable
fun DockBar(current: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    val colors = LocalNikonColors.current
    val tabs = listOf(
        "🔧" to "连接",
        "📷" to "遥控",
        "🖼️" to "图库",
        "⚙️" to "设置",
    )
    Row(
        modifier = modifier
            .navigationBarsPadding()
            .padding(horizontal = 16.dp, vertical = 10.dp)
            .fillMaxWidth()
            .widthIn(max = 480.dp)
            .height(64.dp)
            .clip(RoundedCornerShape(32.dp))
            .background(colors.dockBg)
            .shadow(20.dp, RoundedCornerShape(32.dp)),
        horizontalArrangement = Arrangement.SpaceEvenly,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        tabs.forEachIndexed { i, (icon, label) ->
            val selected = current == i
            val tint = if (selected) colors.themeColor else colors.textFaint
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(2.dp),
                modifier = Modifier
                    .clip(RoundedCornerShape(16.dp))
                    .clickable { onSelect(i) }
                    .padding(horizontal = 8.dp, vertical = 8.dp),
            ) {
                Text(icon, fontSize = 22.sp, color = tint)
                Text(label, fontSize = 11.sp, color = tint)
            }
        }
    }
}
