package com.nikoncam.connector

import android.hardware.usb.UsbManager
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/** 连接方式 */
enum class Method { USB, BLE, AP, STA }

private fun Method.label(): String = when (this) {
    Method.USB -> "USB"
    Method.BLE -> "蓝牙"
    Method.AP -> "AP"
    Method.STA -> "STA"
}

@Composable
fun ConnectScreen(
    controller: CameraController,
    usbManager: UsbManager,
    requestPermission: (android.hardware.usb.UsbDevice) -> Unit,
) {
    val colors = LocalNikonColors.current
    val state by controller.state.collectAsState()
    val scope = rememberCoroutineScope()
    var method by remember { mutableStateOf(Method.USB) }
    var host by remember { mutableStateOf("192.168.1.1") }

    val usbPresent = usbManager.deviceList.values.any { it.vendorId == UsbTransport.NIKON_VID }

    fun connect() {
        when (method) {
            Method.USB -> {
                val d = usbManager.deviceList.values.firstOrNull { it.vendorId == UsbTransport.NIKON_VID }
                if (d != null) requestPermission(d)
            }
            Method.AP -> scope.launch(Dispatchers.IO) { controller.connectWifi("192.168.1.1") }
            Method.STA -> scope.launch(Dispatchers.IO) { controller.connectWifi(host.trim()) }
            Method.BLE -> Unit // 蓝牙暂未支持
        }
    }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp)
    ) {
        // 标题栏 + 状态 chip
        Row(
            Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                "连接相机",
                fontSize = 22.sp,
                fontWeight = FontWeight.Bold,
                color = colors.textMain,
                modifier = Modifier.weight(1f),
            )
            val st = state
            if (st !is CameraController.State.Idle) {
                val (txt, col) = when (st) {
                    is CameraController.State.Connected -> "已连接" to colors.ok
                    is CameraController.State.Connecting -> "连接中…" to colors.warn
                    is CameraController.State.Error -> "连接失败" to colors.err
                    else -> "" to colors.textSub
                }
                Row(
                    Modifier
                        .clip(RoundedCornerShape(12.dp))
                        .background(colors.primarySoft)
                        .padding(horizontal = 12.dp, vertical = 5.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    StatusDot(col)
                    Spacer(Modifier.size(5.dp))
                    Text(txt, fontSize = 12.sp, color = col)
                }
            }
        }

        Spacer(Modifier.height(10.dp))

        // 连接方式选择
        Column(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(colors.bgCard)
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Method.values().forEach { m ->
                    MethodCard(m, selected = method == m, onClick = { method = m }, Modifier.weight(1f))
                }
            }
            if (method == Method.USB && usbPresent) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    StatusDot(colors.ok)
                    Spacer(Modifier.size(6.dp))
                    Text("已检测到 USB 设备", fontSize = 13.sp, color = colors.ok)
                }
            }
        }

        // STA 提示 + IP 输入
        if (method == Method.STA) {
            Spacer(Modifier.height(12.dp))
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.bgCard)
                    .padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text(
                    "相机需设为「连接至计算机」并用电脑端 WTU 配对；「连接至智能设备」是 SnapBridge 专用，不响应 PTP/IP",
                    fontSize = 11.sp,
                    color = colors.warn,
                )
                OutlinedTextField(
                    value = host,
                    onValueChange = { host = it },
                    label = { Text("相机 IP", color = colors.textFaint) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                    textStyle = TextStyle(color = colors.textMain, fontSize = 15.sp),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = colors.primary,
                        unfocusedBorderColor = colors.border,
                        focusedLabelColor = colors.primary,
                        unfocusedLabelColor = colors.textFaint,
                        cursorColor = colors.primary,
                        focusedContainerColor = colors.inputBg,
                        unfocusedContainerColor = colors.inputBg,
                    ),
                )
            }
        }

        // 蓝牙占位提示
        if (method == Method.BLE) {
            Spacer(Modifier.height(12.dp))
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.bgCard)
                    .padding(14.dp)
            ) {
                Text("蓝牙遥控暂未支持，请使用 USB 或 WiFi 连接", fontSize = 12.sp, color = colors.textFaint)
            }
        }

        Spacer(Modifier.height(12.dp))

        // 主连接按钮
        val canConnect = state !is CameraController.State.Connected &&
            state !is CameraController.State.Connecting
        val btnText = when (state) {
            is CameraController.State.Connecting -> "连接中…"
            is CameraController.State.Connected -> "已连接"
            else -> "连接"
        }
        CapsuleButton(
            text = btnText,
            onClick = { connect() },
            bg = if (canConnect) colors.primaryBtn else colors.btnDisabled,
            fg = if (canConnect) colors.primaryBtnText else colors.btnDisabledText,
            modifier = Modifier.fillMaxWidth(),
            enabled = canConnect,
        )

        // 错误反馈
        val err = state as? CameraController.State.Error
        if (err != null) {
            Spacer(Modifier.height(8.dp))
            Text(ccErrorText(err.code), fontSize = 12.sp, color = colors.err, modifier = Modifier.fillMaxWidth())
        }

        // 已连接：设备信息 + 断开
        if (state is CameraController.State.Connected) {
            Spacer(Modifier.height(12.dp))
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(colors.bgCard)
                    .padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                InfoRow("型号", "尼康相机")
                InfoRow("连接方式", if (method == Method.USB) "USB (PTP)" else "WiFi (PTP/IP)")
                InfoRow("状态", "已连接")
            }
            Spacer(Modifier.height(12.dp))
            CapsuleButton(
                text = "断开连接",
                onClick = { scope.launch(Dispatchers.IO) { controller.disconnect() } },
                bg = colors.errBtn,
                fg = Color.White,
                modifier = Modifier.fillMaxWidth(),
                height = 46.dp,
                radius = 23.dp,
                fontSize = 15,
            )
        }

        Spacer(Modifier.height(96.dp)) // 底部 Dock 留白
    }
}

@Composable
private fun MethodCard(method: Method, selected: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier) {
    val colors = LocalNikonColors.current
    Box(
        modifier = modifier
            .height(44.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(if (selected) colors.selectedBg else Color.Transparent)
            .border(1.5.dp, if (selected) colors.primary else colors.border, RoundedCornerShape(12.dp))
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) {
        Text(
            method.label(),
            fontSize = 14.sp,
            fontWeight = FontWeight.Medium,
            maxLines = 1,
            color = if (selected) colors.selectedText else colors.textMain,
        )
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    val colors = LocalNikonColors.current
    Row(Modifier.fillMaxWidth()) {
        Text(label, fontSize = 13.sp, color = colors.textSub, modifier = Modifier.weight(1f))
        Text(value, fontSize = 13.sp, color = colors.textMain)
    }
}
