package com.nikoncam.connector

import android.graphics.Color as AndroidColor
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import kotlin.math.roundToInt

/**
 * 主题系统（对齐 VPawCamCtrl 的视觉范式）：
 * - 单一「主题色」驱动全局，卡片/chip/页面底色均由色板从主题色派生；
 * - 主题色默认取 logo 冷调石墨蓝黑 #2E3841，可在设置页切换 3 款灰黑预设；
 * - 卡片 borderRadius 16、chip 10、悬浮胶囊 32，卡片无投影、投影只给 Dock。
 */

enum class ThemeMode { SYSTEM, LIGHT, DARK }

object ThemeColors {
    const val DEFAULT = "#2E3841"
    val PRESETS = listOf("#2E3841", "#1F2124", "#5B6068")
}

/** 完整界面色板（由主题色 + 深浅色派生）。 */
data class NikonColors(
    val themeColor: Color,
    val primary: Color,
    val primaryBtn: Color,
    val primaryBtnText: Color,
    val primarySoft: Color,
    val primaryAlpha: Color,
    val primaryFaint: Color,
    val bgPage: Color,
    val bgCard: Color,
    val selectedBg: Color,
    val selectedText: Color,
    val inputBg: Color,
    val textMain: Color,
    val textSub: Color,
    val textFaint: Color,
    val textDisabled: Color,
    val border: Color,
    val btnDisabled: Color,
    val btnDisabledText: Color,
    val gridPlaceholder: Color,
    val ok: Color,
    val err: Color,
    val errBtn: Color,
    val warn: Color,
    val warnBtn: Color,
    val record: Color,
    val recordActive: Color,
    val dockBg: Color,
    val dockDivider: Color,
)

private object C {
    fun rgb(hex: String): IntArray {
        val c = AndroidColor.parseColor(hex)
        return intArrayOf(AndroidColor.red(c), AndroidColor.green(c), AndroidColor.blue(c))
    }

    fun solid(hex: String): Color {
        val c = AndroidColor.parseColor(hex)
        return Color(AndroidColor.red(c), AndroidColor.green(c), AndroidColor.blue(c))
    }

    fun blend(hex: String, base: IntArray, ratio: Float): Color {
        val c = rgb(hex)
        return Color(
            (base[0] + (c[0] - base[0]) * ratio).roundToInt(),
            (base[1] + (c[1] - base[1]) * ratio).roundToInt(),
            (base[2] + (c[2] - base[2]) * ratio).roundToInt(),
        )
    }

    fun alpha(hex: String, a: Float): Color {
        val c = rgb(hex)
        return Color(c[0], c[1], c[2], (a * 255).roundToInt())
    }

    fun page(hex: String, dark: Boolean): Color =
        if (dark) blend(hex, intArrayOf(22, 19, 16), 0.16f)
        else blend(hex, intArrayOf(255, 255, 255), 0.09f)
}

fun deriveColors(themeColor: String, dark: Boolean): NikonColors {
    val c = themeColor
    return if (dark) {
        NikonColors(
            themeColor = C.solid(c),
            primary = C.blend(c, intArrayOf(36, 31, 27), 0.62f),
            primaryBtn = C.blend(c, intArrayOf(90, 96, 104), 0.85f),
            primaryBtnText = Color.White,
            primarySoft = C.alpha(c, 0.16f),
            primaryAlpha = C.alpha(c, 0.35f),
            primaryFaint = C.alpha(c, 0.60f),
            bgPage = C.page(c, true),
            bgCard = Color(0xFF1E2124),
            selectedBg = Color(0xFF5B6C7A),
            selectedText = Color.White,
            inputBg = Color(0xFF24282C),
            textMain = Color(0xFFEDEDED),
            textSub = Color(0xFF9CA3A0),
            textFaint = Color(0xFF7E8581),
            textDisabled = Color(0xFF4A5055),
            border = Color(0xFF33383D),
            btnDisabled = Color(0xFF3A403C),
            btnDisabledText = Color(0xFF9CA3A0),
            gridPlaceholder = Color(0xFF262B28),
            ok = Color(0xFF7FC98B),
            err = Color(0xFFE57373),
            errBtn = Color(0xFFB03A3A),
            warn = Color(0xFFFFB74D),
            warnBtn = Color(0xFFB36A00),
            record = Color(0xFFC04343),
            recordActive = Color(0xFF7A1F1F),
            dockBg = Color(0xEB1A1C1E),
            dockDivider = Color(0x33FFFFFF),
        )
    } else {
        NikonColors(
            themeColor = C.solid(c),
            primary = C.solid(c),
            primaryBtn = C.solid(c),
            primaryBtnText = Color.White,
            primarySoft = C.alpha(c, 0.10f),
            primaryAlpha = C.alpha(c, 0.35f),
            primaryFaint = C.alpha(c, 0.60f),
            bgPage = C.page(c, false),
            bgCard = Color.White,
            selectedBg = Color(0xFFC9CFD4),
            selectedText = Color(0xFF1A1A1A),
            inputBg = Color.White,
            textMain = Color(0xFF1A1A1A),
            textSub = Color(0xFF666666),
            textFaint = Color(0xFF999999),
            textDisabled = Color(0xFFCCCCCC),
            border = Color(0xFFE6E8EA),
            btnDisabled = Color(0xFFB8BCBF),
            btnDisabledText = Color(0xFF5F5E5A),
            gridPlaceholder = Color(0xFFF0F1F2),
            ok = Color(0xFF2E7D32),
            err = Color(0xFFC62828),
            errBtn = Color(0xFFC62828),
            warn = Color(0xFFB36A00),
            warnBtn = Color(0xFFB36A00),
            record = Color(0xFFC62828),
            recordActive = Color(0xFF8E1414),
            dockBg = Color(0xEBFFFFFF),
            dockDivider = Color(0x33DDDDDD),
        )
    }
}

/** 主题状态：模式 + 主题色，设置页可直接读写。 */
class AppThemeState {
    var mode by mutableStateOf(ThemeMode.SYSTEM)
    var themeColor by mutableStateOf(ThemeColors.DEFAULT)
}

val LocalNikonColors = staticCompositionLocalOf { deriveColors(ThemeColors.DEFAULT, false) }
val LocalAppTheme = staticCompositionLocalOf { AppThemeState() }

@Composable
fun NikonTheme(theme: AppThemeState, content: @Composable () -> Unit) {
    val dark = when (theme.mode) {
        ThemeMode.SYSTEM -> isSystemInDarkTheme()
        ThemeMode.LIGHT -> false
        ThemeMode.DARK -> true
    }
    val colors = deriveColors(theme.themeColor, dark)
    CompositionLocalProvider(
        LocalNikonColors provides colors,
        LocalAppTheme provides theme,
        content = content,
    )
}
