package com.savannahdsp.spatialeq

import android.Manifest
import android.app.Application
import android.content.pm.PackageManager
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Apps
import androidx.compose.material.icons.filled.Equalizer
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.SurroundSound
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.launch
import com.savannahdsp.spatialeq.audio.CaptureService
import androidx.compose.foundation.layout.Column
import com.savannahdsp.spatialeq.ui.Ads
import com.savannahdsp.spatialeq.ui.AppCatalog
import com.savannahdsp.spatialeq.ui.BannerAd
import com.savannahdsp.spatialeq.ui.AppsScreen
import com.savannahdsp.spatialeq.ui.EffectsScreen
import com.savannahdsp.spatialeq.ui.EqScreen
import com.savannahdsp.spatialeq.ui.HomeScreen
import com.savannahdsp.spatialeq.ui.Palette
import com.savannahdsp.spatialeq.ui.PresetsScreen
import com.savannahdsp.spatialeq.ui.SpatialEqTheme
import com.savannahdsp.spatialeq.ui.rememberLiveSpectrum

class SpatialEqApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        AppStore.init(this)
    }
}

class MainActivity : ComponentActivity() {
    private val projectionRequest = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        val data = result.data
        if (result.resultCode == RESULT_OK && data != null) CaptureService.start(this, result.resultCode, data)
        else AppStore.status.value = AppStore.status.value.copy(message = "Capture permission was declined")
    }

    private val permissionRequest = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { granted ->
        if (granted[Manifest.permission.RECORD_AUDIO] == true) requestProjection()
        else AppStore.status.value = AppStore.status.value.copy(
            message = "SpatialEQ needs the audio-recording permission to capture other apps' sound")
    }

    /** Starts system-wide processing: runtime permissions, then the MediaProjection consent. */
    fun startProcessing() {
        val needed = buildList {
            add(Manifest.permission.RECORD_AUDIO)
            if (Build.VERSION.SDK_INT >= 33) add(Manifest.permission.POST_NOTIFICATIONS)
        }.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (needed.isEmpty()) requestProjection() else permissionRequest.launch(needed.toTypedArray())
    }

    fun stopProcessing() = CaptureService.stop(this)

    private fun requestProjection() {
        projectionRequest.launch(getSystemService(MediaProjectionManager::class.java).createScreenCaptureIntent())
    }

    override fun onResume() {
        super.onResume()
        AppStore.refreshSessionAccess(this)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        lifecycleScope.launch { AppCatalog.get(this@MainActivity) } // warm the Apps list
        Ads.init(this)
        setContent {
            SpatialEqTheme {
                var tab by rememberSaveable { mutableIntStateOf(0) }
                val spectrum = rememberLiveSpectrum()
                Scaffold(
                    containerColor = Palette.background,
                    bottomBar = {
                        Column {
                        // One banner on Home and Presets only, away from sliders and the 3D controls.
                        if (tab == 0 || tab == 4) BannerAd()
                        NavigationBar(containerColor = Palette.panel) {
                            listOf(
                                "Home" to Icons.Filled.Home, "EQ" to Icons.Filled.Equalizer,
                                "Effects" to Icons.Filled.SurroundSound, "Apps" to Icons.Filled.Apps,
                                "Presets" to Icons.Filled.LibraryMusic,
                            ).forEachIndexed { i, (label, icon) ->
                                NavigationBarItem(selected = tab == i, onClick = { tab = i },
                                    icon = { Icon(icon, label) }, label = { Text(label) })
                            }
                        }
                        }
                    },
                ) { padding ->
                    val m = Modifier.padding(padding)
                    when (tab) {
                        0 -> HomeScreen(spectrum, onStart = ::startProcessing, onStop = ::stopProcessing, modifier = m)
                        1 -> EqScreen(spectrum, m)
                        2 -> EffectsScreen(m)
                        3 -> AppsScreen(m)
                        else -> PresetsScreen(m)
                    }
                }
            }
        }
    }
}
