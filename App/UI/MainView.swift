import SwiftUI

struct MainView: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var tracker: HeadTracker

    var body: some View {
        VStack(spacing: 10) {
            TopBar()
            HStack(spacing: 10) {
                ZStack(alignment: .topLeading) {
                    SceneView3D()
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                        .overlay(RoundedRectangle(cornerRadius: 12).stroke(Theme.panelBorder))
                    SceneHint()
                        .padding(12)
                }
                ControlsSidebar()
                    .frame(width: 320)
            }
            EQEditorView()
                .frame(height: state.selectedBandID == nil ? 240 : 280)
        }
        .padding(.horizontal, 12)
        .padding(.bottom, 12)
        .padding(.top, 28)
        .background(Theme.background)
        .preferredColorScheme(.dark)
        .alert("SpatialEQ", isPresented: Binding(get: { state.lastError != nil }, set: { if !$0 { state.lastError = nil } })) {
            Button("OK") { state.lastError = nil }
        } message: {
            Text(state.lastError ?? "")
        }
    }
}

private struct SceneHint: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var tracker: HeadTracker

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(modeTitle).font(.headline)
            Text(hint).font(.caption).foregroundStyle(Theme.textDim)
            if tracker.isRunning {
                Label(String(format: "Head yaw %+.0f°", tracker.yawDegrees), systemImage: "gyroscope")
                    .font(.caption.monospacedDigit()).foregroundStyle(Theme.accent)
            }
        }
        .padding(10)
        .background(.black.opacity(0.35), in: RoundedRectangle(cornerRadius: 8))
        .allowsHitTesting(false)
    }

    private var modeTitle: String {
        if state.settings.mode == .speakers { return "Speakers" }
        return state.settings.spatialEnabled ? "Headphones · \(state.settings.upmix.label)" : "Headphones · Stereo"
    }

    private var hint: String {
        if state.settings.mode == .speakers { return "Drag a speaker to set the span · drag to orbit · scroll to zoom" }
        if state.settings.spatialEnabled { return "Drag sources to move them · ⌥-drag for height · double-click resets view" }
        return "Turn on Spatial to place sound around you"
    }
}

struct TopBar: View {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var meters: MeterModel
    @State private var showSaveSheet = false
    @State private var newPresetName = ""

    var body: some View {
        HStack(spacing: 14) {
            Button {
                state.enabled.toggle()
            } label: {
                Image(systemName: "power")
                    .font(.system(size: 15, weight: .bold))
                    .frame(width: 30, height: 30)
                    .background(state.enabled ? Theme.accent : Color.white.opacity(0.1), in: Circle())
                    .foregroundStyle(state.enabled ? .black : .white)
            }
            .buttonStyle(.plain)
            .help(state.enabled ? "Bypass processing" : "Enable processing")

            VStack(alignment: .leading, spacing: 0) {
                Text("SpatialEQ").font(.system(size: 15, weight: .bold))
                RouteStatus()
            }

            Spacer()

            PresetMenu(showSaveSheet: $showSaveSheet)

            DevicePicker()
                .frame(width: 230)

            VStack(spacing: 3) {
                LevelMeter(level: meters.peakL)
                LevelMeter(level: meters.peakR)
            }
            .frame(width: 90)
            if meters.limiterDb < -0.5 {
                Text(String(format: "LIM %.0f", meters.limiterDb))
                    .font(.system(size: 9, weight: .bold).monospacedDigit())
                    .foregroundStyle(.orange)
            }
        }
        .sheet(isPresented: $showSaveSheet) {
            VStack(alignment: .leading, spacing: 12) {
                Text("Save Preset").font(.headline)
                TextField("Name", text: $newPresetName).frame(width: 260)
                HStack {
                    Spacer()
                    Button("Cancel") { showSaveSheet = false }
                    Button("Save") {
                        state.saveCurrentAsPreset(named: newPresetName.isEmpty ? "My Preset" : newPresetName)
                        newPresetName = ""
                        showSaveSheet = false
                    }
                    .keyboardShortcut(.defaultAction)
                }
            }
            .padding(20)
        }
    }
}

struct RouteStatus: View {
    @EnvironmentObject var state: AppState

    var body: some View {
        switch state.routeState {
        case .stopped:
            Text("Stopped").font(.caption).foregroundStyle(Theme.textDim)
        case let .running(name, rate):
            let latency = state.activeDevice.map { String(format: " · %.0f ms", $0.latencyMs) } ?? ""
            Text("\(name) · \(Int(rate / 1000)) kHz\(latency)").font(.caption).foregroundStyle(Theme.textDim)
        case let .failed(message):
            HStack(spacing: 6) {
                Text(message).font(.caption).foregroundStyle(.orange).lineLimit(1)
                Button("Retry") { state.restartRoute() }.controlSize(.mini)
                Button("Privacy Settings") {
                    NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AudioCapture")!)
                }
                .controlSize(.mini)
            }
        }
    }
}

struct PresetMenu: View {
    @EnvironmentObject var state: AppState
    @Binding var showSaveSheet: Bool

    var body: some View {
        Menu {
            Section("Built-in") {
                ForEach(state.builtInPresets) { p in
                    Button(p.name) { state.apply(p) }
                }
            }
            if !state.userPresets.isEmpty {
                Section("My Presets") {
                    ForEach(state.userPresets) { p in
                        Button(p.name) { state.apply(p) }
                    }
                }
            }
            Divider()
            Button("Save as New Preset…") { showSaveSheet = true }
            if state.userPresets.contains(where: { $0.id == state.currentPresetID }) {
                Button("Update “\(state.currentPresetName)”") { state.updateCurrentPreset() }
                Button("Delete “\(state.currentPresetName)”", role: .destructive) {
                    if let p = state.userPresets.first(where: { $0.id == state.currentPresetID }) { state.deletePreset(p) }
                }
            }
            Button("Import AutoEq Profile…") { state.importAutoEq() }
        } label: {
            Label(state.currentPresetName + (state.isCurrentPresetModified ? " (edited)" : ""), systemImage: "slider.horizontal.3")
        }
        .frame(width: 200)
    }
}

struct DevicePicker: View {
    @EnvironmentObject var state: AppState

    var body: some View {
        Picker(selection: $state.selectedDeviceUID) {
            Text("System Default Output").tag(String?.none)
            Divider()
            ForEach(state.devices.devices) { d in
                Label(d.name, systemImage: d.symbolName).tag(String?.some(d.uid))
            }
        } label: {
            Image(systemName: state.activeDevice?.symbolName ?? "speaker.wave.2")
        }
    }
}
