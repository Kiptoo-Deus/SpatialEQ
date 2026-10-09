import SwiftUI

struct ContentView: View {
    @EnvironmentObject var state: PlayerState
    @State private var sheet: Sheet?

    enum Sheet: String, Identifiable {
        case library, eq, effects, presets
        var id: String { rawValue }
    }

    var body: some View {
        VStack(spacing: 0) {
            header
            ZStack(alignment: .bottomLeading) {
                SceneView3D()
                SceneHint().padding(10)
            }
            .clipShape(RoundedRectangle(cornerRadius: 16))
            .padding(.horizontal, 12)
            MiniSpectrum()
                .frame(height: 70)
                .padding(.horizontal, 12)
                .padding(.top, 8)
            NowPlayingPanel(openLibrary: { sheet = .library })
                .padding(12)
            toolbar
        }
        .background(Theme.background.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .sheet(item: $sheet) { s in
            Group {
                switch s {
                case .library: LibraryView()
                case .eq: EQView()
                case .effects: EffectsView()
                case .presets: PresetsView()
                }
            }
            .environmentObject(state)
            .environmentObject(state.headTracker)
            .presentationDetents(s == .eq ? [.large] : [.medium, .large])
            .presentationBackground(Theme.panel)
        }
        .alert("SpatialEQ", isPresented: Binding(get: { state.lastError != nil }, set: { if !$0 { state.lastError = nil } })) {
            Button("OK") { state.lastError = nil }
        } message: {
            Text(state.lastError ?? "")
        }
    }

    private var header: some View {
        HStack(spacing: 12) {
            Button {
                state.enabled.toggle()
            } label: {
                Image(systemName: "power")
                    .font(.system(size: 15, weight: .bold))
                    .frame(width: 34, height: 34)
                    .background(state.enabled ? Theme.accent : Color.white.opacity(0.12), in: Circle())
                    .foregroundStyle(state.enabled ? .black : .white)
            }
            .accessibilityLabel(state.enabled ? "Bypass effects" : "Enable effects")
            VStack(alignment: .leading, spacing: 1) {
                Text("SpatialEQ").font(.headline)
                Text(state.routeName).font(.caption).foregroundStyle(Theme.textDim)
            }
            Spacer()
            Button {
                sheet = .presets
            } label: {
                Label(state.currentPresetName, systemImage: "slider.horizontal.3")
                    .font(.subheadline.weight(.medium))
                    .padding(.horizontal, 12).padding(.vertical, 7)
                    .background(Color.white.opacity(0.08), in: Capsule())
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
    }

    private var toolbar: some View {
        HStack {
            toolButton("Library", "music.note.list", .library)
            toolButton("EQ", "slider.vertical.3", .eq)
            toolButton("Effects", "dot.radiowaves.left.and.right", .effects)
            toolButton("Presets", "square.stack.3d.up", .presets)
        }
        .padding(.horizontal, 8)
        .padding(.bottom, 4)
    }

    private func toolButton(_ title: String, _ icon: String, _ s: Sheet) -> some View {
        Button { sheet = s } label: {
            VStack(spacing: 4) {
                Image(systemName: icon).font(.system(size: 20))
                Text(title).font(.caption2)
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, 6)
        }
        .foregroundStyle(.white.opacity(0.85))
    }
}

private struct SceneHint: View {
    @EnvironmentObject var state: PlayerState

    var body: some View {
        Text(text)
            .font(.caption2)
            .padding(.horizontal, 8).padding(.vertical, 5)
            .background(.black.opacity(0.4), in: Capsule())
            .foregroundStyle(Theme.textDim)
            .allowsHitTesting(false)
    }

    private var text: String {
        if state.settings.mode == .speakers { return "Speakers · drag a speaker to set the span" }
        if state.settings.spatialEnabled { return "\(state.settings.upmix.label) · drag sources around you" }
        return "Turn on Virtual Surround in Effects"
    }
}

/// Small always-moving spectrum strip between the scene and the transport.
private struct MiniSpectrum: View {
    @EnvironmentObject var state: PlayerState

    var body: some View {
        TimelineView(.animation) { timeline in
            let t = timeline.date.timeIntervalSinceReferenceDate
            Canvas { ctx, size in
                let bands = state.analyzer.snapshot().bands
                guard !bands.isEmpty else { return }
                let w = size.width / CGFloat(bands.count)
                for (i, v) in bands.enumerated() {
                    let wobble = 0.04 * (0.5 + 0.5 * sin(t * 2.6 + Double(i) * 0.35))
                    let h = max(2, size.height * CGFloat(min(1, Double(v) + wobble)))
                    let rect = CGRect(x: CGFloat(i) * w + 1, y: size.height - h, width: max(1, w - 2), height: h)
                    let mix = Double(i) / Double(bands.count)
                    ctx.fill(Path(roundedRect: rect, cornerRadius: 2),
                             with: .color(mix < 0.5 ? Theme.accent.opacity(0.85) : Theme.accent2.opacity(0.7 + 0.3 * mix)))
                }
            }
        }
    }
}

private struct NowPlayingPanel: View {
    @EnvironmentObject var state: PlayerState
    let openLibrary: () -> Void
    @State private var scrubbing: Double?
    @State private var artwork: UIImage?

    var body: some View {
        VStack(spacing: 10) {
            HStack(spacing: 12) {
                Group {
                    if let artwork {
                        Image(uiImage: artwork).resizable().scaledToFill()
                    } else {
                        Image(systemName: "music.note").font(.title2).foregroundStyle(Theme.textDim)
                    }
                }
                .frame(width: 52, height: 52)
                .background(Color.white.opacity(0.06))
                .clipShape(RoundedRectangle(cornerRadius: 8))
                VStack(alignment: .leading, spacing: 2) {
                    Text(state.currentTrack?.title ?? "Nothing playing").font(.headline).lineLimit(1)
                    Text(state.currentTrack?.artist.isEmpty == false ? state.currentTrack!.artist : (state.currentTrack == nil ? "Add music in Library" : " "))
                        .font(.subheadline).foregroundStyle(Theme.textDim).lineLimit(1)
                }
                Spacer()
            }
            .contentShape(Rectangle())
            .onTapGesture { if state.currentTrack == nil { openLibrary() } }

            VStack(spacing: 2) {
                Slider(value: Binding(get: { scrubbing ?? state.position },
                                      set: { scrubbing = $0 }),
                       in: 0...max(state.duration, 0.1)) { editing in
                    if !editing, let s = scrubbing {
                        state.seek(to: s)
                        scrubbing = nil
                    }
                }
                .tint(Theme.accent)
                .disabled(state.currentTrack == nil)
                HStack {
                    Text(Theme.formatTime(scrubbing ?? state.position))
                    Spacer()
                    Text("-" + Theme.formatTime(state.duration - (scrubbing ?? state.position)))
                }
                .font(.caption2.monospacedDigit()).foregroundStyle(Theme.textDim)
            }

            HStack(spacing: 34) {
                Button { state.shuffle.toggle() } label: {
                    Image(systemName: "shuffle").foregroundStyle(state.shuffle ? Theme.accent : Theme.textDim)
                }
                Button { state.previous() } label: { Image(systemName: "backward.fill").font(.title2) }
                Button { state.togglePlay() } label: {
                    Image(systemName: state.isPlaying ? "pause.circle.fill" : "play.circle.fill").font(.system(size: 54))
                }
                Button { state.next() } label: { Image(systemName: "forward.fill").font(.title2) }
                Button { state.repeatAll.toggle() } label: {
                    Image(systemName: "repeat").foregroundStyle(state.repeatAll ? Theme.accent : Theme.textDim)
                }
            }
            .foregroundStyle(.white)
        }
        .padding(14)
        .background(Theme.panel, in: RoundedRectangle(cornerRadius: 16))
        .task(id: state.currentTrack) {
            artwork = nil
            if let t = state.currentTrack { artwork = await state.library.artwork(for: t) }
        }
    }
}
