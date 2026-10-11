package com.savannahdsp.spatialeq.ui

import android.app.Activity
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.SurroundSound
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.savannahdsp.spatialeq.AppStore
import com.savannahdsp.spatialeq.BuildConfig

const val WEBSITE_URL = "https://savannahdsp.com"

@Composable
fun SettingsScreen(onBack: () -> Unit, modifier: Modifier = Modifier) {
    BackHandler(onBack = onBack)
    val context = LocalContext.current
    val clipboard = LocalClipboardManager.current
    val enabled by AppStore.enabled.collectAsState()
    val status by AppStore.status.collectAsState()
    val privacyRequired by Ads.privacyOptionsRequired.collectAsState()
    fun open(url: String) = context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))

    Column(modifier.fillMaxSize().statusBarsPadding().verticalScroll(rememberScrollState()).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
            Text("Settings", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        }

        Group("Processing") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Effects on", Modifier.weight(1f))
                Switch(enabled, { AppStore.enabled.value = it })
            }
            Text(if (status.sessionAccess) "Full system-wide access: granted" else "Full system-wide access: not granted",
                color = if (status.sessionAccess) Palette.accent else Palette.accent2, fontSize = 13.sp)
            if (!status.sessionAccess) {
                Text("Connect your phone to a computer with USB debugging on and run:", color = Palette.dim, fontSize = 12.sp)
                Row(Modifier.fillMaxWidth().background(Color.Black.copy(alpha = 0.35f), RoundedCornerShape(8.dp)).padding(start = 10.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    Text(GRANT_COMMAND, Modifier.weight(1f), fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                    IconButton(onClick = { clipboard.setText(AnnotatedString(GRANT_COMMAND)) }) {
                        Icon(Icons.Filled.ContentCopy, "Copy command", Modifier.size(18.dp))
                    }
                }
            }
        }

        Group("Privacy") {
            LinkRow("Privacy policy") { open(PRIVACY_POLICY_URL) }
            if (privacyRequired) LinkRow("Ad privacy choices") { (context as? Activity)?.let(Ads::showPrivacyOptions) }
            Text("Audio is processed on your phone and never recorded or sent anywhere.", color = Palette.dim, fontSize = 12.sp)
        }

        Group("About") {
            Column(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Icon(Icons.Filled.SurroundSound, null, tint = Palette.accent, modifier = Modifier.size(44.dp))
                Text("SpatialEQ", fontWeight = FontWeight.Bold, fontSize = 22.sp)
                Text("Version ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})", color = Palette.dim, fontSize = 12.sp)
                Text("Developed by Savannah DSP", fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 10.dp))
                Text("savannahdsp.com", color = Palette.accent, fontSize = 15.sp,
                    modifier = Modifier.clickable { open(WEBSITE_URL) }.padding(6.dp))
                Text("© 2026 Savannah DSP", color = Palette.dim, fontSize = 11.sp)
            }
        }
    }
}

@Composable
private fun Group(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().background(Palette.panel, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(title.uppercase(), color = Palette.dim, fontSize = 11.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.2.sp)
        content()
    }
}

@Composable
private fun LinkRow(text: String, onClick: () -> Unit) =
    Text(text, color = Palette.accent, modifier = Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = 6.dp))
