import SwiftUI

/// Compact menu bar panel: on/off, presets, output device and mode.
struct MenuBarPanel: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var meters: MeterModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("SpatialEQ").font(.headline)
                    RouteStatus()
                }
                Spacer()
                Toggle("", isOn: $state.enabled).toggleStyle(.switch).labelsHidden()
            }

            VStack(spacing: 3) {
                LevelMeter(level: meters.peakL)
                LevelMeter(level: meters.peakR)
            }

            LabeledContent("Output") {
                DevicePicker().labelsHidden()
            }
            LabeledContent("Mode") {
                Picker("", selection: $state.settings.mode) {
                    ForEach(OutputMode.allCases) { Text($0.label).tag($0) }
                }
                .pickerStyle(.segmented).labelsHidden()
            }
            if state.settings.mode == .headphones {
                Toggle("Virtual surround", isOn: $state.settings.spatialEnabled)
            }

            Text("PRESETS").font(.caption2.weight(.bold)).foregroundStyle(.secondary)
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 6) {
                ForEach(state.builtInPresets + state.userPresets) { p in
                    Button {
                        state.apply(p)
                    } label: {
                        Text(p.name).font(.caption).lineLimit(1).frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered)
                    .tint(p.id == state.currentPresetID ? .accentColor : nil)
                }
            }

            Divider()
            HStack {
                Button("Open SpatialEQ") {
                    openWindow(id: "main")
                    NSApp.activate(ignoringOtherApps: true)
                }
                Spacer()
                Button("Quit") { NSApp.terminate(nil) }
            }
        }
        .padding(14)
        .frame(width: 320)
    }
}
