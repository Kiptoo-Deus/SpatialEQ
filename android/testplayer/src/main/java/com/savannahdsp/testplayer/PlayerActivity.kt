package com.savannahdsp.testplayer

import android.app.Activity
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Bundle
import android.widget.TextView
import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.pow
import kotlin.math.sin

/** Loops a synthesised chord progression with drums through a normal USAGE_MEDIA AudioTrack. */
class PlayerActivity : Activity() {
    @Volatile private var playing = true
    private var thread: Thread? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(TextView(this).apply { text = "Test Player: playing music"; textSize = 22f; setPadding(48, 200, 48, 48) })
        thread = Thread(::play).also { it.start() }
    }

    override fun onDestroy() {
        playing = false
        thread?.join(500)
        super.onDestroy()
    }

    private fun play() {
        val sr = 48000
        val track = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(sr)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
            .setBufferSizeInBytes(sr * 8 / 5)
            .build()
        track.play()
        val chords = listOf(intArrayOf(57, 60, 64, 67), intArrayOf(53, 57, 60, 64), intArrayOf(48, 52, 55, 60), intArrayOf(55, 59, 62, 65))
        fun hz(m: Int) = 440.0 * 2.0.pow((m - 69) / 12.0)
        val beat = 60.0 / 112
        val buf = FloatArray(960)
        var n = 0L
        while (playing) {
            for (i in 0 until buf.size / 2) {
                val t = (n + i) / sr.toDouble()
                val bar = (t / (4 * beat)).toInt()
                val chord = chords[bar % 4]
                val inBeat = t % beat
                var l = 0.0; var r = 0.0
                chord.forEachIndexed { k, m -> val v = sin(2 * PI * hz(m) * t) * 0.06; l += v * (1.2 - k * 0.15); r += v * (0.6 + k * 0.15) }
                val bass = sin(2 * PI * hz(chord[0] - 24) * t) * 0.25 * exp(-(t % (beat / 2)) * 6)
                val kick = sin(2 * PI * (50 + 90 * exp(-inBeat * 30)) * inBeat) * exp(-inBeat * 9) * 0.5
                val arp = sin(2 * PI * hz(chord[((t / (beat / 4)).toInt()) % 4] + 12) * t) * exp(-(t % (beat / 4)) * 12) * 0.12
                l += bass + kick + arp; r += bass + kick + arp * 0.4
                buf[i * 2] = l.toFloat(); buf[i * 2 + 1] = r.toFloat()
            }
            n += buf.size / 2
            track.write(buf, 0, buf.size, AudioTrack.WRITE_BLOCKING)
        }
        track.stop(); track.release()
    }
}
