package com.nikoncam.connector

import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.HorizontalDivider
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
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale

/** 参数档位表与格式化（尼康常见档位，PTP 值按「秒×10000 / F×100 / EV×1000」编码）。 */
object ParamSpec {
    val shutterStops = listOf(1, 2, 5, 10, 20, 40, 80, 166, 333, 666, 1250, 2500, 5000, 10000, 20000, 40000, 80000, 150000, 300000)
    val apertureStops = listOf(140, 180, 200, 280, 350, 400, 560, 800, 1100, 1600, 2200, 3200)
    val isoStops = listOf(100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000, 10000, 12800, 16000, 20000, 25600, 51200)
    val biasStops = (-15..15).map { it * 333 } // ±5EV，1/3 EV 步进

    fun stops(prop: Int): List<Int> = when (prop) {
        Props.EXPOSURE_TIME -> shutterStops
        Props.F_NUMBER -> apertureStops
        Props.ISO -> isoStops
        Props.EXPOSURE_BIAS -> biasStops
        else -> emptyList()
    }

    fun format(prop: Int, v: Int): String = when (prop) {
        Props.EXPOSURE_TIME -> if (v <= 0) "—" else if (v >= 10000) "${v / 10000}s" else "1/${(10000 + v / 2) / v}s"
        Props.F_NUMBER -> if (v <= 0) "—" else "f/${v / 100}.${(v % 100) / 10}"
        Props.ISO -> if (v <= 0) "—" else v.toString()
        Props.EXPOSURE_BIAS -> {
            if (v == 0) "0.0"
            else {
                val ev = v / 1000.0
                (if (v > 0) "+" else "") + String.format(Locale.US, "%.1f", ev)
            }
        }
        Props.WHITE_BALANCE -> wbName(v)
        else -> v.toString()
    }

    fun wbName(v: Int): String = when (v) {
        0x0002 -> "自动"
        0x0004 -> "白炽灯"
        0x0005 -> "荧光灯"
        0x0006 -> "晴天"
        0x0007 -> "闪光灯"
        0x0008 -> "阴天"
        0x0009 -> "阴影"
        0x8011 -> "手动预设"
        else -> "自定义($v)"
    }
}

@Composable
fun RemoteScreen(controller: CameraController, onGoConnect: () -> Unit) {
    val colors = LocalNikonColors.current
    val state by controller.state.collectAsState()
    val scope = rememberCoroutineScope()

    var liveBitmap by remember { mutableStateOf<ImageBitmap?>(null) }
    var liveOn by remember { mutableStateOf(false) }
    var feedback by remember { mutableStateOf("") }
    var feedbackOk by remember { mutableStateOf(false) }
    var params by remember { mutableStateOf(mapOf<Int, Int>()) }

    fun readParams() {
        scope.launch(Dispatchers.IO) {
            val m = mutableMapOf<Int, Int>()
            for (p in listOf(Props.F_NUMBER, Props.EXPOSURE_TIME, Props.ISO, Props.WHITE_BALANCE, Props.EXPOSURE_BIAS)) {
                m[p] = controller.getParam(p)
            }
            params = m
        }
    }

    fun stepParam(prop: Int, dir: Int) {
        scope.launch(Dispatchers.IO) {
            val stops = ParamSpec.stops(prop)
            if (stops.isEmpty()) return@launch
            val cur = controller.getParam(prop)
            val idx = stops.indexOf(cur)
            val newIdx = when {
                idx >= 0 -> (if (dir < 0) idx - 1 else idx + 1).coerceIn(0, stops.size - 1)
                else -> {
                    val near = stops.indexOfFirst { it > cur }
                    if (dir < 0) (if (near < 0) stops.size - 1 else near - 1).coerceAtLeast(0)
                    else (if (near < 0) stops.size - 1 else near).coerceAtMost(stops.size - 1)
                }
            }
            val newVal = stops[newIdx]
            if (controller.setParam(prop, newVal)) {
                params = params + (prop to newVal)
                feedback = "参数已更新"
                feedbackOk = true
            } else {
                feedback = "参数更新失败"
                feedbackOk = false
            }
        }
    }

    fun toggleLiveView() {
        if (liveOn) {
            liveOn = false
            scope.launch(Dispatchers.IO) { controller.endLiveView() }
            liveBitmap = null
        } else {
            liveOn = true
            scope.launch(Dispatchers.IO) {
                if (!controller.startLiveView()) {
                    liveOn = false
                    feedback = "开启实时取景失败"
                    feedbackOk = false
                    return@launch
                }
                val buf = ByteArray(2 * 1024 * 1024)
                while (liveOn) {
                    val n = controller.getLiveViewFrame(buf)
                    if (n > 0) {
                        BitmapFactory.decodeByteArray(buf, 0, n)?.asImageBitmap()?.let { liveBitmap = it }
                    }
                    delay(66) // ~15fps
                }
                controller.endLiveView()
            }
        }
    }

    fun shoot() {
        scope.launch(Dispatchers.IO) {
            if (controller.capture()) {
                feedback = "已拍摄"
                feedbackOk = true
            } else {
                feedback = "拍摄失败"
                feedbackOk = false
            }
        }
    }

    if (state !is CameraController.State.Connected) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("📷", fontSize = 48.sp)
                Spacer(Modifier.height(12.dp))
                Text("相机未连接", fontSize = 16.sp, fontWeight = FontWeight.Medium, color = colors.textMain)
                Spacer(Modifier.height(6.dp))
                Text("连接相机后即可远程对焦与拍摄", fontSize = 13.sp, color = colors.textSub)
                Spacer(Modifier.height(16.dp))
                CapsuleButton(
                    "去连接", onClick = onGoConnect,
                    bg = colors.primaryBtn, fg = colors.primaryBtnText,
                    modifier = Modifier.widthIn(max = 200.dp), height = 44.dp, radius = 22.dp, fontSize = 15,
                )
            }
        }
        return
    }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp)
    ) {
        // 标题栏
        Row(Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("遥控", fontSize = 22.sp, fontWeight = FontWeight.Bold, color = colors.textMain, modifier = Modifier.weight(1f))
        }

        // 设备摘要
        Row(Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
            Text("尼康相机", fontSize = 15.sp, fontWeight = FontWeight.Medium, color = colors.textMain, modifier = Modifier.weight(1f))
        }

        // 取景面板
        LiveViewPanel(liveBitmap, liveOn)

        Spacer(Modifier.height(12.dp))

        // 控制按钮区
        ControlBar(liveOn, onToggleLive = { toggleLiveView() }, onShoot = { shoot() })

        // 反馈行
        Spacer(Modifier.height(10.dp))
        if (feedback.isNotEmpty()) {
            Text(feedback, fontSize = 14.sp, color = if (feedbackOk) colors.ok else colors.err, modifier = Modifier.fillMaxWidth())
        } else {
            Text("实时取景 · 快门 · 拍摄参数", fontSize = 12.sp, color = colors.textSub, modifier = Modifier.fillMaxWidth())
        }

        Spacer(Modifier.height(12.dp))

        // 拍摄参数卡
        ParamsCard(params, onRead = { readParams() }, onStep = { p, d -> stepParam(p, d) })

        Spacer(Modifier.height(96.dp))
    }
}

@Composable
private fun LiveViewPanel(bitmap: ImageBitmap?, on: Boolean) {
    val colors = LocalNikonColors.current
    Box(
        Modifier
            .fillMaxWidth()
            .aspectRatio(1.5f)
            .clip(RoundedCornerShape(16.dp))
            .background(Color(0xFF111111))
    ) {
        if (bitmap != null) {
            Image(bitmap, contentDescription = "实时取景", modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
        } else {
            Column(Modifier.align(Alignment.Center), horizontalAlignment = Alignment.CenterHorizontally) {
                Text(if (on) "正在开启实时取景…" else "实时取景未开启", fontSize = 13.sp, color = Color(0xFF999999))
            }
        }
        if (on) {
            Text(
                "LIVE",
                fontSize = 10.sp,
                color = Color.White,
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .padding(8.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(colors.record)
                    .padding(horizontal = 8.dp, vertical = 3.dp),
            )
        }
    }
}

@Composable
private fun ControlBar(liveOn: Boolean, onToggleLive: () -> Unit, onShoot: () -> Unit) {
    val colors = LocalNikonColors.current
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // 对焦竖条
        Box(
            Modifier
                .width(52.dp)
                .height(76.dp)
                .clip(RoundedCornerShape(26.dp))
                .background(colors.primarySoft)
                .clickable { },
            contentAlignment = Alignment.Center,
        ) {
            Text("对\n焦", fontSize = 12.sp, color = colors.textSub, textAlign = TextAlign.Center)
        }

        Spacer(Modifier.width(14.dp))

        // 快门圆钮
        Box(
            Modifier
                .size(76.dp)
                .clip(CircleShape)
                .background(colors.primaryBtn)
                .clickable { onShoot() },
            contentAlignment = Alignment.Center,
        ) {
            Text("快门", fontSize = 15.sp, fontWeight = FontWeight.Medium, color = colors.primaryBtnText)
        }

        Spacer(Modifier.width(14.dp))

        // 右侧竖排
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            CapsuleButton(
                text = if (liveOn) "关闭取景" else "开启取景",
                onClick = onToggleLive,
                bg = if (liveOn) colors.errBtn else colors.primaryBtn,
                fg = Color.White,
                height = 34.dp, radius = 17.dp, fontSize = 12,
            )
            CapsuleButton(
                text = "切换录像",
                onClick = { },
                bg = colors.primaryBtn,
                fg = colors.primaryBtnText,
                height = 34.dp, radius = 17.dp, fontSize = 12,
            )
        }
    }
}

@Composable
private fun ParamsCard(params: Map<Int, Int>, onRead: () -> Unit, onStep: (Int, Int) -> Unit) {
    val colors = LocalNikonColors.current
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(colors.bgCard)
            .padding(14.dp)
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("◉", fontSize = 14.sp, color = colors.primary)
            Spacer(Modifier.width(8.dp))
            Text("拍摄参数", fontSize = 16.sp, fontWeight = FontWeight.Bold, color = colors.textMain, modifier = Modifier.weight(1f))
            ChipButton("刷新", onClick = onRead, bg = colors.primarySoft, fg = colors.primary, radius = 12.dp, fontSize = 12)
        }
        Spacer(Modifier.height(4.dp))
        val rows = listOf(
            Triple("光圈", Props.F_NUMBER, ParamSpec.apertureStops),
            Triple("快门", Props.EXPOSURE_TIME, ParamSpec.shutterStops),
            Triple("ISO", Props.ISO, ParamSpec.isoStops),
            Triple("白平衡", Props.WHITE_BALANCE, emptyList<Int>()),
            Triple("曝光补偿", Props.EXPOSURE_BIAS, ParamSpec.biasStops),
        )
        rows.forEachIndexed { i, (label, prop, stops) ->
            if (i > 0) HorizontalDivider(color = colors.border, modifier = Modifier.padding(horizontal = 4.dp))
            ParamRow(label, prop, stops, value = params[prop], onStep = { d -> onStep(prop, d) })
        }
    }
}

@Composable
private fun ParamRow(label: String, prop: Int, stops: List<Int>, value: Int?, onStep: (Int) -> Unit) {
    val colors = LocalNikonColors.current
    val display = value?.let { ParamSpec.format(prop, it) } ?: "—"
    Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, fontSize = 13.sp, color = colors.textSub, modifier = Modifier.width(64.dp))
        if (stops.isEmpty()) {
            Text(display, fontSize = 14.sp, color = colors.textFaint, modifier = Modifier.weight(1f))
        } else {
            StepBtn("<") { onStep(-1) }
            Text(
                display,
                fontSize = 15.sp,
                fontWeight = FontWeight.Medium,
                color = colors.primary,
                modifier = Modifier.weight(1f),
                textAlign = TextAlign.Center,
            )
            StepBtn(">") { onStep(1) }
        }
    }
}
