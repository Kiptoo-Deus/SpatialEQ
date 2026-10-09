package com.savannahdsp.spatialeq.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.PowerSettingsNew
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.savannahdsp.spatialeq.AppStore
import kotlin.math.sin

const val GRANT_COMMAND = "adb shell pm grant com.savannahdsp.spatialeq android.permission.DUMP"

@Composable
fun HomeScreen(spectrum: LiveSpectrum, onStart: () -> Unit, onStop: () -> Unit, modifier: Modifier = Modifier) {
    val settings by AppStore.settings.collectAsState()
    val enabled by AppStore.enabled.collectAsState()
    val status by AppStore.status.collectAsState()
    val output by AppStore.outputName.collectAsState()
    val presetId by AppStore.presetId.collectAsState()

    Column(modifier.fillMaxSize().statusBarsPadding()) {
        // Header: bypass toggle, title, output device, preset name.
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier.size(38.dp).clip(CircleShape)
                    .background(if (enabled) Palette.accent else Color.White.copy(alpha = 0.12f))
                    .clickable { AppStore.enabled.value = !enabled },
                contentAlignment = Alignment.Center,
            ) { Icon(Icons.Filled.PowerSettingsNew, "Bypass", tint = if (enabled) Color.Black else Color.White) }
            Column(Modifier.padding(start = 12.dp).weight(1f)) {
                Text("SpatialEQ", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                Text(output, color = Palette.dim, fontSize = 12.sp)
            }
            Text(AppStore.allPresets.firstOrNull { it.id == presetId }?.name ?: "Custom",
                Modifier.background(Color.White.copy(alpha = 0.08f), RoundedCornerShape(50)).padding(horizontal = 12.dp, vertical = 6.dp),
                fontSize = 13.sp)
        }

        Box(Modifier.weight(1f).fillMaxWidth().padding(horizontal = 12.dp).clip(RoundedCornerShape(16.dp))) {
            Scene3D(settings, spectrum, enabled, Modifier.fillMaxSize())
            Text(
                when {
                    settings.mode.name == "Speakers" -> "Speakers · drag a speaker to set the span"
                    settings.spatialEnabled -> "${settings.upmix.label} · drag sources around you"
                    else -> "Turn on Virtual surround in Effects"
                },
                Modifier.align(Alignment.BottomStart).padding(10.dp)
                    .background(Color.Black.copy(alpha = 0.4f), RoundedCornerShape(50)).padding(horizontal = 10.dp, vertical = 4.dp),
                color = Palette.dim, fontSize = 11.sp,
            )
        }

        MiniSpectrum(spectrum, Modifier.fillMaxWidth().height(56.dp).padding(horizontal = 12.dp, vertical = 6.dp))

        StatusCard(status, onStart, onStop)
    }
}

@Composable
private fun MiniSpectrum(spectrum: LiveSpectrum, modifier: Modifier) {
    Canvas(modifier) {
        val bands = spectrum.current
        val t = spectrum.time
        val w = size.width / bands.size
        bands.forEachIndexed { i, v ->
            val wobble = 0.04f * (0.5f + 0.5f * sin(t * 2.6f + i * 0.35f))
            val h = (size.height * (v + wobble).coerceIn(0f, 1f)).coerceAtLeast(3f)
            drawRoundRect(
                if (i < bands.size / 2) Palette.accent.copy(alpha = 0.85f) else Palette.accent2.copy(alpha = 0.8f),
                topLeft = Offset(i * w + 1, size.height - h), size = Size(w - 2, h), cornerRadius = CornerRadius(3f),
            )
        }
    }
}

@Composable
private fun StatusCard(status: com.savannahdsp.spatialeq.ProcessingStatus, onStart: () -> Unit, onStop: () -> Unit) {
    val clipboard = LocalClipboardManager.current
    Column(
        Modifier.fillMaxWidth().padding(12.dp).background(Palette.panel, RoundedCornerShape(16.dp)).padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(if (status.running) "Processing all apps" else "System-wide processing is off", fontWeight = FontWeight.SemiBold)
                Text(status.message, color = Palette.dim, fontSize = 12.sp)
            }
            if (status.running) OutlinedButton(onClick = onStop) { Text("Stop") }
            else Button(onClick = onStart, colors = ButtonDefaults.buttonColors(containerColor = Palette.accent)) { Text("Start") }
        }
        if (status.running && status.processedApps.isNotEmpty())
            Text("Processing: " + status.processedApps.joinToString { it.label }, fontSize = 12.sp, color = Palette.accent)
        if (status.running && status.protectedApps.isNotEmpty())
            Text("Can't process (app blocks capture): " + status.protectedApps.joinToString { it.label }, fontSize = 12.sp, color = Palette.accent2)
        if (!status.sessionAccess) {
            Text("One-time setup for full system-wide processing: connect your phone to a computer with USB debugging on and run:",
                fontSize = 12.sp, color = Palette.dim)
            Row(
                Modifier.fillMaxWidth().background(Color.Black.copy(alpha = 0.35f), RoundedCornerShape(8.dp)).padding(start = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(GRANT_COMMAND, Modifier.weight(1f), fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                IconButton(onClick = { clipboard.setText(AnnotatedString(GRANT_COMMAND)) }) {
                    Icon(Icons.Filled.ContentCopy, "Copy command", Modifier.width(18.dp))
                }
            }
            Spacer(Modifier.height(2.dp))
        }
    }
}
