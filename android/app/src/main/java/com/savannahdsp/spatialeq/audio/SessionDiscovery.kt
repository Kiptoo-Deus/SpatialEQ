package com.savannahdsp.spatialeq.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.IBinder
import android.os.ParcelFileDescriptor
import android.os.Process
import java.io.FileInputStream

/** An app's audio player as reported by the audio policy service. */
data class PlaybackSession(
    val sessionId: Int,
    val uid: Int,
    val packageName: String,
    val active: Boolean,
    /** The system is routing this player into a playback capture right now. */
    val captured: Boolean,
)

/**
 * Finds other apps' audio sessions by reading the audio policy service's dump. This needs the
 * DUMP permission, granted once over ADB:
 *
 *     adb shell pm grant com.savannahdsp.spatialeq android.permission.DUMP
 *
 * The policy dump is the ground truth for capture: a player that is being captured has
 * "Secondary Outputs" (the capture mix). Flags in the player list are not reliable; AAudio
 * players, for example, report flags=0x0 there even when they forbid capture.
 *
 * Without DUMP, only players that announce their session (ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION)
 * can be processed.
 */
class SessionDiscovery(private val context: Context) {
    private val pm = context.packageManager
    private val myUid = Process.myUid()

    val hasDumpPermission: Boolean
        get() = context.checkSelfPermission(Manifest.permission.DUMP) == PackageManager.PERMISSION_GRANTED

    /** Usages that playback capture is configured for (and that we therefore may mute). */
    private val capturedUsages = setOf("MEDIA", "GAME", "UNKNOWN")

    /** Other apps' media players, with whether each one is being captured. Empty without DUMP. */
    fun scan(): List<PlaybackSession> {
        if (!hasDumpPermission) return emptyList()
        val dump = dump("media.audio_policy") ?: return emptyList()

        // Each client block looks like:
        //   Port ID: 101; Session ID: 1129; uid 10300; State: Active
        //   AUDIO_FORMAT_PCM_FLOAT; 44100; Channel mask: 0x3
        //   Attributes: { Content type: ... Usage: AUDIO_USAGE_MEDIA Source: AUDIO_SOURCE_DEFAULT ... }
        //   Stream: 3; Flags: 00000008; Refcount: 1; InternalMute: No
        //   DAP Primary Mix: 0x0
        //   DAP Secondary Outputs: 117,        <- only while captured
        // The same client can be listed several times (per output, active list); merge them.
        val sessions = mutableMapOf<Int, PlaybackSession>()
        var current: PlaybackSession? = null
        var playback = false
        var linesLeft = 0
        fun flush() {
            val c = current ?: return
            if (playback) sessions.merge(c.sessionId, c) { a, b ->
                a.copy(active = a.active || b.active, captured = a.captured || b.captured)
            }
            current = null
        }
        for (line in dump.lineSequence()) {
            val header = clientRegex.find(line)
            if (header != null) {
                flush()
                val (session, uid, state) = header.destructured
                val s = session.toInt(); val u = uid.toInt()
                if (s > 0 && u != myUid) {
                    val pkg = pm.getPackagesForUid(u)?.firstOrNull() ?: "uid:$u"
                    current = PlaybackSession(s, u, pkg, active = state == "Active", captured = false)
                    playback = false
                    linesLeft = 5
                }
                continue
            }
            val c = current ?: continue
            if (linesLeft-- <= 0) { flush(); continue }
            usageRegex.find(line)?.let { m ->
                // Recording clients have a real source (MIC, REMOTE_SUBMIX...); players don't.
                val source = sourceRegex.find(line)?.groupValues?.get(1) ?: "INVALID"
                playback = m.groupValues[1] in capturedUsages && (source == "INVALID" || source == "DEFAULT")
            }
            if (line.contains("Secondary Outputs:") && line.substringAfter("Secondary Outputs:").any(Char::isDigit))
                current = c.copy(captured = true)
        }
        flush()
        return sessions.values.toList()
    }

    /**
     * Sessions carrying a Dynamics Processing effect that no process owns any more. Effects that
     * were muting a session outlive our process when it is killed (reinstall, force stop), and
     * stay enabled, so the session is still silent; a new effect can't be attached next to them
     * (the engine fails with -3). Such a session counts as silenced.
     */
    fun orphanedDynamicsSessions(): Set<Int> {
        if (!hasDumpPermission) return emptySet()
        val dump = dump("media.audio_flinger") ?: return emptySet()
        val result = mutableSetOf<Int>()
        var session = -1
        var dynamics = false
        for (line in dump.lineSequence()) {
            effectsHeaderRegex.find(line)?.let { session = it.groupValues[1].toInt(); dynamics = false; return@let }
            if (session <= 0) continue
            if (line.contains("- name:")) dynamics = line.contains("Dynamics Processing")
            else if (dynamics && clientsRegex.find(line)?.groupValues?.get(1) == "0") result += session
        }
        return result
    }

    /** Apps targeting API 29+ allow playback capture unless they opt out. */
    fun appTargetsCaptureByDefault(pkg: String): Boolean =
        runCatching { pm.getApplicationInfo(pkg, 0).targetSdkVersion >= 29 }.getOrDefault(true)

    fun label(pkg: String): String =
        runCatching { pm.getApplicationLabel(pm.getApplicationInfo(pkg, 0)).toString() }.getOrDefault(pkg)

    /** Dumps a system service through its binder (public IBinder.dump); falls back to dumpsys. */
    private fun dump(service: String): String? {
        val binder = runCatching {
            Class.forName("android.os.ServiceManager").getMethod("getService", String::class.java)
                .invoke(null, service) as IBinder?
        }.getOrNull()
        if (binder != null) {
            runCatching {
                val (readEnd, writeEnd) = ParcelFileDescriptor.createPipe()
                var text = ""
                // Drain the pipe concurrently so a large dump can't block the writer.
                val reader = Thread { FileInputStream(readEnd.fileDescriptor).use { text = it.readBytes().decodeToString() } }
                reader.start()
                try {
                    binder.dump(writeEnd.fileDescriptor, emptyArray())
                } finally {
                    writeEnd.close()
                }
                reader.join(3000)
                readEnd.close()
                if (text.isNotBlank() && !text.contains("Permission Denial")) return text
            }
        }
        return runCatching {
            val proc = ProcessBuilder("dumpsys", service).redirectErrorStream(true).start()
            proc.inputStream.bufferedReader().readText().also { proc.waitFor() }
        }.getOrNull()?.takeIf { it.isNotBlank() && !it.contains("Permission Denial") }
    }

    private companion object {
        val clientRegex = Regex("""Port ID: \d+; Session ID: (\d+); uid (\d+); State: (\w+)""")
        val usageRegex = Regex("""Usage: AUDIO_USAGE_(\w+)""")
        val sourceRegex = Regex("""Source: AUDIO_SOURCE_(\w+)""")
        val effectsHeaderRegex = Regex("""^\s*\d+ effects for session (\d+)""")
        val clientsRegex = Regex("""^\s*(\d+) Clients:""")
    }
}
