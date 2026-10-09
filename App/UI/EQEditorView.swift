import SwiftUI

/// 2D parametric EQ editor: live spectrum, combined response curve and draggable band handles.
/// Drag a handle to set frequency/gain, scroll over it to change Q, double-click to toggle it.
struct EQEditorView: View {
    @EnvironmentObject var state: AppState

    private let minF = 20.0, maxF = 20000.0, rangeDb = 18.0

    var body: some View {
        VStack(spacing: 8) {
            header
            GeometryReader { geo in
                ZStack {
                    // Reading the timeline date makes the Canvas redraw on every display frame.
                    TimelineView(.animation) { timeline in
                        let t = timeline.date.timeIntervalSinceReferenceDate
                        Canvas { ctx, size in
                            drawGrid(ctx, size)
                            drawSpectrum(ctx, size, time: t)
                            drawCurve(ctx, size)
                        }
                    }
                    ForEach(Array(state.settings.bands.enumerated()), id: \.element.id) { index, band in
                        handle(index: index, band: band, size: geo.size)
                    }
                }
                .contentShape(Rectangle())
                .onTapGesture { state.selectedBandID = nil }
            }
            .frame(minHeight: 170)
            if let i = selectedIndex { bandInspector(i) }
        }
        .padding(12)
        .panel()
    }

    private var selectedIndex: Int? {
        state.settings.bands.firstIndex { $0.id == state.selectedBandID }
    }

    private var header: some View {
        HStack(spacing: 10) {
            Text("PARAMETRIC EQ").font(.caption.weight(.bold)).tracking(1.2).foregroundStyle(Theme.textDim)
            Toggle("", isOn: $state.settings.eqEnabled).toggleStyle(.switch).controlSize(.mini).labelsHidden()
            Spacer()
            HStack(spacing: 4) {
                Text("Preamp").font(.caption).foregroundStyle(Theme.textDim)
                Slider(value: $state.settings.preampDb, in: -24...6).frame(width: 110).controlSize(.small)
                Text(String(format: "%+.1f dB", state.settings.preampDb)).font(.caption.monospacedDigit()).frame(width: 56)
            }
            Button { state.addBand() } label: { Image(systemName: "plus") }
                .disabled(state.settings.bands.count >= SoundSettings.maxBands)
                .help("Add band")
            Button { state.removeSelectedBand() } label: { Image(systemName: "minus") }
                .disabled(state.selectedBandID == nil)
                .help("Remove selected band")
            Menu {
                Button("Import AutoEq / APO file…") { state.importAutoEq() }
                Button("Export as ParametricEQ.txt…") { state.exportEq() }
                Divider()
                Button("Reset to 10-band flat") {
                    state.settings.bands = SoundSettings.tenBand()
                    state.settings.preampDb = 0
                }
            } label: { Image(systemName: "ellipsis.circle") }
                .menuStyle(.borderlessButton).frame(width: 30)
        }
        .buttonStyle(.borderless)
    }

    // MARK: Mapping

    private func x(_ f: Double, _ w: CGFloat) -> CGFloat { CGFloat(log(f / minF) / log(maxF / minF)) * w }
    private func freq(_ x: CGFloat, _ w: CGFloat) -> Double { minF * pow(maxF / minF, Double(max(0, min(1, x / w)))) }
    private func y(_ db: Double, _ h: CGFloat) -> CGFloat { h / 2 - CGFloat(db / rangeDb) * (h / 2 - 8) }
    private func db(_ y: CGFloat, _ h: CGFloat) -> Double { Double((h / 2 - y) / (h / 2 - 8)) * rangeDb }

    // MARK: Drawing

    private func drawGrid(_ ctx: GraphicsContext, _ size: CGSize) {
        var grid = Path()
        for f in [30.0, 50, 100, 200, 500, 1000, 2000, 5000, 10000] {
            let px = x(f, size.width)
            grid.move(to: CGPoint(x: px, y: 0))
            grid.addLine(to: CGPoint(x: px, y: size.height))
            ctx.draw(Text(Theme.formatHz(f)).font(.system(size: 9)).foregroundColor(Theme.textDim),
                     at: CGPoint(x: px + 2, y: size.height - 6), anchor: .leading)
        }
        for d in stride(from: -12.0, through: 12, by: 6) {
            let py = y(d, size.height)
            grid.move(to: CGPoint(x: 0, y: py))
            grid.addLine(to: CGPoint(x: size.width, y: py))
            ctx.draw(Text(d == 0 ? "0" : String(format: "%+.0f", d)).font(.system(size: 9)).foregroundColor(Theme.textDim),
                     at: CGPoint(x: 4, y: py - 6), anchor: .leading)
        }
        ctx.stroke(grid, with: .color(.white.opacity(0.06)), lineWidth: 1)
    }

    /// Live spectrum drawn as flowing waves: a filled body plus two phase-shifted outlines.
    /// A small time-based ripple keeps it moving even when the audio is quiet or paused.
    private func drawSpectrum(_ ctx: GraphicsContext, _ size: CGSize, time t: Double) {
        let bands = state.analyzer.snapshot().bands
        guard !bands.isEmpty else { return }

        func wave(phase: Double, speed: Double, ripple: Double) -> [CGPoint] {
            bands.indices.map { i in
                let f = Double(Analyzer.frequency(ofBand: i))
                let live = Double(bands[i])
                let motion = ripple * (0.5 + 0.5 * sin(t * speed + Double(i) * 0.32 + phase))
                    + 0.5 * ripple * sin(t * speed * 0.53 - Double(i) * 0.17 + phase)
                let v = min(1, max(0, 0.03 + live + motion * (0.4 + live)))
                return CGPoint(x: x(f, size.width), y: size.height * (1 - CGFloat(v) * 0.9))
            }
        }

        // Smooth curve through the points (midpoint quadratic segments).
        func smoothPath(_ pts: [CGPoint], closed: Bool) -> Path {
            var path = Path()
            if closed { path.move(to: CGPoint(x: 0, y: size.height)); path.addLine(to: pts[0]) }
            else { path.move(to: pts[0]) }
            for i in 1..<pts.count {
                let mid = CGPoint(x: (pts[i - 1].x + pts[i].x) / 2, y: (pts[i - 1].y + pts[i].y) / 2)
                path.addQuadCurve(to: mid, control: pts[i - 1])
            }
            path.addLine(to: pts[pts.count - 1])
            if closed {
                path.addLine(to: CGPoint(x: size.width, y: size.height))
                path.closeSubpath()
            }
            return path
        }

        let body = wave(phase: 0, speed: 2.4, ripple: 0.05)
        ctx.fill(smoothPath(body, closed: true),
                 with: .linearGradient(Gradient(colors: [Theme.accent.opacity(0.4), Theme.accent.opacity(0.03)]),
                                       startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
        ctx.stroke(smoothPath(body, closed: false), with: .color(Theme.accent.opacity(0.9)), lineWidth: 1.5)
        ctx.stroke(smoothPath(wave(phase: 2.1, speed: 1.7, ripple: 0.08), closed: false),
                   with: .color(Theme.accent.opacity(0.35)), lineWidth: 1)
        ctx.stroke(smoothPath(wave(phase: 4.2, speed: 3.1, ripple: 0.06), closed: false),
                   with: .color(Theme.accent2.opacity(0.25)), lineWidth: 1)
    }

    private func drawCurve(_ ctx: GraphicsContext, _ size: CGSize) {
        let n = 220
        let freqs: [Float] = (0..<n).map { Float(minF * pow(maxF / minF, Double($0) / Double(n - 1))) }
        let response = DSPEngine.eqResponse(state.settings, sampleRate: 48000, frequencies: freqs)
        var path = Path()
        for i in 0..<n {
            let p = CGPoint(x: x(Double(freqs[i]), size.width), y: y(Double(response[i]), size.height))
            i == 0 ? path.move(to: p) : path.addLine(to: p)
        }
        let color = state.settings.eqEnabled ? Theme.accent2 : Color.gray
        ctx.stroke(path, with: .color(color.opacity(0.25)), lineWidth: 6)
        ctx.stroke(path, with: .color(color), lineWidth: 2)
    }

    // MARK: Handles

    private func handle(index: Int, band: EQBand, size: CGSize) -> some View {
        let selected = band.id == state.selectedBandID
        let pos = CGPoint(x: x(band.frequency, size.width), y: y(band.type.hasGain ? band.gainDb : 0, size.height))
        return ZStack {
            Circle().fill(band.enabled ? Theme.accent2 : Color.gray).frame(width: 14, height: 14)
            Circle().stroke(.white, lineWidth: selected ? 2 : 0).frame(width: 20, height: 20)
            Text("\(index + 1)").font(.system(size: 8, weight: .bold)).foregroundStyle(.black)
        }
        .position(pos)
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { g in
                    state.selectedBandID = band.id
                    guard let i = state.settings.bands.firstIndex(where: { $0.id == band.id }) else { return }
                    state.settings.bands[i].frequency = (freq(g.location.x, size.width) * 10).rounded() / 10
                    if band.type.hasGain {
                        state.settings.bands[i].gainDb = (max(-rangeDb, min(rangeDb, db(g.location.y, size.height))) * 10).rounded() / 10
                    }
                }
        )
        .simultaneousGesture(TapGesture(count: 2).onEnded {
            if let i = state.settings.bands.firstIndex(where: { $0.id == band.id }) {
                state.settings.bands[i].enabled.toggle()
            }
        })
        .background(ScrollCatcher { delta in
            guard let i = state.settings.bands.firstIndex(where: { $0.id == band.id }) else { return }
            state.settings.bands[i].q = max(0.1, min(20, state.settings.bands[i].q * (1 + delta * 0.02)))
        }.frame(width: 24, height: 24).position(pos))
    }

    private func bandInspector(_ i: Int) -> some View {
        let band = $state.settings.bands[i]
        return HStack(spacing: 14) {
            Text("Band \(i + 1)").font(.caption.weight(.semibold))
            Toggle("On", isOn: band.enabled).toggleStyle(.checkbox).controlSize(.small)
            Picker("", selection: band.type) {
                ForEach(BandType.allCases) { Text($0.label).tag($0) }
            }
            .labelsHidden().frame(width: 110).controlSize(.small)
            numberField("Freq", band.frequency, suffix: "Hz", range: 10...22000)
            numberField("Gain", band.gainDb, suffix: "dB", range: -24...24).disabled(!band.wrappedValue.type.hasGain)
            numberField("Q", band.q, suffix: "", range: 0.1...20)
            Spacer()
        }
        .font(.caption)
    }

    private func numberField(_ title: String, _ value: Binding<Double>, suffix: String, range: ClosedRange<Double>) -> some View {
        HStack(spacing: 4) {
            Text(title).foregroundStyle(Theme.textDim)
            TextField("", value: Binding(get: { value.wrappedValue },
                                         set: { value.wrappedValue = min(range.upperBound, max(range.lowerBound, $0)) }),
                      format: .number.precision(.fractionLength(0...2)))
                .textFieldStyle(.roundedBorder).frame(width: 64).controlSize(.small)
            if !suffix.isEmpty { Text(suffix).foregroundStyle(Theme.textDim) }
        }
    }
}

/// Forwards scroll-wheel deltas over a small area (used to adjust Q on a handle).
private struct ScrollCatcher: NSViewRepresentable {
    let onScroll: (Double) -> Void

    func makeNSView(context: Context) -> CatcherView {
        let v = CatcherView()
        v.onScroll = onScroll
        return v
    }

    func updateNSView(_ v: CatcherView, context: Context) { v.onScroll = onScroll }

    final class CatcherView: NSView {
        var onScroll: ((Double) -> Void)?
        override func scrollWheel(with event: NSEvent) { onScroll?(Double(event.scrollingDeltaY)) }
    }
}
