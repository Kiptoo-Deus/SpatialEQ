import Foundation

/// Parses AutoEq / Equalizer APO "ParametricEQ.txt" files:
///
///     Preamp: -6.2 dB
///     Filter 1: ON LSC Fc 105 Hz Gain 6.9 dB Q 0.70
///     Filter 2: ON PK Fc 2400 Hz Gain -3.1 dB Q 1.41
enum AutoEqImporter {
    struct Result {
        var preampDb: Double
        var bands: [EQBand]
        var skipped: Int
    }

    enum ImportError: LocalizedError {
        case noFilters
        var errorDescription: String? { "No parametric filters were found in this file." }
    }

    static func parse(_ text: String) throws -> Result {
        var preamp = 0.0
        var bands: [EQBand] = []
        var skipped = 0

        for raw in text.components(separatedBy: .newlines) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.lowercased().hasPrefix("preamp:") {
                preamp = number(after: "preamp:", in: line) ?? 0
                continue
            }
            guard line.lowercased().hasPrefix("filter") else { continue }
            let tokens = line.split(whereSeparator: { $0 == " " || $0 == "\t" }).map(String.init)
            let upper = tokens.map { $0.uppercased() }
            guard let typeToken = upper.first(where: { typeMap[$0] != nil }), let type = typeMap[typeToken] else {
                skipped += 1
                continue
            }
            let on = !upper.contains("OFF")
            guard let fc = value(after: "FC", in: tokens) else { skipped += 1; continue }
            let gain = value(after: "GAIN", in: tokens) ?? 0
            let q = value(after: "Q", in: tokens) ?? (type == .peak ? 1.0 : 0.71)
            if bands.count < SoundSettings.maxBands {
                bands.append(EQBand(enabled: on, type: type, frequency: fc, gainDb: gain, q: q))
            } else {
                skipped += 1
            }
        }
        guard !bands.isEmpty else { throw ImportError.noFilters }
        return Result(preampDb: preamp, bands: bands, skipped: skipped)
    }

    /// Exports the current EQ in the same format so it can be used in other tools.
    static func export(_ s: SoundSettings) -> String {
        var lines = [String(format: "Preamp: %.1f dB", s.preampDb)]
        for (i, b) in s.bands.enumerated() {
            let code: String = switch b.type {
            case .peak: "PK"
            case .lowShelf: "LSC"
            case .highShelf: "HSC"
            case .lowPass: "LPQ"
            case .highPass: "HPQ"
            }
            lines.append(String(format: "Filter %d: %@ %@ Fc %.0f Hz Gain %.1f dB Q %.2f",
                                i + 1, b.enabled ? "ON" : "OFF", code, b.frequency, b.gainDb, b.q))
        }
        return lines.joined(separator: "\n") + "\n"
    }

    private static let typeMap: [String: BandType] = [
        "PK": .peak, "PEQ": .peak,
        "LS": .lowShelf, "LSC": .lowShelf, "LSQ": .lowShelf,
        "HS": .highShelf, "HSC": .highShelf, "HSQ": .highShelf,
        "LP": .lowPass, "LPQ": .lowPass,
        "HP": .highPass, "HPQ": .highPass,
    ]

    private static func value(after key: String, in tokens: [String]) -> Double? {
        guard let i = tokens.firstIndex(where: { $0.uppercased() == key }), i + 1 < tokens.count else { return nil }
        return Double(tokens[i + 1])
    }

    private static func number(after prefix: String, in line: String) -> Double? {
        let rest = line.dropFirst(prefix.count).trimmingCharacters(in: .whitespaces)
        return Double(rest.split(separator: " ").first ?? "")
    }
}
