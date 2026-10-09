package com.savannahdsp.spatialeq

import android.content.Context
import android.content.SharedPreferences
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Handler
import android.os.Looper
import com.savannahdsp.spatialeq.audio.NativeEngine
import com.savannahdsp.spatialeq.model.AutoEq
import com.savannahdsp.spatialeq.model.BuiltInPresets
import com.savannahdsp.spatialeq.model.DeviceProfile
import com.savannahdsp.spatialeq.model.EqBand
import com.savannahdsp.spatialeq.model.OutputMode
import com.savannahdsp.spatialeq.model.Preset
import com.savannahdsp.spatialeq.model.SoundSettings
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.update
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

/** An app SpatialEQ has seen playing audio. */
data class AppRef(val packageName: String, val label: String)

/** Processing state shown in the UI. */
data class ProcessingStatus(
    val running: Boolean = false,
    val message: String = "Not running",
    val processedApps: List<AppRef> = emptyList(),
    val protectedApps: List<AppRef> = emptyList(),
    val sessionAccess: Boolean = false, // DUMP permission granted (full system-wide session discovery)
)

/** App-wide state: one DSP engine, the settings, presets and per-device profiles. */
object AppStore {
    lateinit var engine: NativeEngine
        private set

    val settings = MutableStateFlow(SoundSettings())
    val enabled = MutableStateFlow(true)
    val presetId = MutableStateFlow<String?>(null)
    val userPresets = MutableStateFlow<List<Preset>>(emptyList())
    val excludedPackages = MutableStateFlow<Set<String>>(emptySet())
    val status = MutableStateFlow(ProcessingStatus())
    val outputName = MutableStateFlow("")
    val selectedBandId = MutableStateFlow<String?>(null)

    val builtInPresets get() = BuiltInPresets.all
    val allPresets get() = builtInPresets + userPresets.value

    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private lateinit var prefs: SharedPreferences
    private var profiles = mutableMapOf<String, DeviceProfile>()
    private var currentDeviceKey: String? = null

    @OptIn(FlowPreview::class)
    fun init(context: Context) {
        engine = NativeEngine()
        prefs = context.getSharedPreferences("spatialeq", Context.MODE_PRIVATE)
        runCatching { settings.value = json.decodeFromString(prefs.getString("settings", null)!!) }
        runCatching { userPresets.value = json.decodeFromString(prefs.getString("presets", null)!!) }
        runCatching { profiles = json.decodeFromString(prefs.getString("profiles", null)!!) }
        runCatching { excludedPackages.value = json.decodeFromString(prefs.getString("excluded", null)!!) }
        enabled.value = prefs.getBoolean("enabled", true)
        presetId.value = prefs.getString("presetId", null)

        // Push every change to the engine immediately; persist after a short pause.
        combine(settings, enabled) { s, e -> s.pack(e) }.onEach { engine.setParams(it) }.launchIn(scope)
        combine(settings, enabled, userPresets, presetId, excludedPackages) { _, _, _, _, _ -> }
            .debounce(800).onEach { save() }.launchIn(scope)

        refreshSessionAccess(context)
        trackOutputDevice(context)
    }

    /** Re-checks the one-time ADB grant (called at launch and whenever the app comes to the front). */
    fun refreshSessionAccess(context: Context) {
        val granted = context.checkSelfPermission(android.Manifest.permission.DUMP) ==
            android.content.pm.PackageManager.PERMISSION_GRANTED
        status.update { it.copy(sessionAccess = granted) }
    }

    fun update(transform: (SoundSettings) -> SoundSettings) = settings.update(transform)

    fun apply(preset: Preset) {
        presetId.value = preset.id
        settings.value = preset.settings
    }

    fun saveCurrentAsPreset(name: String) {
        val p = Preset(name = name, settings = settings.value)
        userPresets.update { it + p }
        presetId.value = p.id
    }

    fun deletePreset(p: Preset) {
        userPresets.update { list -> list.filterNot { it.id == p.id } }
        if (presetId.value == p.id) presetId.value = null
    }

    val currentPresetName get() = allPresets.firstOrNull { it.id == presetId.value }?.name ?: "Custom"

    fun importAutoEq(text: String, name: String): String? = runCatching {
        val r = AutoEq.parse(text)
        update { it.copy(bands = r.bands, preampDb = r.preampDb, eqEnabled = true) }
        saveCurrentAsPreset(name.removeSuffix(" ParametricEQ"))
        if (r.skipped > 0) "Imported; skipped ${r.skipped} unsupported line(s)." else null
    }.getOrElse { it.message ?: "Import failed" }

    fun addBand() {
        if (settings.value.bands.size >= SoundSettings.MAX_BANDS) return
        val band = EqBand(frequency = 1000f)
        update { it.copy(bands = it.bands + band) }
        selectedBandId.value = band.id
    }

    fun removeBand(id: String) {
        update { s -> s.copy(bands = s.bands.filterNot { it.id == id }) }
        selectedBandId.value = null
    }

    fun setExcluded(pkg: String, excluded: Boolean) =
        excludedPackages.update { if (excluded) it + pkg else it - pkg }

    // Per-output-device memory -----------------------------------------------------------------

    private fun trackOutputDevice(context: Context) {
        val am = context.getSystemService(AudioManager::class.java)
        val refresh: () -> Unit = {
            val outputs = am.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
            val preferred = outputs.firstOrNull { it.type in headphoneTypes } ?: outputs.firstOrNull {
                it.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
            }
            if (preferred != null) onOutputChanged(preferred)
        }
        am.registerAudioDeviceCallback(object : AudioDeviceCallback() {
            override fun onAudioDevicesAdded(added: Array<out AudioDeviceInfo>) = refresh()
            override fun onAudioDevicesRemoved(removed: Array<out AudioDeviceInfo>) = refresh()
        }, Handler(Looper.getMainLooper()))
        refresh()
    }

    private val headphoneTypes = setOf(
        AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_WIRED_HEADSET,
        AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudioDeviceInfo.TYPE_USB_HEADSET,
        AudioDeviceInfo.TYPE_BLE_HEADSET,
    )

    private fun onOutputChanged(device: AudioDeviceInfo) {
        val key = "${device.type}:${device.productName}"
        if (key == currentDeviceKey) return
        val isSpeaker = device.type == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER
        outputName.value = if (isSpeaker) "Phone speaker" else device.productName.toString()
        currentDeviceKey?.let { profiles[it] = DeviceProfile(presetId.value, settings.value) }
        val known = profiles[key]
        if (known != null) {
            presetId.value = known.presetId
            settings.value = known.settings
        } else if (currentDeviceKey != null) {
            update { it.copy(mode = if (isSpeaker) OutputMode.Speakers else OutputMode.Headphones) }
        }
        currentDeviceKey = key
    }

    private fun save() {
        currentDeviceKey?.let { profiles[it] = DeviceProfile(presetId.value, settings.value) }
        prefs.edit()
            .putString("settings", json.encodeToString(settings.value))
            .putString("presets", json.encodeToString(userPresets.value))
            .putString("profiles", json.encodeToString(profiles.toMap()))
            .putString("excluded", json.encodeToString(excludedPackages.value))
            .putBoolean("enabled", enabled.value)
            .putString("presetId", presetId.value)
            .apply()
    }
}
