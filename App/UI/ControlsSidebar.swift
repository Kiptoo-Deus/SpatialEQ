import SwiftUI

struct ControlsSidebar: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var tracker: HeadTracker
    @EnvironmentObject var meters: MeterModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                section("OUTPUT MODE") {
                    Picker("", selection: $state.settings.mode) {
                        ForEach(OutputMode.allCases) { Text($0.label).tag($0) }
                    }
                    .pickerStyle(.segmented).labelsHidden()
                    if let d = state.activeDevice, d.isHeadphoneLike != (state.settings.mode == .headphones) {
                        Text("\(d.name) looks like \(d.isHeadphoneLike ? "headphones" : "speakers").")
                            .font(.caption2).foregroundStyle(.orange)
                    }
                }

                if state.settings.mode == .headphones { headphoneControls } else { speakerControls }

                section("ROOM") {
                    ParamSlider(title: "Room size", value: $state.settings.roomSize, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                    ParamSlider(title: "Reverb", value: $state.settings.reverbMix, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                }

                section("ENHANCE") {
                    ParamSlider(title: "Bass boost", value: $state.settings.bassBoostDb, range: 0...12) { String(format: "%.1f dB", $0) }
                    ParamSlider(title: "Dialogue boost", value: $state.settings.dialogueBoost, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                    Toggle("Volume leveler", isOn: $state.settings.levelerEnabled)
                    if state.settings.levelerEnabled {
                        ParamSlider(title: "Target loudness", value: $state.settings.levelerTargetDb, range: -30 ... -10) { String(format: "%.0f dB", $0) }
                        Text(String(format: "Leveler gain %+.1f dB", meters.levelerDb))
                            .font(.caption2.monospacedDigit()).foregroundStyle(Theme.textDim)
                    }
                }

                section("OUTPUT") {
                    Toggle("Limiter", isOn: $state.settings.limiterEnabled)
                    if state.settings.limiterEnabled {
                        ParamSlider(title: "Ceiling", value: $state.settings.limiterCeilingDb, range: -6...0) { String(format: "%.1f dB", $0) }
                    }
                    ParamSlider(title: "Output gain", value: $state.settings.outputGainDb, range: -12...12) { String(format: "%+.1f dB", $0) }
                }

                section("APP") {
                    Toggle("Launch at login", isOn: $state.launchAtLogin)
                }
            }
            .toggleStyle(.switch)
            .controlSize(.small)
            .padding(12)
        }
        .panel()
    }

    @ViewBuilder private var headphoneControls: some View {
        section("SPATIAL AUDIO") {
            Toggle("Virtual surround", isOn: $state.settings.spatialEnabled)
            if state.settings.spatialEnabled {
                Picker("", selection: $state.settings.upmix) {
                    ForEach(Upmix.allCases) { Text($0.label).tag($0) }
                }
                .pickerStyle(.segmented).labelsHidden()
                Button("Reset source positions") { state.settings.resetSources() }
                    .controlSize(.small)
            }
        }
        section("HEAD TRACKING") {
            Toggle("Track head movement", isOn: $state.headTrackingEnabled)
                .disabled(!state.settings.spatialEnabled)
            if state.headTrackingEnabled {
                HStack {
                    Text(tracker.isConnected ? "Connected" : "Waiting for AirPods…")
                        .font(.caption).foregroundStyle(tracker.isConnected ? Theme.accent : Theme.textDim)
                    Spacer()
                    Button("Recentre") { tracker.recenter() }.controlSize(.small)
                }
                if let err = tracker.errorMessage {
                    Text(err).font(.caption2).foregroundStyle(.orange)
                }
            }
            if state.activeDevice?.isAirPodsLike == true && state.settings.spatialEnabled {
                Text("Turn off Apple’s Spatial Audio for these AirPods (Control Centre → Sound) so audio isn’t spatialised twice.")
                    .font(.caption2).foregroundStyle(Theme.textDim)
            }
        }
    }

    @ViewBuilder private var speakerControls: some View {
        section("STEREO IMAGE") {
            ParamSlider(title: "Width", value: $state.settings.width, range: 0...2.5) { String(format: "%.0f%%", $0 * 100) }
            Toggle("Crosstalk cancellation", isOn: $state.settings.crosstalkEnabled)
            if state.settings.crosstalkEnabled {
                ParamSlider(title: "Strength", value: $state.settings.crosstalkStrength, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                ParamSlider(title: "Speaker span", value: $state.settings.speakerSpan, range: 10...120) { String(format: "%.0f°", $0) }
                Text("Works best sitting centred between the speakers.").font(.caption2).foregroundStyle(Theme.textDim)
            }
        }
    }

    private func section<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title).font(.caption.weight(.bold)).tracking(1.2).foregroundStyle(Theme.textDim)
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(Color.white.opacity(0.03), in: RoundedRectangle(cornerRadius: 8))
    }
}
