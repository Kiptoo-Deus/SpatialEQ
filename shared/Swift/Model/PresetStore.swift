import Foundation

/// JSON persistence in ~/Library/Application Support/SpatialEQ.
struct PresetStore {
    struct Library: Codable {
        var userPresets: [Preset] = []
        var deviceProfiles: [String: DeviceProfile] = [:] // keyed by device UID
        var selectedDeviceUID: String?                   // nil = follow the system default output
        var enabled = true
        var headTracking = false
        var lastSettings = SoundSettings()
    }

    let directory: URL

    init() {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        directory = base.appendingPathComponent("SpatialEQ", isDirectory: true)
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    private var libraryURL: URL { directory.appendingPathComponent("library.json") }

    func load() -> Library {
        guard let data = try? Data(contentsOf: libraryURL),
              let lib = try? JSONDecoder().decode(Library.self, from: data) else { return Library() }
        return lib
    }

    func save(_ lib: Library) {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(lib) else { return }
        try? data.write(to: libraryURL, options: .atomic)
    }
}
