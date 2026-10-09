package com.savannahdsp.spatialeq.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.savannahdsp.spatialeq.AppStore
import com.savannahdsp.spatialeq.model.Preset

const val PRIVACY_POLICY_URL = "https://kiptoo-deus.github.io/SpatialEQ/privacy-policy.html"

@Composable
fun PresetsScreen(modifier: Modifier = Modifier) {
    val current by AppStore.presetId.collectAsState()
    val user by AppStore.userPresets.collectAsState()
    var naming by remember { mutableStateOf(false) }
    val context = androidx.compose.ui.platform.LocalContext.current
    val privacyRequired by Ads.privacyOptionsRequired.collectAsState()
    var name by remember { mutableStateOf("") }

    LazyColumn(modifier.fillMaxSize().statusBarsPadding().padding(horizontal = 12.dp)) {
        item {
            Row(Modifier.fillMaxWidth().padding(vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Presets", fontWeight = FontWeight.Bold, fontSize = 20.sp, modifier = Modifier.weight(1f))
                TextButton(onClick = { naming = true }) { Text("Save current") }
            }
        }
        item { Header("Built-in") }
        items(AppStore.builtInPresets, key = { it.id }) { PresetRow(it, it.id == current, null) }
        if (user.isNotEmpty()) {
            item { Header("My presets") }
            items(user, key = { it.id }) { p -> PresetRow(p, p.id == current) { AppStore.deletePreset(p) } }
        }
        item {
            Row(Modifier.fillMaxWidth().padding(vertical = 16.dp)) {
                TextButton(onClick = { context.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW,
                    android.net.Uri.parse(PRIVACY_POLICY_URL))) }) { Text("Privacy policy") }
                TextButton(onClick = { context.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW,
                    android.net.Uri.parse("https://savannahdsp.com"))) }) { Text("savannahdsp.com") }
                if (privacyRequired) TextButton(onClick = { (context as? android.app.Activity)?.let(Ads::showPrivacyOptions) }) {
                    Text("Ad privacy choices")
                }
            }
        }
    }

    if (naming) AlertDialog(
        onDismissRequest = { naming = false },
        title = { Text("Save preset") },
        text = { OutlinedTextField(name, { name = it }, label = { Text("Name") }, singleLine = true) },
        confirmButton = {
            TextButton(onClick = { AppStore.saveCurrentAsPreset(name.ifBlank { "My Preset" }); name = ""; naming = false }) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = { naming = false }) { Text("Cancel") } },
    )
}

@Composable
private fun Header(text: String) =
    Text(text.uppercase(), color = Palette.dim, fontSize = 11.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.2.sp,
        modifier = Modifier.padding(top = 12.dp, bottom = 6.dp))

@Composable
private fun PresetRow(p: Preset, selected: Boolean, onDelete: (() -> Unit)?) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp).background(Palette.panel, RoundedCornerShape(10.dp))
            .clickable { AppStore.apply(p) }.padding(horizontal = 14.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(p.name, Modifier.weight(1f))
        if (selected) Icon(Icons.Filled.Check, "Selected", tint = Palette.accent)
        onDelete?.let { IconButton(onClick = it) { Icon(Icons.Filled.Delete, "Delete preset") } }
    }
}
