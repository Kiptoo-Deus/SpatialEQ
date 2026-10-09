package com.savannahdsp.spatialeq.model

import kotlinx.serialization.Serializable
import java.util.UUID

@Serializable
enum class BandType(val label: String, val hasGain: Boolean) {
    Peak("Peak", true), LowShelf("Low Shelf", true), HighShelf("High Shelf", true),
    LowPass("Low Pass", false), HighPass("High Pass", false)
}

@Serializable
data class EqBand(
    val id: String = UUID.randomUUID().toString(),
    val enabled: Boolean = true,
    val type: BandType = BandType.Peak,
    val frequency: Float,
    val gainDb: Float = 0f,
    val q: Float = 1f,
)

@Serializable
enum class OutputMode(val label: String) { Headphones("Headphones"), Speakers("Speakers") }

@Serializable
enum class Upmix(val label: String, val channels: List<String>) {
    Stereo("Stereo", listOf("L", "R")),
    Surround51("Virtual 5.1", listOf("L", "R", "C", "Ls", "Rs")),
    Surround71("Virtual 7.1", listOf("L", "R", "C", "Ls", "Rs", "Lb", "Rb")),
}

@Serializable
data class VirtualSource(
    val azimuth: Float,
    val elevation: Float = 0f,
    val distance: Float = 1.5f,
    val gain: Float = 1f,
)

/** Same model as the Apple apps' SoundSettings; packed into the native parameter block. */
@Serializable
data class SoundSettings(
    val preampDb: Float = 0f,
    val eqEnabled: Boolean = true,
    val bands: List<EqBand> = tenBand(),
    val mode: OutputMode = OutputMode.Headphones,
    val spatialEnabled: Boolean = false,
    val upmix: Upmix = Upmix.Stereo,
    val sources: List<VirtualSource> = defaultSources(),
    val width: Float = 1f,
    val crosstalkEnabled: Boolean = false,
    val crosstalkStrength: Float = 0.6f,
    val speakerSpan: Float = 30f,
    val roomSize: Float = 0.35f,
    val reverbMix: Float = 0f,
    val bassBoostDb: Float = 0f,
    val dialogueBoost: Float = 0f,
    val levelerEnabled: Boolean = false,
    val levelerTargetDb: Float = -20f,
    val limiterEnabled: Boolean = true,
    val limiterCeilingDb: Float = -1f,
    val outputGainDb: Float = 0f,
) {
    /** Layout documented in jni_bridge.cpp. */
    fun pack(enabled: Boolean): FloatArray {
        val a = FloatArray(PACKED_SIZE)
        fun b(v: Boolean) = if (v) 1f else 0f
        a[0] = b(enabled); a[1] = preampDb; a[2] = b(eqEnabled)
        val n = minOf(bands.size, MAX_BANDS)
        a[3] = n.toFloat()
        for (i in 0 until n) {
            val o = 4 + i * 5
            val band = bands[i]
            a[o] = b(band.enabled); a[o + 1] = band.type.ordinal.toFloat(); a[o + 2] = band.frequency
            a[o + 3] = band.gainDb; a[o + 4] = band.q
        }
        a[84] = mode.ordinal.toFloat(); a[85] = b(spatialEnabled); a[86] = upmix.ordinal.toFloat()
        for (i in 0 until minOf(sources.size, MAX_SOURCES)) {
            val o = 87 + i * 4
            val s = sources[i]
            a[o] = s.azimuth; a[o + 1] = s.elevation; a[o + 2] = s.distance; a[o + 3] = s.gain
        }
        a[119] = width; a[120] = b(crosstalkEnabled); a[121] = crosstalkStrength; a[122] = speakerSpan
        a[123] = roomSize; a[124] = reverbMix; a[125] = bassBoostDb; a[126] = dialogueBoost
        a[127] = b(levelerEnabled); a[128] = levelerTargetDb; a[129] = b(limiterEnabled)
        a[130] = limiterCeilingDb; a[131] = outputGainDb
        return a
    }

    companion object {
        const val PACKED_SIZE = 132
        const val MAX_BANDS = 16
        const val MAX_SOURCES = 8
        val defaultAzimuths = listOf(-30f, 30f, 0f, -110f, 110f, -150f, 150f, 0f)

        fun defaultSources() = defaultAzimuths.map { VirtualSource(it) }

        fun tenBand(): List<EqBand> {
            val freqs = listOf(32f, 64f, 125f, 250f, 500f, 1000f, 2000f, 4000f, 8000f, 16000f)
            return freqs.mapIndexed { i, f ->
                EqBand(
                    type = when (i) { 0 -> BandType.LowShelf; freqs.lastIndex -> BandType.HighShelf; else -> BandType.Peak },
                    frequency = f,
                    q = if (i == 0 || i == freqs.lastIndex) 0.7f else 1f,
                )
            }
        }
    }
}

@Serializable
data class Preset(
    val id: String = UUID.randomUUID().toString(),
    val name: String,
    val settings: SoundSettings,
    val builtIn: Boolean = false,
)

@Serializable
data class DeviceProfile(val presetId: String?, val settings: SoundSettings)
