import AVFoundation
import Combine
import MediaPlayer
import UIKit

/// Owns the player, engine, library and settings. Single source of truth for the iOS UI.
final class PlayerState: ObservableObject {
    let dsp = DSPEngine()
    let library = Library()
    let headTracker = HeadTracker()
    let analyzer: Analyzer
    private let player: PlayerEngine
    private let store = PresetStore()
    private var saved: PresetStore.Library

    @Published var settings: SoundSettings {
        didSet {
            guard settings != oldValue else { return }
            dsp.apply(settings, enabled: enabled)
            scheduleSave()
        }
    }
    @Published var enabled: Bool {
        didSet { dsp.apply(settings, enabled: enabled); scheduleSave() }
    }
    @Published var currentPresetID: UUID?
    @Published private(set) var userPresets: [Preset]
    let builtInPresets = BuiltInPresets.all

    @Published private(set) var currentTrack: Track?
    @Published private(set) var isPlaying = false
    @Published private(set) var position: Double = 0
    @Published var shuffle = false
    @Published var repeatAll = true
    @Published var headTrackingEnabled = false {
        didSet { headTrackingEnabled ? headTracker.start() : headTracker.stop() }
    }
    @Published var selectedBandID: UUID?
    @Published private(set) var routeName = ""
    @Published var lastError: String?

    private var routeUID: String?
    private var positionTimer: Timer?
    private var saveWork: DispatchWorkItem?
    private var cancellables: Set<AnyCancellable> = []

    var allPresets: [Preset] { builtInPresets + userPresets }
    var currentPresetName: String { allPresets.first { $0.id == currentPresetID }?.name ?? "Custom" }
    var duration: Double { currentTrack?.duration ?? 0 }

    init() {
        saved = store.load()
        settings = saved.lastSettings
        enabled = saved.enabled
        userPresets = saved.userPresets
        analyzer = Analyzer(dsp: dsp)
        player = PlayerEngine(dsp: dsp)

        dsp.apply(settings, enabled: enabled)
        headTracker.onYaw = { [weak self] yaw in self?.dsp.setHeadYaw(degrees: yaw) }
        player.onFinished = { [weak self] in self?.next(auto: true) }
        library.objectWillChange.sink { [weak self] in self?.objectWillChange.send() }.store(in: &cancellables)

        do {
            try player.start()
            analyzer.setSampleRate(player.sampleRate)
        } catch {
            lastError = "Audio could not start: \(error.localizedDescription)"
        }
        analyzer.start()
        observeSession()
        setupRemoteCommands()
        updateRoute()

        positionTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            guard let self, self.currentTrack != nil else { return }
            self.position = min(self.player.position, self.duration)
        }
    }

    // MARK: - Playback

    func play(_ track: Track) {
        currentTrack = track
        player.load(library.url(for: track), play: true)
        isPlaying = true
        position = 0
        updateNowPlaying()
    }

    func togglePlay() {
        guard let track = currentTrack ?? library.tracks.first else { return }
        if currentTrack == nil { return play(track) }
        isPlaying ? player.pause() : player.play()
        isPlaying.toggle()
        updateNowPlaying()
    }

    func next(auto: Bool = false) {
        let tracks = library.tracks
        guard !tracks.isEmpty else { return }
        guard let current = currentTrack, let i = tracks.firstIndex(of: current) else { return play(tracks[0]) }
        if shuffle, tracks.count > 1 {
            var j = i
            while j == i { j = Int.random(in: 0..<tracks.count) }
            return play(tracks[j])
        }
        if i + 1 < tracks.count { return play(tracks[i + 1]) }
        if repeatAll || !auto { return play(tracks[0]) }
        isPlaying = false
        updateNowPlaying()
    }

    func previous() {
        let tracks = library.tracks
        guard let current = currentTrack, let i = tracks.firstIndex(of: current) else { return }
        if position > 3 { return seek(to: 0) }
        play(tracks[i > 0 ? i - 1 : tracks.count - 1])
    }

    func seek(to seconds: Double) {
        player.seek(to: seconds)
        position = seconds
        updateNowPlaying()
    }

    func delete(_ track: Track) {
        if track == currentTrack {
            player.stop()
            currentTrack = nil
            isPlaying = false
        }
        library.delete(track)
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

    func deletePreset(_ preset: Preset) {
        userPresets.removeAll { $0.id == preset.id }
        if currentPresetID == preset.id { currentPresetID = nil }
        scheduleSave()
    }

    func importAutoEq(from url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        do {
            let result = try AutoEqImporter.parse(try String(contentsOf: url, encoding: .utf8))
            settings.bands = result.bands
            settings.preampDb = result.preampDb
            settings.eqEnabled = true
            saveCurrentAsPreset(named: url.deletingPathExtension().lastPathComponent
                .replacingOccurrences(of: " ParametricEQ", with: ""))
        } catch {
            lastError = error.localizedDescription
        }
    }

    func setSource(_ index: Int, azimuth: Double, elevation: Double, distance: Double) {
        guard settings.sources.indices.contains(index) else { return }
        settings.sources[index].azimuth = azimuth
        settings.sources[index].elevation = elevation
        settings.sources[index].distance = distance
    }

    func addBand() {
        guard settings.bands.count < SoundSettings.maxBands else { return }
        let band = EQBand(frequency: 1000)
        settings.bands.append(band)
        selectedBandID = band.id
    }

    func removeSelectedBand() {
        settings.bands.removeAll { $0.id == selectedBandID }
        selectedBandID = nil
    }

    // MARK: - Audio session

    private func observeSession() {
        let nc = NotificationCenter.default
        nc.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] note in
            guard let self, let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
                  let type = AVAudioSession.InterruptionType(rawValue: raw) else { return }
            if type == .began {
                self.player.pause()
                self.isPlaying = false
            } else if let opts = note.userInfo?[AVAudioSessionInterruptionOptionKey] as? UInt,
                      AVAudioSession.InterruptionOptions(rawValue: opts).contains(.shouldResume) {
                try? AVAudioSession.sharedInstance().setActive(true)
                self.player.play()
                self.isPlaying = true
            }
            self.updateNowPlaying()
        }
        nc.addObserver(forName: AVAudioSession.routeChangeNotification, object: nil, queue: .main) { [weak self] note in
            guard let self else { return }
            // Pause when headphones are unplugged, as Apple's guidelines require.
            if let raw = note.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt,
               AVAudioSession.RouteChangeReason(rawValue: raw) == .oldDeviceUnavailable {
                self.player.pause()
                self.isPlaying = false
                self.updateNowPlaying()
            }
            self.updateRoute()
        }
        nc.addObserver(forName: .AVAudioEngineConfigurationChange, object: nil, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.player.restart()
            self.analyzer.setSampleRate(self.player.sampleRate)
        }
        nc.addObserver(forName: UIApplication.willResignActiveNotification, object: nil, queue: .main) { [weak self] _ in
            self?.saveNow()
        }
    }

    /// Per-output memory: each headphone / speaker route keeps its own sound.
    private func updateRoute() {
        guard let output = AVAudioSession.sharedInstance().currentRoute.outputs.first else { return }
        routeName = output.portName
        let uid = output.uid
        guard uid != routeUID else { return }
        if let old = routeUID { saved.deviceProfiles[old] = DeviceProfile(presetID: currentPresetID, settings: settings) }
        routeUID = uid
        if let profile = saved.deviceProfiles[uid] {
            currentPresetID = profile.presetID
            settings = profile.settings
        } else {
            let speaker = output.portType == .builtInSpeaker || output.portType == .builtInReceiver
            settings.mode = speaker ? .speakers : .headphones
        }
    }

    // MARK: - Lock screen / Control Centre

    private func setupRemoteCommands() {
        let c = MPRemoteCommandCenter.shared()
        c.playCommand.addTarget { [weak self] _ in
            guard let self, !self.isPlaying else { return .success }
            self.togglePlay(); return .success
        }
        c.pauseCommand.addTarget { [weak self] _ in
            guard let self, self.isPlaying else { return .success }
            self.togglePlay(); return .success
        }
        c.togglePlayPauseCommand.addTarget { [weak self] _ in self?.togglePlay(); return .success }
        c.nextTrackCommand.addTarget { [weak self] _ in self?.next(); return .success }
        c.previousTrackCommand.addTarget { [weak self] _ in self?.previous(); return .success }
        c.changePlaybackPositionCommand.addTarget { [weak self] event in
            guard let e = event as? MPChangePlaybackPositionCommandEvent else { return .commandFailed }
            self?.seek(to: e.positionTime)
            return .success
        }
    }

    private func updateNowPlaying() {
        guard let track = currentTrack else {
            MPNowPlayingInfoCenter.default().nowPlayingInfo = nil
            return
        }
        var info: [String: Any] = [
            MPMediaItemPropertyTitle: track.title,
            MPMediaItemPropertyArtist: track.artist,
            MPMediaItemPropertyPlaybackDuration: track.duration,
            MPNowPlayingInfoPropertyElapsedPlaybackTime: position,
            MPNowPlayingInfoPropertyPlaybackRate: isPlaying ? 1.0 : 0.0,
        ]
        MPNowPlayingInfoCenter.default().nowPlayingInfo = info
        Task {
            if let image = await library.artwork(for: track) {
                info[MPMediaItemPropertyArtwork] = MPMediaItemArtwork(boundsSize: image.size) { _ in image }
                await MainActor.run { MPNowPlayingInfoCenter.default().nowPlayingInfo = info }
            }
        }
    }

    // MARK: - Persistence

    private func scheduleSave() {
        saveWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.saveNow() }
        saveWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.8, execute: work)
    }

    func saveNow() {
        saved.lastSettings = settings
        saved.enabled = enabled
        saved.userPresets = userPresets
        if let uid = routeUID { saved.deviceProfiles[uid] = DeviceProfile(presetID: currentPresetID, settings: settings) }
        store.save(saved)
    }
}
