package com.savannahdsp.spatialeq.audio

import android.media.audiofx.AudioEffect
import android.media.audiofx.DynamicsProcessing
import android.util.Log

/**
 * Silences another app's original output by attaching a DynamicsProcessing effect with -200 dB
 * input gain to its audio session. Playback capture still receives that app's audio (capture
 * happens before session effects), so the user hears only SpatialEQ's processed copy.
 *
 * Only sessions whose app allows capture may be muted, otherwise the app would go silent.
 */
class SessionMuter {
    private val effects = mutableMapOf<Int, DynamicsProcessing>()

    val mutedSessions: Set<Int> get() = effects.keys

    fun mute(sessionId: Int): Boolean {
        if (sessionId in effects) return true
        return try {
            val dp = DynamicsProcessing(Int.MAX_VALUE, sessionId, null)
            dp.setInputGainAllChannelsTo(-200f)
            dp.enabled = true
            // Another app with higher priority can take the effect over; re-apply when we get it back.
            dp.setControlStatusListener { effect, granted ->
                if (granted) (effect as? DynamicsProcessing)?.apply { setInputGainAllChannelsTo(-200f); enabled = true }
            }
            dp.setEnableStatusListener { effect, enabled ->
                if (!enabled) runCatching { effect.enabled = true }
            }
            effects[sessionId] = dp
            true
        } catch (e: Exception) {
            Log.w(TAG, "Could not mute session $sessionId: ${e.message}")
            false
        }
    }

    fun unmute(sessionId: Int) {
        effects.remove(sessionId)?.release()
    }

    /** Keeps exactly [sessions] muted. */
    fun sync(sessions: Set<Int>) {
        (effects.keys - sessions).forEach(::unmute)
        sessions.forEach(::mute)
    }

    fun releaseAll() {
        effects.values.forEach(AudioEffect::release)
        effects.clear()
    }

    private companion object { const val TAG = "SessionMuter" }
}
