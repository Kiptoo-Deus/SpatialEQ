package com.savannahdsp.spatialeq.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.sp
import com.savannahdsp.spatialeq.AppStore

object Palette {
    val background = Color(0xFF090A0F)
    val panel = Color(0xFF12141C)
    val accent = Color(0xFF5CD9F2)
    val accent2 = Color(0xFFFA9E45)
    val dim = Color(0x8CFFFFFF)
    val channels = listOf(
        Color(0xFF4DB3FF), Color(0xFFFF6673), Color(0xFFF2F2F2), Color(0xFF8C73FF),
        Color(0xFFFF8CD9), Color(0xFF59F2A6), Color(0xFFFFCC4D), Color(0xFFB3B3B3),
    )
}

@Composable
fun SpatialEqTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Palette.accent, onPrimary = Color.Black, secondary = Palette.accent2,
            background = Palette.background, surface = Palette.panel, surfaceContainer = Palette.panel,
        ),
        content = content,
    )
}

fun formatHz(f: Float) = when {
    f >= 10000 -> "%.0fk".format(f / 1000)
    f >= 1000 -> "%.1fk".format(f / 1000)
    else -> "%.0f".format(f)
}

@Composable
fun ParamSlider(
    title: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    format: (Float) -> String,
    onChange: (Float) -> Unit,
) {
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth()) {
            Text(title, Modifier.weight(1f), fontSize = 14.sp)
            Text(format(value), color = Palette.dim, fontSize = 13.sp, fontFamily = FontFamily.Monospace)
        }
        Slider(value = value, onValueChange = onChange, valueRange = range,
            colors = SliderDefaults.colors(thumbColor = Palette.accent, activeTrackColor = Palette.accent))
    }
}

/** Live spectrum shared by the 3D scene and EQ graph; refreshed every frame while visible. */
class LiveSpectrum(val bands: Int = 48, val historyLength: Int = 28) {
    var current by mutableStateOf(FloatArray(bands))
        private set
    var history by mutableStateOf(List(historyLength) { FloatArray(bands) })
        private set
    var level by mutableFloatStateOf(0f)
        private set
    var time by mutableFloatStateOf(0f)
        private set

    suspend fun run() {
        var last = 0L
        while (true) {
            withFrameNanos { now ->
                time = now / 1e9f
                if (now - last > 30_000_000L) { // ~33 fps of analysis is plenty
                    last = now
                    val b = FloatArray(bands)
                    level = AppStore.engine.spectrum(b)
                    current = b
                    history = listOf(b) + history.dropLast(1)
                }
            }
        }
    }
}

@Composable
fun rememberLiveSpectrum(): LiveSpectrum {
    val s = remember { LiveSpectrum() }
    LaunchedEffect(s) { s.run() }
    return s
}
