import SwiftUI
import UniformTypeIdentifiers

struct LibraryView: View {
    @EnvironmentObject var state: PlayerState
    @Environment(\.dismiss) private var dismiss
    @State private var importing = false

    var body: some View {
        NavigationStack {
            Group {
                if state.library.tracks.isEmpty {
                    ContentUnavailableView {
                        Label("No music yet", systemImage: "music.note.list")
                    } description: {
                        Text("Import audio files (MP3, AAC, ALAC, WAV, AIFF, FLAC) from Files, or copy them to SpatialEQ in Finder on your Mac.")
                    } actions: {
                        Button("Import Music") { importing = true }.buttonStyle(.borderedProminent)
                    }
                } else {
                    List {
                        ForEach(state.library.tracks) { track in
                            Button {
                                state.play(track)
                                dismiss()
                            } label: {
                                HStack {
                                    VStack(alignment: .leading, spacing: 2) {
                                        Text(track.title).foregroundStyle(track == state.currentTrack ? Theme.accent : .primary)
                                        if !track.artist.isEmpty {
                                            Text(track.artist).font(.caption).foregroundStyle(.secondary)
                                        }
                                    }
                                    Spacer()
                                    if track == state.currentTrack && state.isPlaying {
                                        Image(systemName: "waveform").foregroundStyle(Theme.accent).symbolEffect(.variableColor.iterative)
                                    }
                                    Text(Theme.formatTime(track.duration)).font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                                }
                            }
                        }
                        .onDelete { idx in idx.map { state.library.tracks[$0] }.forEach(state.delete) }
                        .onMove { state.library.move(from: $0, to: $1) }
                        .listRowBackground(Color.white.opacity(0.04))
                    }
                    .scrollContentBackground(.hidden)
                }
            }
            .navigationTitle("Library")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) { EditButton().disabled(state.library.tracks.isEmpty) }
                ToolbarItem(placement: .topBarTrailing) {
                    Button { importing = true } label: { Image(systemName: "plus") }
                }
            }
            .fileImporter(isPresented: $importing, allowedContentTypes: [.audio], allowsMultipleSelection: true) { result in
                if case let .success(urls) = result {
                    Task { await state.library.importFiles(urls) }
                }
            }
            .refreshable { await state.library.scanForNewFiles() }
        }
    }
}
