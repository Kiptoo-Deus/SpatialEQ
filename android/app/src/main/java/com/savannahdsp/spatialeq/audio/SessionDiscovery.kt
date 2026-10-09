package com.savannahdsp.spatialeq.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.IBinder
import android.os.ParcelFileDescriptor
import android.os.Process
import java.io.FileInputStream

/** An app's audio player as reported by the system audio service. */
data class PlaybackSession(
    val sessionId: Int,
    val uid: Int,
    val packageName: String,
    val usage: String,
    val state: String,
    val captureAllowed: Boolean,
)

/**
 * Finds other apps' audio sessions by reading the system audio service's dump. This needs the
 * DUMP permission, granted once over ADB:
 *
 *     adb shell pm grant com.savannahdsp.spatialeq android.permission.DUMP
 *
 * Without it, only players that announce their session (ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION)
 * can be processed.
 */
class SessionDiscovery(private val context: Context) {
    private val pm = context.packageManager
    private val myUid = Process.myUid()

    val hasDumpPermission: Boolean
        get() = context.checkSelfPermission(Manifest.permission.DUMP) == PackageManager.PERMISSION_GRANTED

    /** Usages that playback capture is configured for (and that we therefore may mute). */
    private val capturedUsages = setOf("USAGE_MEDIA", "USAGE_GAME", "USAGE_UNKNOWN")

    fun scan(): List<PlaybackSession> {
        if (!hasDumpPermission) return emptyList()
        val audioDump = dump("audio") ?: return emptyList()
        val policyDump = dump("media.audio_policy").orEmpty()
        val captureDeniedPackages = captureDeniedRegex.findAll(policyDump)
            .filter { it.groupValues[1] == "false" }
            .map { it.groupValues[2].removePrefix("shared:") }
            .toSet()

        val sessions = mutableMapOf<Int, PlaybackSession>()
        for (line in audioDump.lineSequence()) {
            if (!line.contains("AudioPlaybackConfiguration")) continue
            val uid = uidRegex.find(line)?.groupValues?.get(1)?.toIntOrNull() ?: continue
            val session = sessionRegex.find(line)?.groupValues?.get(1)?.toIntOrNull() ?: continue
            val usage = usageRegex.find(line)?.groupValues?.get(1) ?: continue
            val state = stateRegex.find(line)?.groupValues?.get(1) ?: "unknown"
            if (uid == myUid || session <= 0 || usage !in capturedUsages || state == "released") continue
            val flags = flagsRegex.find(line)?.groupValues?.get(1)?.toIntOrNull(16) ?: 0
            val pkg = pm.getPackagesForUid(uid)?.firstOrNull() ?: "uid:$uid"
            // FLAG_NO_MEDIA_PROJECTION (1 << 10) / FLAG_NO_SYSTEM_CAPTURE (1 << 12): the app forbids capture.
            val allowed = pkg !in captureDeniedPackages && flags and ((1 shl 10) or (1 shl 12)) == 0 &&
                appTargetsCaptureByDefault(pkg)
            sessions[session] = PlaybackSession(session, uid, pkg, usage, state, allowed)
        }
        return sessions.values.toList()
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
        val uidRegex = Regex("""u/pid:(\d+)/""")
        val sessionRegex = Regex("""sessionId:(\d+)""")
        val usageRegex = Regex("""usage=(\w+)""")
        val stateRegex = Regex("""state:(\w+)""")
        val flagsRegex = Regex("""flags=0x([0-9A-Fa-f]+)""")
        val captureDeniedRegex = Regex("""allowPlaybackCapture=(\S+?)\s*,.+?packageName=(\S+)""")
    }
}
