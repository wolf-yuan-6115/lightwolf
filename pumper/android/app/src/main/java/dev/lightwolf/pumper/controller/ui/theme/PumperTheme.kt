package dev.lightwolf.pumper.controller.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.MaterialExpressiveTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import dev.lightwolf.pumper.controller.settings.ThemeMode

private val LightWolfLightColors = lightColorScheme(
    primary = Color(0xff006a63),
    onPrimary = Color.White,
    primaryContainer = Color(0xff9cf2e8),
    onPrimaryContainer = Color(0xff00201d),
    secondary = Color(0xff8b4a3d),
    onSecondary = Color.White,
    secondaryContainer = Color(0xffffdad2),
    onSecondaryContainer = Color(0xff3a0b04),
    tertiary = Color(0xff735c00),
    onTertiary = Color.White,
    tertiaryContainer = Color(0xffffe16c),
    onTertiaryContainer = Color(0xff231b00),
    background = Color(0xfffaf9f7),
    surface = Color(0xfffaf9f7),
    surfaceVariant = Color(0xffdae5e1),
)

private val LightWolfDarkColors = darkColorScheme(
    primary = Color(0xff80d5cc),
    onPrimary = Color(0xff003732),
    primaryContainer = Color(0xff005049),
    onPrimaryContainer = Color(0xff9cf2e8),
    secondary = Color(0xffffb4a6),
    onSecondary = Color(0xff542017),
    secondaryContainer = Color(0xff70362b),
    onSecondaryContainer = Color(0xffffdad2),
    tertiary = Color(0xffffdf5d),
    onTertiary = Color(0xff3c2f00),
    tertiaryContainer = Color(0xff574500),
    onTertiaryContainer = Color(0xffffe16c),
    background = Color(0xff111412),
    surface = Color(0xff111412),
    surfaceVariant = Color(0xff3f4946),
)

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun PumperTheme(themeMode: ThemeMode, content: @Composable () -> Unit) {
    val dark = when (themeMode) {
        ThemeMode.System -> isSystemInDarkTheme()
        ThemeMode.Light -> false
        ThemeMode.Dark -> true
    }
    val context = LocalContext.current
    val colors = runCatching {
        if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
    }.getOrElse {
        if (dark) LightWolfDarkColors else LightWolfLightColors
    }
    MaterialExpressiveTheme(
        colorScheme = colors,
        motionScheme = MotionScheme.expressive(),
        content = content,
    )
}
