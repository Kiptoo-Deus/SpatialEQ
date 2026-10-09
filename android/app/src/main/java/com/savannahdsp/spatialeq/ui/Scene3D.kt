package com.savannahdsp.spatialeq.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.input.pointer.pointerInput
import com.savannahdsp.spatialeq.AppStore
import com.savannahdsp.spatialeq.audio.NativeEngine
import com.savannahdsp.spatialeq.model.OutputMode
import com.savannahdsp.spatialeq.model.SoundSettings
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.pow
import kotlin.math.sin
import kotlin.math.sqrt

private data class V3(val x: Float, val y: Float, val z: Float) {
    operator fun minus(o: V3) = V3(x - o.x, y - o.y, z - o.z)
    operator fun plus(o: V3) = V3(x + o.x, y + o.y, z + o.z)
    operator fun times(k: Float) = V3(x * k, y * k, z * k)
    fun dot(o: V3) = x * o.x + y * o.y + z * o.z
    fun cross(o: V3) = V3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x)
    fun norm(): V3 { val l = sqrt(dot(this)); return V3(x / l, y / l, z / l) }
}

/** Orbiting perspective camera looking at the listener (front of the listener is -z). */
private class Camera(var yaw: Float = 0f, var pitch: Float = 0.5f, var distance: Float = 8f) {
    lateinit var eye: V3; lateinit var fwd: V3; lateinit var right: V3; lateinit var up: V3
    var cx = 0f; var cy = 0f; var f = 1f

    fun setup(w: Float, h: Float) {
        eye = V3(distance * cos(pitch) * sin(yaw), distance * sin(pitch), distance * cos(pitch) * cos(yaw))
        fwd = (V3(0f, 0f, 0f) - eye).norm()
        right = fwd.cross(V3(0f, 1f, 0f)).norm()
        up = right.cross(fwd)
        cx = w / 2; cy = h * 0.47f; f = h * 1.15f
    }

    /** Screen position and depth, or null when behind the camera. */
    fun project(p: V3): Pair<Offset, Float>? {
        val v = p - eye
        val z = v.dot(fwd)
        if (z < 0.1f) return null
        return Offset(cx + f * v.dot(right) / z, cy - f * v.dot(up) / z) to z
    }

    fun ground(screen: Offset, height: Float): V3? {
        val dir = (fwd + right * ((screen.x - cx) / f) + up * ((cy - screen.y) / f)).norm()
        if (abs(dir.y) < 1e-4f) return null
        val t = (height - eye.y) / dir.y
        return if (t > 0) eye + dir * t else null
    }
}

private fun sourcePos(az: Float, el: Float, d: Float): V3 {
    val a = az * PI.toFloat() / 180f; val e = el * PI.toFloat() / 180f
    return V3(d * sin(a) * cos(e), d * sin(e), -d * cos(a) * cos(e))
}

/**
 * Interactive 3D view drawn on a Canvas: head, draggable sources (or speakers), spectrum terrain
 * and the EQ curve as a ribbon. Drag a source to move it, drag elsewhere to orbit, pinch to zoom,
 * double-tap to reset the view.
 */
@Composable
fun Scene3D(settings: SoundSettings, spectrum: LiveSpectrum, enabled: Boolean, modifier: Modifier = Modifier) {
    val cam = remember { Camera() }
    var redraw by remember { mutableFloatStateOf(0f) }
    var dragging by remember { mutableStateOf<Int?>(null) }
    val spatial = settings.mode == OutputMode.Headphones && settings.spatialEnabled
    val channelNames = if (spatial) settings.upmix.channels else emptyList()

    val ribbon = remember(settings.bands, settings.preampDb, settings.eqEnabled) {
        val freqs = FloatArray(120) { 20f * 1000f.pow(it / 119f) }
        NativeEngine.eqResponse(settings.pack(true), 48000f, freqs)
    }

    Canvas(
        modifier
            .pointerInput(spatial, settings.mode) {
                detectDragGestures(
                    onDragStart = { p ->
                        dragging = null
                        if (spatial) {
                            channelNames.indices.minByOrNull { i ->
                                val s = AppStore.settings.value.sources[i]
                                cam.project(sourcePos(s.azimuth, s.elevation, s.distance))?.let { (o, _) -> (o - p).getDistance() } ?: 1e9f
                            }?.let { i ->
                                val s = AppStore.settings.value.sources[i]
                                val d = cam.project(sourcePos(s.azimuth, s.elevation, s.distance))?.let { (o, _) -> (o - p).getDistance() } ?: 1e9f
                                if (d < 70f) dragging = i
                            }
                        } else if (settings.mode == OutputMode.Speakers) {
                            dragging = -1 // speakers: drag sets the span
                        }
                    },
                    onDragEnd = { dragging = null },
                    onDragCancel = { dragging = null },
                ) { change, delta ->
                    val i = dragging
                    when {
                        i != null && i >= 0 -> {
                            val s = AppStore.settings.value.sources[i]
                            cam.ground(change.position, sourcePos(s.azimuth, s.elevation, s.distance).y)?.let { g ->
                                val az = atan2(g.x, -g.z) * 180f / PI.toFloat()
                                val dist = (hypot(g.x, g.z) / maxOf(cos(s.elevation * PI.toFloat() / 180f), 0.2f)).coerceIn(0.5f, 4f)
                                AppStore.update { st ->
                                    st.copy(sources = st.sources.mapIndexed { j, v -> if (j == i) v.copy(azimuth = az, distance = dist) else v })
                                }
                            }
                        }
                        i == -1 -> cam.ground(change.position, 0f)?.let { g ->
                            val az = abs(atan2(g.x, -g.z) * 180f / PI.toFloat())
                            AppStore.update { it.copy(speakerSpan = (az * 2).coerceIn(10f, 120f)) }
                        }
                        else -> {
                            cam.yaw -= delta.x * 0.008f
                            cam.pitch = (cam.pitch + delta.y * 0.008f).coerceIn(-0.2f, 1.45f)
                            redraw++
                        }
                    }
                }
            }
            .pointerInput(Unit) {
                detectTransformGestures { _, _, zoom, _ ->
                    if (zoom != 1f) { cam.distance = (cam.distance / zoom).coerceIn(3.5f, 16f); redraw++ }
                }
            }
            .pointerInput(Unit) {
                detectTapGestures(onDoubleTap = { cam.yaw = 0f; cam.pitch = 0.5f; cam.distance = 8f; redraw++ })
            }
    ) {
        redraw.let { }
        cam.setup(size.width, size.height)
        drawRect(Palette.background)
        drawTerrain(cam, spectrum, enabled)
        drawRibbon(cam, ribbon)
        drawRings(cam)

        // Depth-sort head and sources so nearer things draw on top.
        data class Item(val depth: Float, val draw: DrawScope.() -> Unit)
        val items = mutableListOf<Item>()
        cam.project(V3(0f, 0f, 0f))?.let { (o, z) -> items += Item(z) { drawHead(cam, o, z) } }
        val pulse = 1f + spectrum.level * 0.6f
        if (spatial) {
            channelNames.forEachIndexed { i, name ->
                val s = settings.sources[i]
                val p = sourcePos(s.azimuth, s.elevation, s.distance)
                val head = cam.project(V3(0f, 0f, 0f))
                cam.project(p)?.let { (o, z) ->
                    items += Item(z) {
                        head?.let { (h, _) -> drawLine(Palette.channels[i].copy(alpha = 0.35f), h, o, strokeWidth = 2f) }
                        val r = cam.f * 0.13f / z
                        val glow = if (dragging == i) pulse * 1.5f else pulse
                        drawCircle(Brush.radialGradient(listOf(Palette.channels[i].copy(alpha = 0.45f), Color.Transparent), o, r * 2.6f * glow), r * 2.6f * glow, o)
                        drawCircle(Palette.channels[i], r, o)
                        drawCircle(Color.White.copy(alpha = 0.7f), r * 0.45f, o)
                        drawLabel(name, o + Offset(0f, -r * 1.9f))
                    }
                }
            }
        } else {
            val half = if (settings.mode == OutputMode.Headphones) 30f else settings.speakerSpan / 2
            listOf(-half, half).forEachIndexed { i, az ->
                cam.project(sourcePos(az, 0f, 2.2f))?.let { (o, z) ->
                    items += Item(z) {
                        val w = cam.f * 0.4f / z; val h = cam.f * 0.62f / z
                        drawRoundRect(Color(0xFF2A2D36), topLeft = o - Offset(w / 2, h / 2),
                            size = androidx.compose.ui.geometry.Size(w, h), cornerRadius = androidx.compose.ui.geometry.CornerRadius(w * 0.1f))
                        drawCircle(Palette.channels[i], w * 0.3f * pulse, o + Offset(0f, h * 0.12f))
                        drawCircle(Palette.channels[i].copy(alpha = 0.6f), w * 0.12f, o - Offset(0f, h * 0.27f))
                    }
                }
            }
        }
        items.sortedByDescending { it.depth }.forEach { it.draw(this) }
    }
}

private const val FLOOR = -1.25f

private fun DrawScope.drawTerrain(cam: Camera, spectrum: LiveSpectrum, enabled: Boolean) {
    val rows = spectrum.history
    val cols = spectrum.bands
    val width = 7f; val depth = 6.5f; val front = 2.2f
    // Back to front so nearer rows overlap farther ones.
    for (r in rows.indices.reversed()) {
        val z = front - r.toFloat() / (rows.size - 1) * depth
        val row = rows[r]
        val path = Path()
        val base = Path()
        var started = false
        for (c in 0 until cols) {
            val x = (c.toFloat() / (cols - 1) - 0.5f) * width
            val v = row.getOrElse(c) { 0f }
            val p = cam.project(V3(x, FLOOR + v * 1.3f, z))?.first ?: continue
            if (!started) {
                path.moveTo(p.x, p.y)
                cam.project(V3(x, FLOOR, z))?.first?.let { base.moveTo(it.x, it.y) }
                started = true
            } else path.lineTo(p.x, p.y)
        }
        if (!started) continue
        val fade = (1f - r.toFloat() / rows.size) * if (enabled) 1f else 0.4f
        // Filled body down to the floor.
        val fill = Path().apply {
            addPath(path)
            cam.project(V3(width / 2, FLOOR, z))?.first?.let { lineTo(it.x, it.y) }
            cam.project(V3(-width / 2, FLOOR, z))?.first?.let { lineTo(it.x, it.y) }
            close()
        }
        val avg = row.average().toFloat()
        drawPath(fill, heat(avg).copy(alpha = 0.10f * fade))
        drawPath(path, heat(0.35f + avg).copy(alpha = 0.75f * fade), style = Stroke(width = 2f))
    }
}

private fun heat(v: Float): Color {
    val stops = listOf(0f to Color(0xFF081030), 0.35f to Color(0xFF1A59BF), 0.6f to Color(0xFF4DD9F2),
        0.8f to Color(0xFFFA9940), 1f to Color(0xFFFFF2D9))
    val t = v.coerceIn(0f, 1f)
    for (i in 1 until stops.size) if (t <= stops[i].first) {
        val (t0, c0) = stops[i - 1]; val (t1, c1) = stops[i]
        val k = (t - t0) / (t1 - t0)
        return Color(c0.red + (c1.red - c0.red) * k, c0.green + (c1.green - c0.green) * k, c0.blue + (c1.blue - c0.blue) * k)
    }
    return stops.last().second
}

private fun DrawScope.drawRibbon(cam: Camera, db: FloatArray) {
    val path = Path()
    db.forEachIndexed { i, d ->
        val x = (i.toFloat() / (db.size - 1) - 0.5f) * 7f
        val y = FLOOR + 0.9f + d.coerceIn(-18f, 18f) / 18f * 0.8f
        cam.project(V3(x, y, 2.35f))?.first?.let { if (i == 0) path.moveTo(it.x, it.y) else path.lineTo(it.x, it.y) }
    }
    drawPath(path, Palette.accent2.copy(alpha = 0.3f), style = Stroke(width = 10f))
    drawPath(path, Color(0xFFFFC27A), style = Stroke(width = 3.5f))
}

private fun DrawScope.drawRings(cam: Camera) {
    for (r in 1..3) {
        val path = Path()
        for (k in 0..72) {
            val a = k / 72f * 2 * PI.toFloat()
            cam.project(V3(r * sin(a), -0.55f, r * cos(a)))?.first?.let { if (k == 0) path.moveTo(it.x, it.y) else path.lineTo(it.x, it.y) }
        }
        drawPath(path, Color.White.copy(alpha = 0.1f), style = Stroke(width = 1.5f))
    }
}

private fun DrawScope.drawHead(cam: Camera, o: Offset, z: Float) {
    val r = cam.f * 0.36f / z
    drawCircle(Brush.radialGradient(listOf(Color(0xFF8A8F9C), Color(0xFF2A2D35), Color(0xFF121418)),
        o - Offset(r * 0.35f, r * 0.4f), r * 1.5f), r, o)
    drawCircle(Palette.accent.copy(alpha = 0.35f), r, o, style = Stroke(width = 2f))
    // Nose marks where the listener is facing (-z).
    cam.project(V3(0f, -0.02f, -0.4f))?.let { (n, nz) -> if (nz < z) drawCircle(Color(0xFF6B707C), cam.f * 0.06f / nz, n) }
    cam.project(V3(0f, -0.55f, -0.7f))?.first?.let { drawCircle(Palette.accent, 5f, it) }
}

private fun DrawScope.drawLabel(text: String, at: Offset) {
    val paint = android.graphics.Paint().apply {
        color = android.graphics.Color.WHITE
        textSize = 34f
        textAlign = android.graphics.Paint.Align.CENTER
        isAntiAlias = true
        isFakeBoldText = true
    }
    drawContext.canvas.nativeCanvas.drawText(text, at.x, at.y, paint)
}
