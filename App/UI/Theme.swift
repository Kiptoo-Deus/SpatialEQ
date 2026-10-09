import SwiftUI

enum Theme {
    static let background = Color(red: 0.035, green: 0.04, blue: 0.06)
    static let panel = Color(red: 0.07, green: 0.08, blue: 0.11)
    static let panelBorder = Color.white.opacity(0.07)
    static let accent = Color(red: 0.36, green: 0.85, blue: 0.95)
    static let accent2 = Color(red: 0.98, green: 0.62, blue: 0.27)
    static let textDim = Color.white.opacity(0.55)

    /// One colour per virtual channel: L R C Ls Rs Lb Rb.
    static let channelColors: [NSColor] = [
        NSColor(calibratedRed: 0.30, green: 0.70, blue: 1.00, alpha: 1),
        NSColor(calibratedRed: 1.00, green: 0.40, blue: 0.45, alpha: 1),
        NSColor(calibratedRed: 0.95, green: 0.95, blue: 0.95, alpha: 1),
        NSColor(calibratedRed: 0.55, green: 0.45, blue: 1.00, alpha: 1),
        NSColor(calibratedRed: 1.00, green: 0.55, blue: 0.85, alpha: 1),
        NSColor(calibratedRed: 0.35, green: 0.95, blue: 0.65, alpha: 1),
        NSColor(calibratedRed: 1.00, green: 0.80, blue: 0.30, alpha: 1),
        NSColor(calibratedRed: 0.70, green: 0.70, blue: 0.70, alpha: 1),
    ]

    static func formatHz(_ f: Double) -> String {
        f >= 1000 ? String(format: f >= 10000 ? "%.0fk" : "%.1fk", f / 1000) : String(format: "%.0f", f)
    }
}

struct PanelBackground: ViewModifier {
    func body(content: Content) -> some View {
        content
            .background(Theme.panel, in: RoundedRectangle(cornerRadius: 12))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(Theme.panelBorder))
    }
}

extension View {
    func panel() -> some View { modifier(PanelBackground()) }
}

/// Labelled slider row used throughout the controls.
struct ParamSlider: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var format: (Double) -> String = { String(format: "%.1f", $0) }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).font(.caption).foregroundStyle(Theme.textDim)
                Spacer()
                Text(format(value)).font(.caption.monospacedDigit()).foregroundStyle(.white.opacity(0.85))
            }
            Slider(value: $value, in: range).controlSize(.small)
        }
    }
}

struct LevelMeter: View {
    let level: Float // linear peak

    var body: some View {
        GeometryReader { geo in
            let db = 20 * log10(max(level, 1e-5))
            let frac = CGFloat(max(0, min(1, (db + 60) / 60)))
            ZStack(alignment: .leading) {
                Capsule().fill(Color.white.opacity(0.08))
                Capsule()
                    .fill(LinearGradient(colors: [Theme.accent, .green, .yellow, .red],
                                         startPoint: .leading, endPoint: .trailing))
                    .mask(alignment: .leading) { Capsule().frame(width: geo.size.width * frac) }
            }
        }
        .frame(height: 5)
    }
}
