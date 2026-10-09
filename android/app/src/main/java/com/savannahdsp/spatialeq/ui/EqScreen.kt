package com.savannahdsp.spatialeq.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.FileOpen
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.savannahdsp.spatialeq.AppStore
import com.savannahdsp.spatialeq.audio.NativeEngine
import com.savannahdsp.spatialeq.model.BandType
import com.savannahdsp.spatialeq.model.SoundSettings
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.pow
import kotlin.math.sin

private const val MIN_F = 20f
private const val MAX_F = 20000f
private const val RANGE_DB = 18f

private fun fx(f: Float, w: Float) = (ln(f / MIN_F) / ln(MAX_F / MIN_F)) * w
private fun xf(x: Float, w: Float) = MIN_F * (MAX_F / MIN_F).pow((x / w).coerceIn(0f, 1f))
private fun dy(db: Float, h: Float) = h / 2 - db / RANGE_DB * (h / 2 - 14)
private fun ydb(y: Float, h: Float) = (h / 2 - y) / (h / 2 - 14) * RANGE_DB

@Composable
fun EqScreen(spectrum: LiveSpectrum, modifier: Modifier = Modifier) {
    val settings by AppStore.settings.collectAsState()
    val selectedId by AppStore.selectedBandId.collectAsState()
    var message by remember { mutableStateOf<String?>(null) }
    val context = LocalContext.current
    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        val text = context.contentResolver.openInputStream(uri)?.bufferedReader()?.readText() ?: return@rememberLauncherForActivityResult
        val name = uri.lastPathSegment?.substringAfterLast('/')?.substringBeforeLast('.') ?: "AutoEq"
        message = AppStore.importAutoEq(text, name)
    }

    Column(modifier.fillMaxSize().statusBarsPadding().verticalScroll(rememberScrollState()).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Equalizer", fontWeight = FontWeight.Bold, fontSize = 20.sp, modifier = Modifier.weight(1f))
            Switch(settings.eqEnabled, { v -> AppStore.update { it.copy(eqEnabled = v) } })
            IconButton(onClick = { AppStore.addBand() }, enabled = settings.bands.size < SoundSettings.MAX_BANDS) { Icon(Icons.Filled.Add, "Add band") }
            IconButton(onClick = { importer.launch(arrayOf("text/plain", "*/*")) }) { Icon(Icons.Filled.FileOpen, "Import AutoEq") }
            IconButton(onClick = {
                AppStore.update { it.copy(bands = SoundSettings.tenBand(), preampDb = 0f) }
                AppStore.selectedBandId.value = null
            }) { Icon(Icons.Filled.RestartAlt, "Reset") }
        }
        message?.let { Text(it, color = Palette.accent2, fontSize = 12.sp) }

        EqGraph(settings, selectedId, spectrum,
            Modifier.fillMaxWidth().height(260.dp).background(Color.Black.copy(alpha = 0.3f), RoundedCornerShape(12.dp)))

        Column(Modifier.background(Palette.panel, RoundedCornerShape(12.dp)).padding(12.dp)) {
            ParamSlider("Preamp", settings.preampDb, -24f..6f, { "%+.1f dB".format(it) }) { v -> AppStore.update { it.copy(preampDb = v) } }
        }

        val index = settings.bands.indexOfFirst { it.id == selectedId }
        if (index >= 0) BandInspector(settings, index)
        else Text("Drag a numbered handle to shape the sound; tap one to edit it precisely.", color = Palette.dim, fontSize = 13.sp)
    }
}

@Composable
private fun EqGraph(settings: SoundSettings, selectedId: String?, spectrum: LiveSpectrum, modifier: Modifier) {
    val response = remember(settings.bands, settings.preampDb, settings.eqEnabled) {
        val freqs = FloatArray(180) { MIN_F * (MAX_F / MIN_F).pow(it / 179f) }
        freqs to NativeEngine.eqResponse(settings.pack(true), 48000f, freqs)
    }
    var dragId by remember { mutableStateOf<String?>(null) }

    Canvas(modifier
        .pointerInput(Unit) {
            fun nearest(p: Offset): String? = AppStore.settings.value.bands.minByOrNull { b ->
                (Offset(fx(b.frequency, size.width.toFloat()), dy(if (b.type.hasGain) b.gainDb else 0f, size.height.toFloat())) - p).getDistance()
            }?.takeIf { b ->
                (Offset(fx(b.frequency, size.width.toFloat()), dy(if (b.type.hasGain) b.gainDb else 0f, size.height.toFloat())) - p).getDistance() < 64f
            }?.id
            detectDragGestures(onDragStart = { p -> dragId = nearest(p); AppStore.selectedBandId.value = dragId ?: AppStore.selectedBandId.value },
                onDragEnd = { dragId = null }) { change, _ ->
                val id = dragId ?: return@detectDragGestures
                val w = size.width.toFloat(); val h = size.height.toFloat()
                AppStore.update { s ->
                    s.copy(bands = s.bands.map { b ->
                        if (b.id != id) b else b.copy(
                            frequency = (xf(change.position.x, w) * 10).toInt() / 10f,
                            gainDb = if (b.type.hasGain) ((ydb(change.position.y, h).coerceIn(-RANGE_DB, RANGE_DB)) * 10).toInt() / 10f else b.gainDb,
                        )
                    })
                }
            }
        }
        .pointerInput(Unit) {
            detectTapGestures { p ->
                AppStore.selectedBandId.value = AppStore.settings.value.bands.minByOrNull { b ->
                    (Offset(fx(b.frequency, size.width.toFloat()), dy(if (b.type.hasGain) b.gainDb else 0f, size.height.toFloat())) - p).getDistance()
                }?.id
            }
        }
    ) {
        val w = size.width; val h = size.height
        val paint = android.graphics.Paint().apply { color = 0x99FFFFFF.toInt(); textSize = 26f; isAntiAlias = true }
        // Grid
        for (f in listOf(50f, 100f, 200f, 500f, 1000f, 2000f, 5000f, 10000f)) {
            val x = fx(f, w)
            drawLine(Color.White.copy(alpha = 0.07f), Offset(x, 0f), Offset(x, h))
            drawContext.canvas.nativeCanvas.drawText(formatHz(f), x + 4, h - 8, paint)
        }
        for (d in listOf(-12f, -6f, 0f, 6f, 12f)) drawLine(Color.White.copy(alpha = 0.07f), Offset(0f, dy(d, h)), Offset(w, dy(d, h)))

        // Live spectrum with a gentle ripple so it is always moving.
        val bands = spectrum.current
        val t = spectrum.time
        val spec = Path().apply {
            moveTo(0f, h)
            bands.forEachIndexed { i, v ->
                val f = MIN_F * (MAX_F / MIN_F).pow((i + 0.5f) / bands.size)
                val ripple = 0.05f * (0.5f + 0.5f * sin(t * 2.4f + i * 0.32f))
                lineTo(fx(f, w), h * (1 - (0.03f + v + ripple * (0.4f + v)).coerceIn(0f, 1f) * 0.9f))
            }
            lineTo(w, h); close()
        }
        drawPath(spec, Brush.verticalGradient(listOf(Palette.accent.copy(alpha = 0.4f), Palette.accent.copy(alpha = 0.03f))))

        // Combined EQ curve
        val (freqs, db) = response
        val curve = Path()
        freqs.forEachIndexed { i, f -> val p = Offset(fx(f, w), dy(db[i], h)); if (i == 0) curve.moveTo(p.x, p.y) else curve.lineTo(p.x, p.y) }
        val curveColor = if (settings.eqEnabled) Palette.accent2 else Color.Gray
        drawPath(curve, curveColor.copy(alpha = 0.25f), style = Stroke(12f))
        drawPath(curve, curveColor, style = Stroke(4f))

        // Handles
        val label = android.graphics.Paint().apply {
            color = android.graphics.Color.BLACK; textSize = 24f; isFakeBoldText = true
            textAlign = android.graphics.Paint.Align.CENTER; isAntiAlias = true
        }
        settings.bands.forEachIndexed { i, b ->
            val c = Offset(fx(b.frequency, w), dy(if (b.type.hasGain) b.gainDb else 0f, h))
            if (b.id == selectedId) drawCircle(Color.White, 30f, c, style = Stroke(4f))
            drawCircle(if (b.enabled) Palette.accent2 else Color.Gray, 22f, c)
            drawContext.canvas.nativeCanvas.drawText("${i + 1}", c.x, c.y + 8, label)
        }
    }
}

@Composable
private fun BandInspector(settings: SoundSettings, index: Int) {
    val band = settings.bands[index]
    fun edit(transform: (com.savannahdsp.spatialeq.model.EqBand) -> com.savannahdsp.spatialeq.model.EqBand) =
        AppStore.update { s -> s.copy(bands = s.bands.map { if (it.id == band.id) transform(it) else it }) }

    Column(Modifier.background(Palette.panel, RoundedCornerShape(12.dp)).padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Band ${index + 1}", fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            Switch(band.enabled, { v -> edit { it.copy(enabled = v) } })
            IconButton(onClick = { AppStore.removeBand(band.id) }) { Icon(Icons.Filled.Delete, "Remove band") }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            BandType.entries.forEach { t ->
                FilterChip(selected = band.type == t, onClick = { edit { it.copy(type = t) } }, label = { Text(t.label, fontSize = 11.sp) })
            }
        }
        ParamSlider("Frequency", log10(band.frequency), log10(20f)..log10(20000f), { formatHz(10f.pow(it)) + " Hz" }) { v ->
            edit { it.copy(frequency = (10f.pow(v) * 10).toInt() / 10f) }
        }
        if (band.type.hasGain) ParamSlider("Gain", band.gainDb, -18f..18f, { "%+.1f dB".format(it) }) { v -> edit { it.copy(gainDb = v) } }
        ParamSlider("Q", band.q, 0.1f..10f, { "%.2f".format(it) }) { v -> edit { it.copy(q = v) } }
    }
}
