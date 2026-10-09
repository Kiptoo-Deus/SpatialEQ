package com.savannahdsp.spatialeq.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.savannahdsp.spatialeq.AppStore
import com.savannahdsp.spatialeq.model.OutputMode
import com.savannahdsp.spatialeq.model.SoundSettings
import com.savannahdsp.spatialeq.model.Upmix

@Composable
fun EffectsScreen(modifier: Modifier = Modifier) {
    val s by AppStore.settings.collectAsState()
    val pct = { v: Float -> "%.0f%%".format(v * 100) }

    Column(modifier.fillMaxSize().statusBarsPadding().verticalScroll(rememberScrollState()).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Effects", fontWeight = FontWeight.Bold, fontSize = 20.sp)

        Section("Output") {
            Segmented(OutputMode.entries, s.mode, { it.label }) { m -> AppStore.update { it.copy(mode = m) } }
        }

        if (s.mode == OutputMode.Headphones) {
            Section("Spatial audio") {
                Toggle("Virtual surround", s.spatialEnabled) { v -> AppStore.update { it.copy(spatialEnabled = v) } }
                if (s.spatialEnabled) {
                    Segmented(Upmix.entries, s.upmix, { it.label }) { u -> AppStore.update { it.copy(upmix = u) } }
                    TextButton(onClick = { AppStore.update { it.copy(sources = SoundSettings.defaultSources()) } }) { Text("Reset source positions") }
                }
            }
        } else {
            Section("Stereo image") {
                ParamSlider("Width", s.width, 0f..2.5f, pct) { v -> AppStore.update { it.copy(width = v) } }
                Toggle("Crosstalk cancellation", s.crosstalkEnabled) { v -> AppStore.update { it.copy(crosstalkEnabled = v) } }
                if (s.crosstalkEnabled) {
                    ParamSlider("Strength", s.crosstalkStrength, 0f..1f, pct) { v -> AppStore.update { it.copy(crosstalkStrength = v) } }
                    ParamSlider("Speaker span", s.speakerSpan, 10f..120f, { "%.0f°".format(it) }) { v -> AppStore.update { it.copy(speakerSpan = v) } }
                }
            }
        }

        Section("Room") {
            ParamSlider("Room size", s.roomSize, 0f..1f, pct) { v -> AppStore.update { it.copy(roomSize = v) } }
            ParamSlider("Reverb", s.reverbMix, 0f..1f, pct) { v -> AppStore.update { it.copy(reverbMix = v) } }
        }

        Section("Enhance") {
            ParamSlider("Bass boost", s.bassBoostDb, 0f..12f, { "%.1f dB".format(it) }) { v -> AppStore.update { it.copy(bassBoostDb = v) } }
            ParamSlider("Dialogue boost", s.dialogueBoost, 0f..1f, pct) { v -> AppStore.update { it.copy(dialogueBoost = v) } }
            Toggle("Volume leveler", s.levelerEnabled) { v -> AppStore.update { it.copy(levelerEnabled = v) } }
            if (s.levelerEnabled) ParamSlider("Target loudness", s.levelerTargetDb, -30f..-10f, { "%.0f dB".format(it) }) { v ->
                AppStore.update { it.copy(levelerTargetDb = v) }
            }
        }

        Section("Output level") {
            Toggle("Limiter", s.limiterEnabled) { v -> AppStore.update { it.copy(limiterEnabled = v) } }
            ParamSlider("Output gain", s.outputGainDb, -12f..12f, { "%+.1f dB".format(it) }) { v -> AppStore.update { it.copy(outputGainDb = v) } }
        }

    }
}

@Composable
private fun Section(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().background(Palette.panel, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(title.uppercase(), color = Palette.dim, fontSize = 11.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.2.sp)
        content()
    }
}

@Composable
private fun Toggle(title: String, value: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(title, Modifier.weight(1f))
        Switch(value, onChange)
    }
}

@Composable
private fun <T> Segmented(options: List<T>, selected: T, label: (T) -> String, onSelect: (T) -> Unit) {
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        options.forEachIndexed { i, o ->
            SegmentedButton(selected = o == selected, onClick = { onSelect(o) },
                shape = SegmentedButtonDefaults.itemShape(i, options.size)) { Text(label(o), fontSize = 12.sp) }
        }
    }
}
