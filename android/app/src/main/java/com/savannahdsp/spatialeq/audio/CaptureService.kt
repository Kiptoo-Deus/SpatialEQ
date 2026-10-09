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
 */
class CaptureService : Service() {
    private var projection: MediaProjection? = null
    private var worker: Thread? = null
    @Volatile private var running = false
    private val muter = SessionMuter()
    private lateinit var discovery: SessionDiscovery
    private val handler = Handler(Looper.getMainLooper())
    private val announcedSessions = mutableMapOf<Int, String>() // from effect-control broadcasts

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
        val framesPerBuffer = 480 // 10 ms at 48 kHz
        val engine = AppStore.engine

        val record0 = buildRecord(mp, sampleRate, framesPerBuffer, AppStore.excludedPackages.value)
            ?: run { setStatus("Microphone permission is required to capture other apps' audio"); stopSelf(); return }

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
            .setBufferSizeInBytes(maxOf(trackMin, framesPerBuffer * 8) * 2)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
            .build()

        engine.prepare(sampleRate, 4096)
        AppStore.settings.value.let { engine.setParams(it.pack(AppStore.enabled.value)) }

        running = true
        worker = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
            val buffer = FloatArray(framesPerBuffer * 2)
            var record = record0
            var excluded = AppStore.excludedPackages.value
            record.startRecording()
            track.play()
            while (running) {
                // Apps switched off in the Apps screen must not be captured either, otherwise they
                // would be heard twice (original + processed). The capture filter is fixed per
                // AudioRecord, so rebuild it when the selection changes.
                val wanted = AppStore.excludedPackages.value
                if (wanted != excluded) {
                    buildRecord(mp, sampleRate, framesPerBuffer, wanted)?.let { fresh ->
                        runCatching { record.stop() }; record.release()
                        record = fresh
                        record.startRecording()
                    }
                    excluded = wanted
                }
                val read = record.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING)
                if (read <= 0) continue
                engine.process(buffer, read / 2)
                track.write(buffer, 0, read, AudioTrack.WRITE_BLOCKING)
            }
            runCatching { record.stop() }; record.release()
            runCatching { track.stop() }; track.release()
        }, "SpatialEQ-audio").also { it.start() }

        setStatus("Processing system audio")
        handler.post(sessionPoller)
    }

    /** Capture of every app's media/game audio except ours and the ones the user switched off. */
    private fun buildRecord(mp: MediaProjection, sampleRate: Int, framesPerBuffer: Int, excluded: Set<String>): AudioRecord? {
        val config = AudioPlaybackCaptureConfiguration.Builder(mp)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .addMatchingUsage(AudioAttributes.USAGE_GAME)
            .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
            .excludeUid(Process.myUid())
        excluded.mapNotNull { runCatching { packageManager.getPackageUid(it, 0) }.getOrNull() }.distinct().forEach(config::excludeUid)
        val format = AudioFormat.Builder()
            .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
            .setSampleRate(sampleRate)
            .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
            .build()
        val min = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT)
        return try {
            AudioRecord.Builder()
                .setAudioFormat(format)
                .setBufferSizeInBytes(maxOf(min, framesPerBuffer * 8) * 2)
                .setAudioPlaybackCaptureConfig(config.build())
                .build()
        } catch (e: SecurityException) {
            null
        }
    }

    /** Every second: find playing apps, mute the ones we capture, report the rest. */
    private val sessionPoller = object : Runnable {
        override fun run() {
            if (!running) return
            Thread {
                val excluded = AppStore.excludedPackages.value
                val sessions = discovery.scan()
                val toMute = sessions.filter { it.captureAllowed && it.packageName !in excluded }.map { it.sessionId }.toMutableSet()
                synchronized(announcedSessions) {
                    val denied = sessions.filter { !it.captureAllowed }.map { it.packageName }.toSet()
                    announcedSessions.filterValues {
                        it !in excluded && it !in denied && discovery.appTargetsCaptureByDefault(it)
                    }.keys.forEach(toMute::add)
                }
                handler.post {
                    if (!running) return@post
                    muter.sync(toMute)
                    val processed = (sessions.filter { it.sessionId in muter.mutedSessions }.map { it.packageName } +
                        synchronized(announcedSessions) { announcedSessions.filterKeys { it in muter.mutedSessions }.values })
                        .distinct().map { AppRef(it, discovery.label(it)) }
                    val protected = sessions.filter { !it.captureAllowed }.map { it.packageName }.distinct()
                        .map { AppRef(it, discovery.label(it)) }
                    AppStore.status.update {
                        it.copy(processedApps = processed, protectedApps = protected, sessionAccess = discovery.hasDumpPermission)
                    }
                }
            }.start()
            handler.postDelayed(this, 1000)
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
