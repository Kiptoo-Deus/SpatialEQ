package com.savannahdsp.spatialeq.model

/** The same 12 built-in presets as the Apple apps (stable IDs so device profiles keep pointing at them). */
object BuiltInPresets {
    private fun id(n: Int) = "00000000-0000-0000-0000-%012d".format(n)

    private fun SoundSettings.gains(vararg pairs: Pair<Int, Float>) =
        copy(bands = bands.mapIndexed { i, b -> pairs.firstOrNull { it.first == i }?.let { b.copy(gainDb = it.second) } ?: b })

    private fun SoundSettings.sourceAt(i: Int, az: Float? = null, dist: Float? = null) =
        copy(sources = sources.mapIndexed { j, s -> if (j == i) s.copy(azimuth = az ?: s.azimuth, distance = dist ?: s.distance) else s })

    val all: List<Preset> by lazy {
        val base = SoundSettings()
        listOf(
            Preset(id(1), "Flat", base, true),
            Preset(id(2), "Bass Boost", base.gains(0 to 6f, 1 to 4f, 2 to 2f).copy(bassBoostDb = 4f, preampDb = -4f), true),
            Preset(id(3), "Treble Lift", base.gains(7 to 2f, 8 to 4f, 9 to 5f).copy(preampDb = -4f), true),
            Preset(id(4), "Loudness", base.gains(0 to 5f, 1 to 3f, 8 to 2f, 9 to 4f).copy(preampDb = -4f), true),
            Preset(id(5), "Vocal & Dialogue", base.gains(0 to -2f, 6 to 2f, 7 to 1.5f)
                .copy(dialogueBoost = 0.6f, levelerEnabled = true), true),
            Preset(id(6), "Podcast", base.gains(0 to -6f, 1 to -3f, 6 to 2f)
                .copy(dialogueBoost = 0.8f, levelerEnabled = true, levelerTargetDb = -18f), true),
            Preset(id(7), "Cinema", base.gains(0 to 4f).copy(
                spatialEnabled = true, upmix = Upmix.Surround71, roomSize = 0.55f, reverbMix = 0.25f,
                bassBoostDb = 3f, dialogueBoost = 0.35f, levelerEnabled = true, preampDb = -3f), true),
            Preset(id(8), "Music Hall", base.copy(spatialEnabled = true, roomSize = 0.85f, reverbMix = 0.4f)
                .sourceAt(0, -40f, 2.5f).sourceAt(1, 40f, 2.5f), true),
            Preset(id(9), "Studio Speakers", base.copy(spatialEnabled = true, roomSize = 0.2f, reverbMix = 0.12f), true),
            Preset(id(10), "Gaming", base.gains(7 to 2f, 1 to 2f)
                .copy(spatialEnabled = true, upmix = Upmix.Surround51, roomSize = 0.15f, reverbMix = 0.05f), true),
            Preset(id(11), "Wide Speakers", base.copy(mode = OutputMode.Speakers, width = 1.5f, crosstalkEnabled = true), true),
            Preset(id(12), "Phone Speaker", base.copy(
                mode = OutputMode.Speakers, width = 1.4f, crosstalkEnabled = true, speakerSpan = 20f,
                bassBoostDb = 6f, levelerEnabled = true, preampDb = -3f,
                bands = base.bands.mapIndexed { i, b -> if (i == 0) b.copy(type = BandType.HighPass, frequency = 80f, q = 0.7f) else b }), true),
        )
    }
}

/** AutoEq / Equalizer APO "ParametricEQ.txt" import and export. */
object AutoEq {
    data class Result(val preampDb: Float, val bands: List<EqBand>, val skipped: Int)

    private val types = mapOf(
        "PK" to BandType.Peak, "PEQ" to BandType.Peak,
        "LS" to BandType.LowShelf, "LSC" to BandType.LowShelf, "LSQ" to BandType.LowShelf,
        "HS" to BandType.HighShelf, "HSC" to BandType.HighShelf, "HSQ" to BandType.HighShelf,
        "LP" to BandType.LowPass, "LPQ" to BandType.LowPass, "HP" to BandType.HighPass, "HPQ" to BandType.HighPass,
    )

    fun parse(text: String): Result {
        var preamp = 0f
        val bands = mutableListOf<EqBand>()
        var skipped = 0
        for (raw in text.lines()) {
            val line = raw.trim()
            if (line.lowercase().startsWith("preamp:")) {
                preamp = line.substringAfter(":").trim().split(" ").firstOrNull()?.toFloatOrNull() ?: 0f
                continue
            }
            if (!line.lowercase().startsWith("filter")) continue
            val tokens = line.split(Regex("\\s+"))
            val upper = tokens.map { it.uppercase() }
            val type = upper.firstNotNullOfOrNull { types[it] }
            fun after(key: String) = upper.indexOf(key).takeIf { it >= 0 && it + 1 < tokens.size }?.let { tokens[it + 1].toFloatOrNull() }
            val fc = after("FC")
            if (type == null || fc == null || bands.size >= SoundSettings.MAX_BANDS) { skipped++; continue }
            bands += EqBand(
                enabled = "OFF" !in upper, type = type, frequency = fc,
                gainDb = after("GAIN") ?: 0f, q = after("Q") ?: if (type == BandType.Peak) 1f else 0.71f,
            )
        }
        require(bands.isNotEmpty()) { "No parametric filters were found in this file." }
        return Result(preamp, bands, skipped)
    }

    fun export(s: SoundSettings): String = buildString {
        appendLine("Preamp: %.1f dB".format(s.preampDb))
        s.bands.forEachIndexed { i, b ->
            val code = when (b.type) {
                BandType.Peak -> "PK"; BandType.LowShelf -> "LSC"; BandType.HighShelf -> "HSC"
                BandType.LowPass -> "LPQ"; BandType.HighPass -> "HPQ"
            }
            appendLine("Filter %d: %s %s Fc %.0f Hz Gain %.1f dB Q %.2f".format(
                i + 1, if (b.enabled) "ON" else "OFF", code, b.frequency, b.gainDb, b.q))
        }
    }
}
