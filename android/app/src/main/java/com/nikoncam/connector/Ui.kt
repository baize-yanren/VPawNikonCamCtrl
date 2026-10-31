package com.nikoncam.connector

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/** 胶囊按钮（主按钮 / 次要按钮）。宽度由调用方 modifier 控制。 */
@Composable
fun CapsuleButton(
    text: String,
    onClick: () -> Unit,
    bg: Color,
    fg: Color,
    modifier: Modifier = Modifier,
    height: Dp = 48.dp,
    radius: Dp = 24.dp,
    fontSize: Int = 16,
    enabled: Boolean = true,
) {
    Box(
        modifier = modifier
            .height(height)
            .clip(RoundedCornerShape(radius))
            .background(bg)
            .clickable(enabled = enabled) { onClick() }
            .padding(horizontal = 20.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = fg, fontSize = fontSize.sp, fontWeight = FontWeight.Medium, maxLines = 1)
    }
}

/** 主题色 chip 按钮（标题栏操作、标签等）。 */
@Composable
fun ChipButton(
    text: String,
    onClick: () -> Unit,
    bg: Color,
    fg: Color,
    modifier: Modifier = Modifier,
    radius: Dp = 16.dp,
    fontSize: Int = 13,
) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(radius))
            .background(bg)
            .clickable { onClick() }
            .padding(horizontal = 14.dp, vertical = 7.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, fontSize = fontSize.sp, color = fg, fontWeight = FontWeight.Medium, maxLines = 1)
    }
}

/** 圆形步进按钮（参数调节器用）。 */
@Composable
fun StepBtn(symbol: String, onClick: () -> Unit) {
    val colors = LocalNikonColors.current
    Box(
        modifier = Modifier
            .size(34.dp)
            .clip(CircleShape)
            .background(colors.primarySoft)
            .clickable { onClick() },
        contentAlignment = Alignment.Center,
    ) {
        Text(symbol, fontSize = 16.sp, color = colors.primary)
    }
}

/** 状态圆点。 */
@Composable
fun StatusDot(color: Color, size: Dp = 7.dp) {
    Box(
        Modifier
            .size(size)
            .clip(CircleShape)
            .background(color)
    )
}

/** PTP/CamCore 状态码 → 中文文案。 */
fun ccErrorText(code: Int): String = when (code) {
    CamCoreJni.CC_ERR_NOT_CONNECTED -> "未连接相机"
    CamCoreJni.CC_ERR_TIMEOUT -> "连接超时，请检查相机是否响应"
    CamCoreJni.CC_ERR_PROTOCOL -> "协议错误，请重试"
    CamCoreJni.CC_ERR_DEVICE_BUSY -> "相机忙，请稍后重试"
    CamCoreJni.CC_ERR_UNSUPPORTED -> "当前机型不支持该操作"
    CamCoreJni.CC_ERR_IO -> "I/O 错误，请检查线材与连接"
    CamCoreJni.CC_ERR_SESSION -> "会话未打开，请重新连接"
    else -> "错误码 $code"
}
