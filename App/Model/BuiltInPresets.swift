import Foundation

enum BuiltInPresets {
    // Stable IDs so device profiles keep pointing at the same built-in preset across launches.
    private static func id(_ n: Int) -> UUID { UUID(uuidString: String(format: "00000000-0000-0000-0000-%012d", n))! }

    static var all: [Preset] {
        [
            make(1, "Flat") { _ in },
            make(2, "Bass Boost") { s in
                s.bands[0].gainDb = 6; s.bands[1].gainDb = 4; s.bands[2].gainDb = 2
                s.bassBoostDb = 4; s.preampDb = -4
            },
            make(3, "Treble Lift") { s in
                s.bands[7].gainDb = 2; s.bands[8].gainDb = 4; s.bands[9].gainDb = 5; s.preampDb = -4
            },
            make(4, "Loudness") { s in
                s.bands[0].gainDb = 5; s.bands[1].gainDb = 3; s.bands[8].gainDb = 2; s.bands[9].gainDb = 4
                s.preampDb = -4
            },
            make(5, "Vocal & Dialogue") { s in
                s.bands[0].gainDb = -2; s.bands[6].gainDb = 2; s.bands[7].gainDb = 1.5
                s.dialogueBoost = 0.6; s.levelerEnabled = true
            },
            make(6, "Podcast") { s in
                s.bands[0].gainDb = -6; s.bands[1].gainDb = -3; s.bands[6].gainDb = 2
                s.dialogueBoost = 0.8; s.levelerEnabled = true; s.levelerTargetDb = -18
            },
            make(7, "Cinema") { s in
                s.spatialEnabled = true; s.upmix = .surround71
                s.roomSize = 0.55; s.reverbMix = 0.25
                s.bands[0].gainDb = 4; s.bassBoostDb = 3; s.dialogueBoost = 0.35; s.levelerEnabled = true
                s.preampDb = -3
            },
            make(8, "Music Hall") { s in
                s.spatialEnabled = true; s.upmix = .stereo
                s.sources[0].distance = 2.5; s.sources[1].distance = 2.5
                s.sources[0].azimuth = -40; s.sources[1].azimuth = 40
                s.roomSize = 0.85; s.reverbMix = 0.4
            },
            make(9, "Studio Speakers") { s in
                s.spatialEnabled = true; s.upmix = .stereo
                s.roomSize = 0.2; s.reverbMix = 0.12
            },
            make(10, "Gaming") { s in
                s.spatialEnabled = true; s.upmix = .surround51
                s.roomSize = 0.15; s.reverbMix = 0.05
                s.bands[7].gainDb = 2; s.bands[1].gainDb = 2
            },
            make(11, "Wide Speakers") { s in
                s.mode = .speakers; s.width = 1.5; s.crosstalkEnabled = true; s.crosstalkStrength = 0.6
            },
            make(12, "Laptop Speakers") { s in
                s.mode = .speakers; s.width = 1.4; s.crosstalkEnabled = true; s.speakerSpan = 20
                s.bassBoostDb = 6; s.bands[0].type = .highPass; s.bands[0].frequency = 80; s.bands[0].q = 0.7
                s.levelerEnabled = true; s.preampDb = -3
            },
        ]
    }

    private static func make(_ n: Int, _ name: String, _ edit: (inout SoundSettings) -> Void) -> Preset {
        var s = SoundSettings()
        edit(&s)
        return Preset(id: id(n), name: name, settings: s, isBuiltIn: true)
    }
}
