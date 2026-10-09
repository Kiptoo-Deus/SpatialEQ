import SwiftUI
import UniformTypeIdentifiers

/// Touch parametric EQ: drag handles on the graph, fine-tune the selected band below.
struct EQView: View {
    @EnvironmentObject var state: PlayerState
    @State private var importing = false

    private let minF = 20.0, maxF = 20000.0, rangeDb = 18.0

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 14) {
                    graph
                        .frame(height: 240)
                        .background(Color.black.opacity(0.25), in: RoundedRectangle(cornerRadius: 12))

                    if let i = selectedIndex { inspector(i) } else {
                        Text("Drag a numbered handle to shape the sound. Tap one to edit it precisely.")
                            .font(.footnote).foregroundStyle(.secondary).frame(maxWidth: .infinity, alignment: .leading)
                    }

                    VStack(spacing: 10) {
                        Toggle("Equalizer", isOn: $state.settings.eqEnabled)
                        ParamSlider(title: "Preamp", value: $state.settings.preampDb, range: -24...6) { String(format: "%+.1f dB", $0) }
                    }
                    .padding()
                    .background(Color.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 12))
                }
                .padding()
            }
            .navigationTitle("Equalizer")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItemGroup(placement: .topBarTrailing) {
                    Button { state.addBand() } label: { Image(systemName: "plus") }
                        .disabled(state.settings.bands.count >= SoundSettings.maxBands)
                    Menu {
                        Button("Import AutoEq profile…") { importing = true }
                        Button("Reset to 10-band flat") {
                            state.settings.bands = SoundSettings.tenBand()
                            state.settings.preampDb = 0
                            state.selectedBandID = nil
                        }
                    } label: { Image(systemName: "ellipsis.circle") }
                }
            }
            .fileImporter(isPresented: $importing, allowedContentTypes: [.plainText, .text]) { result in
                if case let .success(url) = result { state.importAutoEq(from: url) }
            }
        }
    }

    private var selectedIndex: Int? { state.settings.bands.firstIndex { $0.id == state.selectedBandID } }

    // MARK: Graph

    private func x(_ f: Double, _ w: CGFloat) -> CGFloat { CGFloat(log(f / minF) / log(maxF / minF)) * w }
    private func freq(_ x: CGFloat, _ w: CGFloat) -> Double { minF * pow(maxF / minF, Double(max(0, min(1, x / w)))) }
    private func y(_ db: Double, _ h: CGFloat) -> CGFloat { h / 2 - CGFloat(db / rangeDb) * (h / 2 - 12) }
    private func db(_ y: CGFloat, _ h: CGFloat) -> Double { Double((h / 2 - y) / (h / 2 - 12)) * rangeDb }

    private var graph: some View {
        GeometryReader { geo in
            ZStack {
                TimelineView(.animation) { timeline in
                    let t = timeline.date.timeIntervalSinceReferenceDate
                    Canvas { ctx, size in
                        drawGrid(ctx, size)
                        drawSpectrum(ctx, size, t)
                        drawCurve(ctx, size)
                    }
                }
                ForEach(Array(state.settings.bands.enumerated()), id: \.element.id) { index, band in
                    handle(index, band, geo.size)
                }
            }
        }
    }

    private func drawGrid(_ ctx: GraphicsContext, _ size: CGSize) {
        var grid = Path()
        for f in [50.0, 100, 200, 500, 1000, 2000, 5000, 10000] {
            let px = x(f, size.width)
            grid.move(to: CGPoint(x: px, y: 0)); grid.addLine(to: CGPoint(x: px, y: size.height))
            ctx.draw(Text(Theme.formatHz(f)).font(.system(size: 9)).foregroundColor(.secondary),
                     at: CGPoint(x: px + 2, y: size.height - 7), anchor: .leading)
        }
        for d in stride(from: -12.0, through: 12, by: 6) {
            let py = y(d, size.height)
            grid.move(to: CGPoint(x: 0, y: py)); grid.addLine(to: CGPoint(x: size.width, y: py))
        }
        ctx.stroke(grid, with: .color(.white.opacity(0.07)), lineWidth: 1)
    }

    private func drawSpectrum(_ ctx: GraphicsContext, _ size: CGSize, _ t: Double) {
        let bands = state.analyzer.snapshot().bands
        guard !bands.isEmpty else { return }
        var path = Path()
        path.move(to: CGPoint(x: 0, y: size.height))
        for (i, v) in bands.enumerated() {
            let ripple = 0.05 * (0.5 + 0.5 * sin(t * 2.4 + Double(i) * 0.32))
            let level = min(1, 0.03 + Double(v) + ripple * (0.4 + Double(v)))
            path.addLine(to: CGPoint(x: x(Double(Analyzer.frequency(ofBand: i)), size.width),
                                     y: size.height * (1 - CGFloat(level) * 0.9)))
        }
        path.addLine(to: CGPoint(x: size.width, y: size.height))
        path.closeSubpath()
        ctx.fill(path, with: .linearGradient(Gradient(colors: [Theme.accent.opacity(0.4), Theme.accent.opacity(0.03)]),
                                             startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
    }

    private func drawCurve(_ ctx: GraphicsContext, _ size: CGSize) {
        let n = 160
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

    private func handle(_ index: Int, _ band: EQBand, _ size: CGSize) -> some View {
        let selected = band.id == state.selectedBandID
        return ZStack {
            Circle().fill(band.enabled ? Theme.accent2 : .gray).frame(width: 22, height: 22)
            Circle().stroke(.white, lineWidth: selected ? 2 : 0).frame(width: 30, height: 30)
            Text("\(index + 1)").font(.system(size: 10, weight: .bold)).foregroundStyle(.black)
        }
        .frame(width: 44, height: 44) // comfortable touch target
        .contentShape(Circle())
        .position(x: x(band.frequency, size.width), y: y(band.type.hasGain ? band.gainDb : 0, size.height))
        .gesture(DragGesture(minimumDistance: 0).onChanged { g in
            state.selectedBandID = band.id
            guard let i = state.settings.bands.firstIndex(where: { $0.id == band.id }) else { return }
            state.settings.bands[i].frequency = (freq(g.location.x, size.width) * 10).rounded() / 10
            if band.type.hasGain {
                state.settings.bands[i].gainDb = (max(-rangeDb, min(rangeDb, db(g.location.y, size.height))) * 10).rounded() / 10
            }
        })
    }

    // MARK: Inspector

    private func inspector(_ i: Int) -> some View {
        let band = $state.settings.bands[i]
        return VStack(spacing: 10) {
            HStack {
                Text("Band \(i + 1)").font(.headline)
                Spacer()
                Toggle("", isOn: band.enabled).labelsHidden()
                Button(role: .destructive) { state.removeSelectedBand() } label: { Image(systemName: "trash") }
            }
            Picker("Type", selection: band.type) {
                ForEach(BandType.allCases) { Text($0.label).tag($0) }
            }
            .pickerStyle(.segmented)
            ParamSlider(title: "Frequency", value: Binding(
                get: { log10(band.wrappedValue.frequency) },
                set: { band.wrappedValue.frequency = (pow(10, $0) * 10).rounded() / 10 }),
                range: log10(20)...log10(20000)) { Theme.formatHz(pow(10, $0)) + " Hz" }
            if band.wrappedValue.type.hasGain {
                ParamSlider(title: "Gain", value: band.gainDb, range: -18...18) { String(format: "%+.1f dB", $0) }
            }
            ParamSlider(title: "Q", value: band.q, range: 0.1...10) { String(format: "%.2f", $0) }
        }
        .padding()
        .background(Color.white.opacity(0.04), in: RoundedRectangle(cornerRadius: 12))
    }
}
