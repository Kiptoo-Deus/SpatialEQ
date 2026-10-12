package com.savannahdsp.spatialeq.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.audiofx.AudioEffect
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Process
import android.util.Log
import androidx.core.content.ContextCompat
import com.savannahdsp.spatialeq.AppRef
import com.savannahdsp.spatialeq.AppStore
import com.savannahdsp.spatialeq.MainActivity
import com.savannahdsp.spatialeq.ProcessingStatus
import com.savannahdsp.spatialeq.R
import kotlinx.coroutines.flow.update

/**
 * System-wide processing:
 *   other apps --(AudioPlaybackCapture)--> AudioRecord --> DSP engine --> AudioTrack --> output
 * while [SessionMuter] silences the originals so only the processed copy is heard.
 *
 * Every app is heard exactly once: an app's original is silenced only while the system confirms
 * it is being captured, and an app that is captured but can't be silenced is dropped from the
 * capture so it plays untouched.
 */
class CaptureService : Service() {
    private var projection: MediaProjection? = null
    private var worker: Thread? = null
    @Volatile private var running = false
    private val muter = SessionMuter()
    private lateinit var discovery: SessionDiscovery
    private val handler = Handler(Looper.getMainLooper())
    private val announcedSessions = mutableMapOf<Int, String>() // from effect-control broadcasts
    @Volatile private var captureFilter = CaptureFilter(emptySet(), emptySet())
    private val blockedUids = mutableSetOf<Int>() // captured apps we couldn't silence (main thread)
    @Volatile private var pendingFailures = emptySet<Int>() // sessions whose mute failed last round

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        discovery = SessionDiscovery(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> { stopSelf(); return START_NOT_STICKY }
            ACTION_START -> {
                startForeground(NOTIFICATION_ID, notification(), ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION)
                val code = intent.getIntExtra(EXTRA_RESULT_CODE, 0)
                @Suppress("DEPRECATION")
                val data: Intent? = intent.getParcelableExtra(EXTRA_RESULT_DATA)
                if (data == null || running) return START_NOT_STICKY
                val mp = getSystemService(MediaProjectionManager::class.java).getMediaProjection(code, data)
                if (mp == null) { setStatus("Screen-capture permission was not granted"); stopSelf(); return START_NOT_STICKY }
                mp.registerCallback(object : MediaProjection.Callback() {
                    override fun onStop() { stopSelf() }
                }, handler)
                projection = mp
                registerEffectBroadcasts()
                startProcessing(mp)
            }
        }
        return START_NOT_STICKY
    }

    private fun startProcessing(mp: MediaProjection) {
        val am = getSystemService(AudioManager::class.java)
        val sampleRate = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
        val framesPerBuffer = 512
        // The capture side (remote submix) delivers audio in bursts of ~1024 frames, and Bluetooth
        // output pulls in large chunks too, so the path needs real headroom between them: a small
        // low-latency buffer runs dry constantly, which is heard as stutter and hangs.
        val bufferFrames = maxOf(4096, sampleRate / 10) // ~100 ms
        val headroomFrames = 2048                         // ~43 ms kept queued in the output
        val engine = AppStore.engine

        // Probe that capture is permitted before starting.
        buildRecord(mp, sampleRate, bufferFrames, CaptureFilter(null, emptySet()))?.release()
            ?: run { setStatus("Microphone permission is required to capture other apps' audio"); stopSelf(); return }
        captureFilter = initialFilter()

        val trackMin = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT)
        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .setAllowedCapturePolicy(AudioAttributes.ALLOW_CAPTURE_BY_NONE) // never re-capture ourselves
                    .build())
            .setAudioFormat(AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                .setSampleRate(sampleRate)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                .build())
            .setBufferSizeInBytes(maxOf(trackMin, bufferFrames * 8))
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        engine.prepare(sampleRate, 4096)
        AppStore.settings.value.let { engine.setParams(it.pack(AppStore.enabled.value)) }

        running = true
        worker = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
            val buffer = FloatArray(framesPerBuffer * 2)
            val silence = FloatArray(headroomFrames * 2)
            var record: AudioRecord? = null
            var filter: CaptureFilter? = null
            var underruns = 0
            track.write(silence, 0, silence.size, AudioTrack.WRITE_BLOCKING) // start with headroom
            track.play()
            while (running) {
                // The capture filter is fixed per AudioRecord, so rebuild it when it changes.
                val wanted = captureFilter
                if (wanted != filter) {
                    record?.let { runCatching { it.stop() }; it.release() }
                    record = if (wanted.matchUids?.isEmpty() == true) null
                        else buildRecord(mp, sampleRate, bufferFrames, wanted)?.also { it.startRecording() }
                    filter = wanted
                }
                val read = record?.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING)
                    ?: buffer.size.also { buffer.fill(0f) } // nothing to capture: let effect tails ring out
                if (read <= 0) continue
                engine.process(buffer, read / 2)
                // If the output ran dry anyway (capture and output clocks drift), rebuild the headroom.
                val now = track.underrunCount
                if (now != underruns) {
                    underruns = now
                    track.write(silence, 0, silence.size, AudioTrack.WRITE_NON_BLOCKING)
                }
                track.write(buffer, 0, read, AudioTrack.WRITE_BLOCKING) // paces the loop in real time
            }
            record?.let { runCatching { it.stop() }; it.release() }
            runCatching { track.stop() }; track.release()
        }, "SpatialEQ-audio").also { it.start() }

        setStatus("Processing system audio")
        handler.post(sessionPoller)
    }

    /**
     * What the recorder captures. [matchUids] null: every app's media except [excludeUids] (used
     * with session access, where we can see exactly what is captured). Otherwise only those apps.
     */
    private data class CaptureFilter(val matchUids: Set<Int>?, val excludeUids: Set<Int>)

    private fun initialFilter() =
        if (discovery.hasDumpPermission) CaptureFilter(null, uidsOf(AppStore.excludedPackages.value))
        else CaptureFilter(emptySet(), emptySet())

    private fun uidsOf(packages: Collection<String>): Set<Int> =
        packages.mapNotNull { runCatching { packageManager.getPackageUid(it, 0) }.getOrNull() }.toSet()

    private fun buildRecord(mp: MediaProjection, sampleRate: Int, bufferFrames: Int, filter: CaptureFilter): AudioRecord? {
        val config = AudioPlaybackCaptureConfiguration.Builder(mp)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
        val match = filter.matchUids
        if (match == null) (filter.excludeUids + Process.myUid()).forEach(config::excludeUid)
        else match.forEach(config::addMatchingUid)
        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
            .setSampleRate(sampleRate)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build()
        val min = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT)
        return try {
            AudioRecord.Builder()
                .setAudioFormat(format)
                .setBufferSizeInBytes(maxOf(min, bufferFrames * 8))
                .setAudioPlaybackCaptureConfig(config.build())
                .build()
        } catch (e: SecurityException) {
            null
        }
    }

    /**
     * Twice a second: mute exactly the players that are being captured, so every app is heard
     * once. With session access the system tells us which players are captured; a captured app
     * that can't be silenced (another app holds its effect, ...) is dropped from the capture
     * instead, so it plays unprocessed rather than doubled. Without session access, only players
     * that announce their session are muted, and only those apps are captured.
     */
    private val sessionPoller = object : Runnable {
        override fun run() {
            if (!running) return
            Thread {
                val excluded = AppStore.excludedPackages.value
                val excludedUids = uidsOf(excluded)
                val sessions = discovery.scan()
                // Only worth the (large) flinger dump when a mute failed last time.
                val orphans = if (pendingFailures.isNotEmpty()) discovery.orphanedDynamicsSessions() else emptySet()
                val announced = synchronized(announcedSessions) {
                    announcedSessions.filterValues { it !in excluded && discovery.appTargetsCaptureByDefault(it) }
                        .mapNotNull { (session, pkg) ->
                            runCatching { packageManager.getPackageUid(pkg, 0) }.getOrNull()?.let { session to (it to pkg) }
                        }.toMap()
                }
                handler.post {
                    if (!running) return@post
                    if (discovery.hasDumpPermission) {
                        val toMute = sessions.filter { it.captured && it.uid !in excludedUids && it.uid !in blockedUids }
                        muter.sync(toMute.map { it.sessionId }.toSet())
                        val silenced = muter.silencedSessions + orphans
                        // A captured player we failed to silence is heard twice. Give it one more
                        // round (the effect may just have been taken), then stop capturing that app.
                        val failed = toMute.filter { it.sessionId !in silenced }
                        failed.filter { it.sessionId in pendingFailures }.forEach { blockedUids += it.uid }
                        pendingFailures = failed.map { it.sessionId }.toSet()
                        captureFilter = CaptureFilter(null, excludedUids + blockedUids)
                        val processed = toMute.filter { it.sessionId in silenced }.map { it.packageName }
                        val unprocessed = sessions.filter { it.active && it.uid !in excludedUids && it.packageName !in processed }
                            .map { it.packageName }
                        publish(processed, unprocessed)
                    } else {
                        muter.sync(announced.keys)
                        val silenced = muter.silencedSessions
                        // Capture an app only when all of its sessions are silenced.
                        val uids = announced.entries.groupBy({ it.value.first }, { it.key })
                            .filterValues { s -> s.all { it in silenced } }.keys
                        captureFilter = CaptureFilter(uids, emptySet())
                        publish(announced.values.filter { it.first in uids }.map { it.second }, emptyList())
                    }
                }
            }.start()
            handler.postDelayed(this, 500)
        }
    }

    private fun publish(processed: List<String>, unprocessed: List<String>) {
        AppStore.status.update {
            it.copy(
                processedApps = processed.distinct().map { p -> AppRef(p, discovery.label(p)) },
                protectedApps = unprocessed.distinct().map { p -> AppRef(p, discovery.label(p)) },
                sessionAccess = discovery.hasDumpPermission)
        }
    }

    /** Players that announce their sessions (no special permission needed). */
    private val effectReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val session = intent.getIntExtra(AudioEffect.EXTRA_AUDIO_SESSION, -1)
            val pkg = intent.getStringExtra(AudioEffect.EXTRA_PACKAGE_NAME) ?: return
            if (session <= 0 || pkg == packageName) return
            synchronized(announcedSessions) {
                when (intent.action) {
                    AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION -> announcedSessions[session] = pkg
                    AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION -> announcedSessions.remove(session)
                }
            }
        }
    }

    private fun registerEffectBroadcasts() {
        val filter = IntentFilter().apply {
            addAction(AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION)
            addAction(AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION)
        }
        ContextCompat.registerReceiver(this, effectReceiver, filter, ContextCompat.RECEIVER_EXPORTED)
    }

    override fun onDestroy() {
        running = false
        handler.removeCallbacks(sessionPoller)
        worker?.join(500)
        muter.releaseAll() // un-mute every app immediately
        runCatching { unregisterReceiver(effectReceiver) }
        projection?.stop()
        projection = null
        AppStore.status.value = ProcessingStatus(message = "Not running", sessionAccess = discovery.hasDumpPermission)
        super.onDestroy()
    }

    private fun setStatus(msg: String) {
        AppStore.status.update { it.copy(running = running, message = msg, sessionAccess = discovery.hasDumpPermission) }
        Log.i("CaptureService", msg)
    }

    private fun notification(): Notification {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Audio processing", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, CaptureService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE)
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("SpatialEQ is processing audio")
            .setContentText("EQ and spatial effects are applied to your apps")
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .setOngoing(true)
            .build()
    }

    companion object {
        const val ACTION_START = "com.savannahdsp.spatialeq.START"
        const val ACTION_STOP = "com.savannahdsp.spatialeq.STOP"
        const val EXTRA_RESULT_CODE = "code"
        const val EXTRA_RESULT_DATA = "data"
        private const val CHANNEL = "processing"
        private const val NOTIFICATION_ID = 1

        fun start(context: Context, resultCode: Int, data: Intent) {
            context.startForegroundService(Intent(context, CaptureService::class.java)
                .setAction(ACTION_START).putExtra(EXTRA_RESULT_CODE, resultCode).putExtra(EXTRA_RESULT_DATA, data))
        }

        fun stop(context: Context) {
            context.startService(Intent(context, CaptureService::class.java).setAction(ACTION_STOP))
        }
    }
}
