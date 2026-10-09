import SwiftUI

enum Theme {
    static let background = Color(red: 0.035, green: 0.04, blue: 0.06)
    static let panel = Color(red: 0.07, green: 0.08, blue: 0.11)
    static let accent = Color(red: 0.36, green: 0.85, blue: 0.95)
    static let accent2 = Color(red: 0.98, green: 0.62, blue: 0.27)
    static let textDim = Color.white.opacity(0.55)

    static func formatHz(_ f: Double) -> String {
        f >= 1000 ? String(format: f >= 10000 ? "%.0fk" : "%.1fk", f / 1000) : String(format: "%.0f", f)
    }

    static func formatTime(_ s: Double) -> String {
        guard s.isFinite else { return "0:00" }
        let t = Int(s.rounded(.down))
        return String(format: "%d:%02d", t / 60, t % 60)
    }
}

struct ParamSlider: View {
    let title: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    var format: (Double) -> String = { String(format: "%.1f", $0) }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title)
                Spacer()
                Text(format(value)).monospacedDigit().foregroundStyle(.secondary)
            }
            .font(.subheadline)
            Slider(value: $value, in: range)
        }
    }
}
