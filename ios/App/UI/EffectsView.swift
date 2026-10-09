import SwiftUI

struct EffectsView: View {
    @EnvironmentObject var state: PlayerState
    @EnvironmentObject var tracker: HeadTracker

    var body: some View {
        NavigationStack {
            Form {
                Section("Output") {
                    Picker("Mode", selection: $state.settings.mode) {
                        ForEach(OutputMode.allCases) { Text($0.label).tag($0) }
                    }
                    .pickerStyle(.segmented)
                }

                if state.settings.mode == .headphones {
                    Section {
                        Toggle("Virtual surround", isOn: $state.settings.spatialEnabled)
                        if state.settings.spatialEnabled {
                            Picker("Layout", selection: $state.settings.upmix) {
                                ForEach(Upmix.allCases) { Text($0.label).tag($0) }
                            }
                            .pickerStyle(.segmented)
                            Button("Reset source positions") { state.settings.resetSources() }
                        }
                    } header: { Text("Spatial audio") } footer: {
                        Text("If you use AirPods, turn off Apple's Spatial Audio in Control Centre so the sound isn't spatialised twice.")
                    }
                    Section("Head tracking") {
                        Toggle("Track head movement", isOn: $state.headTrackingEnabled)
                            .disabled(!state.settings.spatialEnabled)
                        if state.headTrackingEnabled {
                            HStack {
                                Text(tracker.isConnected ? String(format: "Yaw %+.0f°", tracker.yawDegrees) : "Waiting for AirPods…")
                                    .monospacedDigit().foregroundStyle(.secondary)
                                Spacer()
                                Button("Recentre") { tracker.recenter() }
                            }
                            if let err = tracker.errorMessage { Text(err).font(.footnote).foregroundStyle(.orange) }
                        }
                    }
                } else {
                    Section("Stereo image") {
                        ParamSlider(title: "Width", value: $state.settings.width, range: 0...2.5) { String(format: "%.0f%%", $0 * 100) }
                        Toggle("Crosstalk cancellation", isOn: $state.settings.crosstalkEnabled)
                        if state.settings.crosstalkEnabled {
                            ParamSlider(title: "Strength", value: $state.settings.crosstalkStrength, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                            ParamSlider(title: "Speaker span", value: $state.settings.speakerSpan, range: 10...120) { String(format: "%.0f°", $0) }
                        }
                    }
                }

                Section("Room") {
                    ParamSlider(title: "Room size", value: $state.settings.roomSize, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                    ParamSlider(title: "Reverb", value: $state.settings.reverbMix, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                }

                Section("Enhance") {
                    ParamSlider(title: "Bass boost", value: $state.settings.bassBoostDb, range: 0...12) { String(format: "%.1f dB", $0) }
                    ParamSlider(title: "Dialogue boost", value: $state.settings.dialogueBoost, range: 0...1) { String(format: "%.0f%%", $0 * 100) }
                    Toggle("Volume leveler", isOn: $state.settings.levelerEnabled)
                    if state.settings.levelerEnabled {
                        ParamSlider(title: "Target loudness", value: $state.settings.levelerTargetDb, range: -30 ... -10) { String(format: "%.0f dB", $0) }
                    }
                }

                Section("Output level") {
                    Toggle("Limiter", isOn: $state.settings.limiterEnabled)
                    ParamSlider(title: "Output gain", value: $state.settings.outputGainDb, range: -12...12) { String(format: "%+.1f dB", $0) }
                }
            }
            .scrollContentBackground(.hidden)
            .navigationTitle("Effects")
            .navigationBarTitleDisplayMode(.inline)
        }
    }
}
