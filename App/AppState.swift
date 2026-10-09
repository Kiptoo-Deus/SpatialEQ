import AppKit
import Combine
import Foundation
import ServiceManagement
import UniformTypeIdentifiers

/// Live meter values, kept separate from AppState so 15 Hz updates don't redraw every view.
final class MeterModel: ObservableObject {
    @Published var peakL: Float = 0
    @Published var peakR: Float = 0
    @Published var limiterDb: Float = 0
    @Published var levelerDb: Float = 0
}

/// Owns the engine, routing, devices and settings; the single source of truth for the UI.
final class AppState: ObservableObject {
    let dsp = DSPEngine()
    let devices = DeviceManager()
    let headTracker = HeadTracker()
    let meters = MeterModel()
    let analyzer: Analyzer
    private let router: AudioRouter
    private let store = PresetStore()
    private var library: PresetStore.Library

    @Published var settings: SoundSettings {
        didSet {
            guard settings != oldValue else { return }
            dsp.apply(settings, enabled: enabled)
            scheduleSave()
        }
    }
    @Published var enabled: Bool {
        didSet {
            dsp.apply(settings, enabled: enabled)
            scheduleSave()
        }
    }
    @Published var currentPresetID: UUID?
    @Published private(set) var userPresets: [Preset]
    let builtInPresets = BuiltInPresets.all

    /// nil = follow the macOS default output.
    @Published var selectedDeviceUID: String? {
        didSet { if selectedDeviceUID != oldValue { scheduleRouteRestart(); scheduleSave() } }
    }
    @Published private(set) var activeDeviceUID: String?
    @Published private(set) var routeState: AudioRouter.State = .stopped
    @Published var headTrackingEnabled: Bool {
        didSet {
            headTrackingEnabled ? headTracker.start() : headTracker.stop()
            scheduleSave()
        }
    }
    @Published var selectedBandID: UUID?
    @Published var launchAtLogin: Bool = SMAppService.mainApp.status == .enabled {
        didSet {
            do {
                if launchAtLogin { try SMAppService.mainApp.register() } else { try SMAppService.mainApp.unregister() }
            } catch {
                lastError = "Launch at login: \(error.localizedDescription)"
            }
        }
    }
    @Published var lastError: String?
    /// Off while generating README media so the user's settings aren't overwritten.
    var persistenceEnabled = true

    private var restartWork: DispatchWorkItem?
    private var saveWork: DispatchWorkItem?
    private var meterTimer: Timer?
    private var cancellables: Set<AnyCancellable> = []

    var activeDevice: OutputDevice? { devices.device(uid: activeDeviceUID) }
    var allPresets: [Preset] { builtInPresets + userPresets }

    init() {
        library = store.load()
        settings = library.lastSettings
        enabled = library.enabled
        userPresets = library.userPresets
        selectedDeviceUID = library.selectedDeviceUID
        headTrackingEnabled = false
        analyzer = Analyzer(dsp: dsp)
        router = AudioRouter(dsp: dsp)

        dsp.apply(settings, enabled: enabled)

        router.onStateChange = { [weak self] state in
            DispatchQueue.main.async {
                self?.routeState = state
                if case let .running(_, rate) = state { self?.analyzer.setSampleRate(rate) }
            }
        }
        router.onNeedsRestart = { [weak self] in self?.scheduleRouteRestart(force: true) }
        // Device list changes (including the one our own aggregate device causes) only matter
        // if they change which device we should be playing to.
        devices.onChange = { [weak self] in self?.scheduleRouteRestart(force: false) }
        headTracker.onYaw = { [weak self] yaw in self?.dsp.setHeadYaw(degrees: yaw) }

        // Re-publish device list changes so views observing AppState refresh.
        devices.objectWillChange.sink { [weak self] in self?.objectWillChange.send() }.store(in: &cancellables)

        analyzer.start()
        startMeters()
        restartRoute()
        // Property observers don't run inside init, so start tracking explicitly.
        if library.headTracking {
            headTrackingEnabled = true
            headTracker.start()
        }
    }

    // MARK: - Routing

    func scheduleRouteRestart(force: Bool = false) {
        restartWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.restartRoute(force: force) }
        restartWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3, execute: work)
    }

    func restartRoute(force: Bool = true) {
        devices.refresh()
        let uid = selectedDeviceUID.flatMap { devices.device(uid: $0) != nil ? $0 : nil } ?? devices.defaultOutputUID
        if !force, uid == activeDeviceUID, case .running = router.state { return }
        guard let uid, let device = devices.device(uid: uid) else {
            router.stop()
            routeState = .failed("No output device available")
            return
        }
        if uid != activeDeviceUID { switchProfile(from: activeDeviceUID, to: device) }
        activeDeviceUID = uid
        router.start(output: device)
    }

    /// Per-device memory: stash the current sound for the old device, restore (or guess) for the new one.
    private func switchProfile(from old: String?, to device: OutputDevice) {
        if let old {
            library.deviceProfiles[old] = DeviceProfile(presetID: currentPresetID, settings: settings)
        }
        if let profile = library.deviceProfiles[device.uid] {
            currentPresetID = profile.presetID
            settings = profile.settings
        } else if old != nil {
            // First time on this device: keep the EQ but pick the output mode that fits the hardware.
            settings.mode = device.isHeadphoneLike ? .headphones : .speakers
        }
    }

    // MARK: - Presets

    func apply(_ preset: Preset) {
        currentPresetID = preset.id
        settings = preset.settings
    }

    func saveCurrentAsPreset(named name: String) {
        let preset = Preset(name: name, settings: settings)
        userPresets.append(preset)
        currentPresetID = preset.id
        scheduleSave()
    }

    func updateCurrentPreset() {
        guard let id = currentPresetID, let i = userPresets.firstIndex(where: { $0.id == id }) else { return }
        userPresets[i].settings = settings
        scheduleSave()
    }

    func deletePreset(_ preset: Preset) {
        userPresets.removeAll { $0.id == preset.id }
        if currentPresetID == preset.id { currentPresetID = nil }
        scheduleSave()
    }

    var currentPresetName: String {
        allPresets.first { $0.id == currentPresetID }?.name ?? "Custom"
    }

    var isCurrentPresetModified: Bool {
        guard let p = allPresets.first(where: { $0.id == currentPresetID }) else { return false }
        return p.settings != settings
    }

    func importAutoEq() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.plainText, UTType(filenameExtension: "txt")!]
        panel.message = "Choose an AutoEq or Equalizer APO ParametricEQ.txt file"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let text = try String(contentsOf: url, encoding: .utf8)
            let result = try AutoEqImporter.parse(text)
            settings.bands = result.bands
            settings.preampDb = result.preampDb
            settings.eqEnabled = true
            let name = url.deletingPathExtension().lastPathComponent
                .replacingOccurrences(of: " ParametricEQ", with: "")
            saveCurrentAsPreset(named: name)
            if result.skipped > 0 { lastError = "Imported, but skipped \(result.skipped) unsupported line(s)." }
        } catch {
            lastError = error.localizedDescription
        }
    }

    func exportEq() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "\(currentPresetName) ParametricEQ.txt"
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try AutoEqImporter.export(settings).write(to: url, atomically: true, encoding: .utf8)
        } catch {
            lastError = error.localizedDescription
        }
    }

    // MARK: - Editing helpers

    func addBand() {
        guard settings.bands.count < SoundSettings.maxBands else { return }
        let band = EQBand(frequency: 1000)
        settings.bands.append(band)
        selectedBandID = band.id
    }

    func removeSelectedBand() {
        guard let id = selectedBandID else { return }
        settings.bands.removeAll { $0.id == id }
        selectedBandID = nil
    }

    func setSource(_ index: Int, azimuth: Double, elevation: Double, distance: Double) {
        guard settings.sources.indices.contains(index) else { return }
        settings.sources[index].azimuth = azimuth
        settings.sources[index].elevation = elevation
        settings.sources[index].distance = distance
    }

    // MARK: - Persistence / meters

    private func scheduleSave() {
        saveWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.saveNow() }
        saveWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8, execute: work)
    }

    func saveNow() {
        guard persistenceEnabled else { return }
        library.lastSettings = settings
        library.enabled = enabled
        library.userPresets = userPresets
        library.selectedDeviceUID = selectedDeviceUID
        library.headTracking = headTrackingEnabled
        if let uid = activeDeviceUID {
            library.deviceProfiles[uid] = DeviceProfile(presetID: currentPresetID, settings: settings)
        }
        store.save(library)
    }

    func shutdown() {
        saveNow()
        router.stop()
        analyzer.stop()
    }

    private func startMeters() {
        meterTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 15.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            let m = self.dsp.meters
            self.meters.peakL = m.peakL
            self.meters.peakR = m.peakR
            self.meters.limiterDb = m.limiterReductionDb
            self.meters.levelerDb = m.levelerGainDb
        }
    }
}
