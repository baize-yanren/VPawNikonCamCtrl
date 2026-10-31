package com.nikoncam.connector

import android.graphics.Color as AndroidColor
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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun SettingsScreen() {
    val colors = LocalNikonColors.current
    val theme = LocalAppTheme.current

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp)
    ) {
        Text(
            "设置",
            fontSize = 22.sp,
            fontWeight = FontWeight.Bold,
            color = colors.textMain,
            modifier = Modifier.fillMaxWidth().padding(top = 14.dp, bottom = 6.dp),
        )

        Spacer(Modifier.height(4.dp))

        // 应用信息卡
        Row(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(colors.bgCard)
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                Modifier.size(48.dp).clip(RoundedCornerShape(12.dp)).background(colors.primary),
                contentAlignment = Alignment.Center,
            ) {
                Text("📷", fontSize = 24.sp)
            }
            Spacer(Modifier.size(12.dp))
            Column {
                Text("Nikon Connector", fontSize = 17.sp, fontWeight = FontWeight.Bold, color = colors.textMain)
                Spacer(Modifier.height(4.dp))
                Box(
                    Modifier.clip(RoundedCornerShape(10.dp)).background(colors.primarySoft).padding(horizontal = 10.dp, vertical = 4.dp)
                ) {
                    Text("v0.1.0 · MIT", fontSize = 11.sp, color = colors.primary)
                }
            }
        }

        Spacer(Modifier.height(12.dp))

        // 外观分组
        Column(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(colors.bgCard)
                .padding(vertical = 14.dp)
        ) {
            Text("外观", fontSize = 12.sp, color = colors.textFaint, modifier = Modifier.padding(horizontal = 14.dp))
            Spacer(Modifier.height(10.dp))
            Row(Modifier.fillMaxWidth().padding(horizontal = 14.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf(
                    ThemeMode.SYSTEM to "跟随系统",
                    ThemeMode.LIGHT to "浅色",
                    ThemeMode.DARK to "深色",
                ).forEach { (mode, label) ->
                    val selected = theme.mode == mode
                    Box(
                        Modifier
                            .clip(RoundedCornerShape(16.dp))
                            .background(if (selected) colors.primaryBtn else colors.inputBg)
                            .border(1.dp, if (selected) colors.primaryBtn else colors.border, RoundedCornerShape(16.dp))
                            .clickable { theme.mode = mode }
                            .padding(horizontal = 14.dp, vertical = 8.dp)
                    ) {
                        Text(label, fontSize = 13.sp, color = if (selected) colors.primaryBtnText else colors.textSub)
                    }
                }
            }
        }

        Spacer(Modifier.height(12.dp))

        // 主题色
        Column(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(colors.bgCard)
                .padding(vertical = 14.dp)
        ) {
            Row(Modifier.padding(horizontal = 14.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("主题色", fontSize = 15.sp, fontWeight = FontWeight.Medium, color = colors.textMain)
                Text("灰黑系 · 与应用图标一致", fontSize = 11.sp, color = colors.textFaint, modifier = Modifier.weight(1f))
            }
            Spacer(Modifier.height(12.dp))
            Row(Modifier.padding(horizontal = 14.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                ThemeColors.PRESETS.forEach { c ->
                    val selected = theme.themeColor == c
                    Box(
                        Modifier
                            .size(30.dp)
                            .clip(CircleShape)
                            .background(Color(AndroidColor.parseColor(c)))
                            .clickable { theme.themeColor = c },
                        contentAlignment = Alignment.Center,
                    ) {
                        if (selected) Text("✓", fontSize = 13.sp, color = Color.White)
                    }
                }
            }
        }

        Spacer(Modifier.height(12.dp))

        // 关于
        Row(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(16.dp))
                .background(colors.bgCard)
                .clickable { }
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("关于", fontSize = 15.sp, color = colors.textMain, modifier = Modifier.weight(1f))
            Text("›", fontSize = 18.sp, color = colors.textDisabled)
        }

        Spacer(Modifier.height(96.dp))
    }
}
