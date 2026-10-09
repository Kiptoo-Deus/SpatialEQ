import AVFoundation
import Foundation
import UIKit

struct Track: Codable, Identifiable, Hashable {
    var id = UUID()
    var fileName: String
    var title: String
    var artist: String
    var duration: Double
}

/// The user's music: audio files copied into Documents/Music, plus anything dropped into the
/// app's Documents folder through Finder / the Files app.
final class Library: ObservableObject {
    @Published private(set) var tracks: [Track] = []
    private var artworkCache: [String: UIImage] = [:]

    static let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    static let musicFolder = documents.appendingPathComponent("Music", isDirectory: true)
    private static let indexURL = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("SpatialEQ/library.json")
    static let audioExtensions: Set<String> = ["mp3", "m4a", "aac", "wav", "aif", "aiff", "caf", "flac", "alac", "mp4"]

    init() {
        try? FileManager.default.createDirectory(at: Self.musicFolder, withIntermediateDirectories: true)
        try? FileManager.default.createDirectory(at: Self.indexURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        if let data = try? Data(contentsOf: Self.indexURL), let saved = try? JSONDecoder().decode([Track].self, from: data) {
            tracks = saved.filter { FileManager.default.fileExists(atPath: url(for: $0).path) }
        }
        Task { await scanForNewFiles() }
    }

    func url(for track: Track) -> URL { Self.musicFolder.appendingPathComponent(track.fileName) }

    /// Copies picked or shared files into the library.
    @MainActor
    func importFiles(_ urls: [URL]) async {
        for url in urls {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            let dest = uniqueDestination(for: url.lastPathComponent)
            do {
                try FileManager.default.copyItem(at: url, to: dest)
                await add(dest)
            } catch {
                continue
            }
        }
        save()
    }

    /// Moves audio files the user dropped into Documents (Finder file sharing) into the library.
    @MainActor
    func scanForNewFiles() async {
        let fm = FileManager.default
        let loose = (try? fm.contentsOfDirectory(at: Self.documents, includingPropertiesForKeys: nil)) ?? []
        for file in loose where Self.audioExtensions.contains(file.pathExtension.lowercased()) {
            let dest = uniqueDestination(for: file.lastPathComponent)
            if (try? fm.moveItem(at: file, to: dest)) != nil { await add(dest) }
        }
        let known = Set(tracks.map(\.fileName))
        let inFolder = (try? fm.contentsOfDirectory(at: Self.musicFolder, includingPropertiesForKeys: nil)) ?? []
        for file in inFolder where !known.contains(file.lastPathComponent)
            && Self.audioExtensions.contains(file.pathExtension.lowercased()) {
            await add(file)
        }
        save()
    }

    func delete(_ track: Track) {
        try? FileManager.default.removeItem(at: url(for: track))
        tracks.removeAll { $0.id == track.id }
        save()
    }

    func move(from source: IndexSet, to destination: Int) {
        tracks.move(fromOffsets: source, toOffset: destination)
        save()
    }

    func artwork(for track: Track) async -> UIImage? {
        if let cached = artworkCache[track.fileName] { return cached }
        let asset = AVURLAsset(url: url(for: track))
        guard let items = try? await asset.load(.commonMetadata) else { return nil }
        for item in items where item.commonKey == .commonKeyArtwork {
            if let data = try? await item.load(.dataValue), let image = UIImage(data: data) {
                await MainActor.run { artworkCache[track.fileName] = image }
                return image
            }
        }
        return nil
    }

    // MARK: - Private

    @MainActor
    private func add(_ url: URL) async {
        let asset = AVURLAsset(url: url)
        var title = url.deletingPathExtension().lastPathComponent
        var artist = ""
        if let items = try? await asset.load(.commonMetadata) {
            for item in items {
                if item.commonKey == .commonKeyTitle, let v = try? await item.load(.stringValue) { title = v }
                if item.commonKey == .commonKeyArtist, let v = try? await item.load(.stringValue) { artist = v }
            }
        }
        let duration = (try? await asset.load(.duration)).map(CMTimeGetSeconds) ?? 0
        guard duration.isFinite, duration > 0 else {
            try? FileManager.default.removeItem(at: url) // not playable audio
            return
        }
        tracks.append(Track(fileName: url.lastPathComponent, title: title, artist: artist, duration: duration))
    }

    private func uniqueDestination(for name: String) -> URL {
        var dest = Self.musicFolder.appendingPathComponent(name)
        let base = dest.deletingPathExtension().lastPathComponent, ext = dest.pathExtension
        var n = 2
        while FileManager.default.fileExists(atPath: dest.path) {
            dest = Self.musicFolder.appendingPathComponent("\(base) \(n).\(ext)")
            n += 1
        }
        return dest
    }

    private func save() {
        if let data = try? JSONEncoder().encode(tracks) { try? data.write(to: Self.indexURL, options: .atomic) }
    }
}
