package com.savannahdsp.spatialeq.ui

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.graphics.drawable.toBitmap
import com.savannahdsp.spatialeq.AppStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** A launchable app the user can include in or exclude from processing. */
data class InstalledApp(val packageName: String, val label: String, val system: Boolean)

/** Icons are decoded off the main thread as rows appear, and kept for the session. */
private val iconCache = java.util.concurrent.ConcurrentHashMap<String, ImageBitmap>()

@Composable
private fun AppIcon(packageName: String) {
    val context = LocalContext.current
    val icon by produceState(iconCache[packageName], packageName) {
        if (value == null) value = withContext(Dispatchers.IO) {
            runCatching { context.packageManager.getApplicationIcon(packageName).toBitmap(96, 96).asImageBitmap() }
                .getOrNull()?.also { iconCache[packageName] = it }
        }
    }
    val bmp = icon
    if (bmp != null) Image(bmp, null, Modifier.size(40.dp).clip(RoundedCornerShape(10.dp)))
    else Box(Modifier.size(40.dp).background(Palette.panel.copy(alpha = 0.6f), RoundedCornerShape(10.dp)))
}

/** Installed-app list, loaded once in the background when SpatialEQ starts. */
object AppCatalog {
    @Volatile private var cached: List<InstalledApp>? = null
    private val lock = kotlinx.coroutines.sync.Mutex()

    suspend fun get(context: Context): List<InstalledApp> =
        cached ?: lock.withLockCompat { cached ?: loadApps(context.applicationContext).also { cached = it } }

    private suspend fun <T> kotlinx.coroutines.sync.Mutex.withLockCompat(block: suspend () -> T): T {
        lock(); try { return block() } finally { unlock() }
    }
}

private suspend fun loadApps(context: Context): List<InstalledApp> = withContext(Dispatchers.IO) {
    val pm = context.packageManager
    val launcher = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
    pm.queryIntentActivities(launcher, PackageManager.MATCH_ALL)
        .map { it.activityInfo.applicationInfo }
        .distinctBy { it.packageName }
        .filter { it.packageName != context.packageName }
        .map { info ->
            InstalledApp(
                packageName = info.packageName,
                label = pm.getApplicationLabel(info).toString(),
                system = info.flags and android.content.pm.ApplicationInfo.FLAG_SYSTEM != 0,
            )
        }
        .sortedBy { it.label.lowercase() }
}

/**
 * Lists every installed app with a switch: on = SpatialEQ processes its audio, off = its audio
 * plays untouched (it is neither captured nor silenced).
 */
@Composable
fun AppsScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val excluded by AppStore.excludedPackages.collectAsState()
    val status by AppStore.status.collectAsState()
    var apps by remember { mutableStateOf<List<InstalledApp>?>(null) }
    var query by remember { mutableStateOf("") }
    LaunchedEffect(Unit) { apps = AppCatalog.get(context) }

    val playing = status.processedApps.map { it.packageName }.toSet()
    val blocked = status.protectedApps.map { it.packageName }.toSet()

    Column(modifier.fillMaxSize().statusBarsPadding().padding(horizontal = 12.dp)) {
        Row(Modifier.fillMaxWidth().padding(vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Apps", fontWeight = FontWeight.Bold, fontSize = 20.sp)
                val total = apps?.size ?: 0
                val on = apps?.count { it.packageName !in excluded } ?: 0
                Text("$on of $total apps processed", color = Palette.dim, fontSize = 12.sp)
            }
            TextButton(onClick = { apps?.let { list -> AppStore.excludedPackages.value = excluded - list.map { it.packageName }.toSet() } }) { Text("All") }
            TextButton(onClick = { apps?.let { list -> AppStore.excludedPackages.value = excluded + list.map { it.packageName } } }) { Text("None") }
        }
        OutlinedTextField(
            value = query, onValueChange = { query = it }, singleLine = true,
            leadingIcon = { Icon(Icons.Filled.Search, null) }, placeholder = { Text("Search apps") },
            modifier = Modifier.fillMaxWidth(),
        )
        Text("Switch an app off to leave its sound untouched. Changes apply immediately.",
            color = Palette.dim, fontSize = 12.sp, modifier = Modifier.padding(vertical = 8.dp))

        val list = apps
        if (list == null) {
            Box(Modifier.fillMaxWidth().padding(32.dp), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
        } else {
            val shown = list.filter { query.isBlank() || it.label.contains(query, ignoreCase = true) }
                .sortedWith(compareByDescending<InstalledApp> { it.packageName in playing }.thenBy { it.system }.thenBy { it.label.lowercase() })
            LazyColumn(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                items(shown, key = { it.packageName }) { app ->
                    AppRow(app, enabled = app.packageName !in excluded, playing = app.packageName in playing,
                        blocked = app.packageName in blocked) { on -> AppStore.setExcluded(app.packageName, !on) }
                }
                item { Spacer(Modifier.padding(8.dp)) }
            }
        }
    }
}

@Composable
private fun AppRow(app: InstalledApp, enabled: Boolean, playing: Boolean, blocked: Boolean, onToggle: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().background(Palette.panel, RoundedCornerShape(10.dp))
            .clickable(enabled = !blocked) { onToggle(!enabled) }.padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        AppIcon(app.packageName)
        Column(Modifier.weight(1f).padding(start = 12.dp)) {
            Text(app.label, maxLines = 1, overflow = TextOverflow.Ellipsis)
            when {
                blocked -> Text("This app blocks audio capture", color = Palette.accent2, fontSize = 11.sp)
                playing -> Text("Playing now · processed", color = Palette.accent, fontSize = 11.sp)
                else -> Text(app.packageName, color = Palette.dim, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        Spacer(Modifier.width(8.dp))
        Switch(checked = enabled && !blocked, onCheckedChange = onToggle, enabled = !blocked)
    }
}
