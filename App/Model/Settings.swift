import Foundation

enum BandType: String, Codable, CaseIterable, Identifiable {
    case peak, lowShelf, highShelf, lowPass, highPass

    var id: String { rawValue }
    var label: String {
        switch self {
        case .peak: "Peak"
        case .lowShelf: "Low Shelf"
        case .highShelf: "High Shelf"
        case .lowPass: "Low Pass"
        case .highPass: "High Pass"
        }
    }
    var hasGain: Bool { self == .peak || self == .lowShelf || self == .highShelf }
    fileprivate var cValue: Int32 {
        switch self {
        case .peak: 0
        case .lowShelf: 1
        case .highShelf: 2
        case .lowPass: 3
        case .highPass: 4
        }
    }
}

struct EQBand: Codable, Hashable, Identifiable {
    var id = UUID()
    var enabled = true
    var type: BandType = .peak
    var frequency: Double
    var gainDb: Double = 0
    var q: Double = 1.0
}

enum OutputMode: String, Codable, CaseIterable, Identifiable {
    case headphones, speakers
    var id: String { rawValue }
    var label: String { self == .headphones ? "Headphones" : "Speakers" }
}

enum Upmix: String, Codable, CaseIterable, Identifiable {
    case stereo, surround51, surround71
    var id: String { rawValue }
    var label: String {
        switch self {
        case .stereo: "Stereo"
        case .surround51: "Virtual 5.1"
        case .surround71: "Virtual 7.1"
        }
    }
    var channelNames: [String] {
        switch self {
        case .stereo: ["L", "R"]
        case .surround51: ["L", "R", "C", "Ls", "Rs"]
        case .surround71: ["L", "R", "C", "Ls", "Rs", "Lb", "Rb"]
        }
    }
    fileprivate var cValue: Int32 {
        switch self {
        case .stereo: 0
        case .surround51: 1
        case .surround71: 2
        }
    }
}

struct VirtualSource: Codable, Hashable {
    var azimuth: Double      // degrees, 0 front, + right
    var elevation: Double = 0
    var distance: Double = 1.5
    var gain: Double = 1
}

struct SoundSettings: Codable, Equatable {
    static let maxBands = Int(SQ_MAX_BANDS)
    static let sourceCount = Int(SQ_MAX_SOURCES)
    static let defaultAzimuths: [Double] = [-30, 30, 0, -110, 110, -150, 150, 0]

    var preampDb: Double = 0
    var eqEnabled = true
    var bands: [EQBand] = SoundSettings.tenBand()

    var mode: OutputMode = .headphones
    var spatialEnabled = false
    var upmix: Upmix = .stereo
    var sources: [VirtualSource] = SoundSettings.defaultAzimuths.map { VirtualSource(azimuth: $0) }

    var width: Double = 1
    var crosstalkEnabled = false
    var crosstalkStrength: Double = 0.6
    var speakerSpan: Double = 30

    var roomSize: Double = 0.35
    var reverbMix: Double = 0

    var bassBoostDb: Double = 0
    var dialogueBoost: Double = 0
    var levelerEnabled = false
    var levelerTargetDb: Double = -20
    var limiterEnabled = true
    var limiterCeilingDb: Double = -1
    var outputGainDb: Double = 0

    static func tenBand() -> [EQBand] {
        let freqs: [Double] = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
        return freqs.enumerated().map { i, f in
            EQBand(type: i == 0 ? .lowShelf : i == freqs.count - 1 ? .highShelf : .peak, frequency: f, q: i == 0 || i == 9 ? 0.7 : 1.0)
        }
    }

    mutating func resetSources() {
        sources = Self.defaultAzimuths.map { VirtualSource(azimuth: $0) }
    }

    /// Converts to the C parameter block consumed by the engine.
    func cParams(enabled: Bool) -> sq_params {
        var p = sq_params()
        sq_params_default(&p)
        p.enabled = enabled ? 1 : 0
        p.preampDb = Float(preampDb)
        p.eqEnabled = eqEnabled ? 1 : 0
        let count = min(bands.count, Self.maxBands)
        p.numBands = Int32(count)
        withUnsafeMutableBytes(of: &p.bands) { raw in
            let dst = raw.bindMemory(to: sq_band.self)
            for i in 0..<count {
                let b = bands[i]
                dst[i] = sq_band(enabled: b.enabled ? 1 : 0, type: b.type.cValue, freq: Float(b.frequency),
                                 gainDb: Float(b.gainDb), q: Float(b.q))
            }
        }
        p.mode = mode == .headphones ? 0 : 1
        p.spatialEnabled = spatialEnabled ? 1 : 0
        p.upmix = upmix.cValue
        withUnsafeMutableBytes(of: &p.sources) { raw in
            let dst = raw.bindMemory(to: sq_source.self)
            for i in 0..<min(sources.count, Self.sourceCount) {
                let s = sources[i]
                dst[i] = sq_source(azimuthDeg: Float(s.azimuth), elevationDeg: Float(s.elevation),
                                   distance: Float(s.distance), gain: Float(s.gain))
            }
        }
        p.width = Float(width)
        p.xtcEnabled = crosstalkEnabled ? 1 : 0
        p.xtcStrength = Float(crosstalkStrength)
        p.speakerSpanDeg = Float(speakerSpan)
        p.roomSize = Float(roomSize)
        p.reverbMix = Float(reverbMix)
        p.bassBoostDb = Float(bassBoostDb)
        p.dialogueBoost = Float(dialogueBoost)
        p.levelerEnabled = levelerEnabled ? 1 : 0
        p.levelerTargetDb = Float(levelerTargetDb)
        p.limiterEnabled = limiterEnabled ? 1 : 0
        p.limiterCeilingDb = Float(limiterCeilingDb)
        p.outputGainDb = Float(outputGainDb)
        return p
    }
}

struct Preset: Codable, Identifiable, Hashable {
    var id = UUID()
    var name: String
    var settings: SoundSettings
    var isBuiltIn = false

    static func == (a: Preset, b: Preset) -> Bool { a.id == b.id }
    func hash(into h: inout Hasher) { h.combine(id) }
}

/// What the app remembers for each output device.
struct DeviceProfile: Codable {
    var presetID: UUID?
    var settings: SoundSettings
}
