package com.nikoncam.connector

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.util.Locale

@Composable
fun GalleryScreen(controller: CameraController, onGoConnect: () -> Unit) {
    val colors = LocalNikonColors.current
    val context = LocalContext.current
    val state by controller.state.collectAsState()
    val scope = rememberCoroutineScope()
    var frames by remember { mutableStateOf<List<ObjectInfo>>(emptyList()) }
    var loading by remember { mutableStateOf(false) }
    var feedback by remember { mutableStateOf("") }
    var feedbackOk by remember { mutableStateOf(false) }

    fun refresh() {
        scope.launch(Dispatchers.IO) {
            loading = true
            frames = controller.listObjects()
            loading = false
        }
    }

    fun download(obj: ObjectInfo) {
        scope.launch(Dispatchers.IO) {
            val path = context.filesDir.absolutePath + "/" + obj.filename
            if (controller.download(obj.handle, path)) {
                feedback = "已下载 ${obj.filename}"
                feedbackOk = true
            } else {
                feedback = "下载失败 ${obj.filename}"
                feedbackOk = false
            }
        }
    }

    Column(Modifier.fillMaxSize().padding(horizontal = 16.dp)) {
        // 标题栏
        Row(Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("图库", fontSize = 22.sp, fontWeight = FontWeight.Bold, color = colors.textMain, modifier = Modifier.weight(1f))
            if (state is CameraController.State.Connected) {
                ChipButton("刷新", onClick = { refresh() }, bg = colors.primarySoft, fg = colors.primary)
            }
        }

        if (feedback.isNotEmpty()) {
            Text(
                feedback,
                fontSize = 13.sp,
                color = if (feedbackOk) colors.ok else colors.err,
                modifier = Modifier.fillMaxWidth().padding(bottom = 6.dp),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        when {
            state !is CameraController.State.Connected -> EmptyState(
                icon = "🖼️", title = "尚未连接相机", sub = "连接后可浏览相机存储卡中的照片", onGoConnect = onGoConnect,
            )
            loading && frames.isEmpty() -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("正在读取相机照片…", fontSize = 13.sp, color = colors.textSub)
            }
            frames.isEmpty() -> EmptyState(
                icon = "🖼️", title = "相机内暂无照片", sub = "点击重新加载", onGoConnect = onGoConnect, actionText = "重新加载",
            )
            else -> LazyVerticalGrid(
                columns = GridCells.Fixed(3),
                modifier = Modifier.fillMaxSize(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
                contentPadding = PaddingValues(top = 4.dp, bottom = 96.dp),
            ) {
                items(frames.take(150)) { obj ->
                    GridItem(obj, onDownload = { download(obj) })
                }
            }
        }
    }
}

@Composable
private fun EmptyState(icon: String, title: String, sub: String, onGoConnect: () -> Unit, actionText: String = "去连接") {
    val colors = LocalNikonColors.current
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(icon, fontSize = 48.sp)
            Spacer(Modifier.height(12.dp))
            Text(title, fontSize = 16.sp, fontWeight = FontWeight.Medium, color = colors.textMain)
            Spacer(Modifier.height(6.dp))
            Text(sub, fontSize = 12.sp, color = colors.textFaint)
            Spacer(Modifier.height(16.dp))
            CapsuleButton(
                actionText, onClick = onGoConnect,
                bg = colors.primaryBtn, fg = colors.primaryBtnText,
                modifier = Modifier.widthIn(max = 200.dp), height = 44.dp, radius = 22.dp, fontSize = 15,
            )
        }
    }
}

@Composable
private fun GridItem(obj: ObjectInfo, onDownload: () -> Unit) {
    val colors = LocalNikonColors.current
    Column(
        Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(colors.bgCard)
    ) {
        Box(
            Modifier.fillMaxWidth().aspectRatio(1f).background(colors.gridPlaceholder),
            contentAlignment = Alignment.Center,
        ) {
            Text("🖼️", fontSize = 28.sp, color = colors.textDisabled)
        }
        Column(Modifier.padding(8.dp)) {
            Text(obj.filename, fontSize = 11.sp, color = colors.textMain, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(formatSize(obj.size), fontSize = 10.sp, color = colors.textFaint)
            Spacer(Modifier.height(6.dp))
            Box(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp))
                    .background(colors.primarySoft)
                    .clickable { onDownload() }
                    .padding(vertical = 5.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text("下载", fontSize = 11.sp, color = colors.primary)
            }
        }
    }
}

private fun formatSize(bytes: Long): String = when {
    bytes >= 1024 * 1024 -> String.format(Locale.US, "%.1f MB", bytes / 1024.0 / 1024.0)
    bytes >= 1024 -> "${bytes / 1024} KB"
    else -> "$bytes B"
}
