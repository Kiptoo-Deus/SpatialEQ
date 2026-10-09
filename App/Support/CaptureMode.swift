import AppKit
import AVFoundation
import SceneKit

/// Produces README media from inside the app (no screen-recording permission needed):
///
///     SpatialEQ.app/Contents/MacOS/SpatialEQ --capture <output-dir>
///
/// 1. waits for silence and saves `idle.png`, then writes `<dir>/ready` so a script can start music,
/// 2. waits until audio is detected and saves `playing.png`,
/// 3. records `orbit.mp4`: the camera orbiting the scene 360°,
/// 4. quits. Settings are not persisted while capturing.
final class CaptureMode {
    static var outputDirectory: URL? {
        let args = ProcessInfo.processInfo.arguments
        guard let i = args.firstIndex(of: "--capture"), i + 1 < args.count else { return nil }
        return URL(fileURLWithPath: args[i + 1], isDirectory: true)
    }

    private let dir: URL
    private let state: AppState
    private let videoSize = CGSize(width: 1920, height: 1200)
    private let fps = 30
    private let orbitSeconds = 14.0

    init(dir: URL, state: AppState) {
        self.dir = dir
        self.state = state
    }

    func run() {
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        state.persistenceEnabled = false
        if let cinema = state.builtInPresets.first(where: { $0.name == "Cinema" }) { state.apply(cinema) }
        state.selectedBandID = nil

        after(2) {
            guard let window = self.mainWindow() else { return self.fail("main window not found") }
            window.ignoresMouseEvents = true // stray clicks must not edit settings mid-recording
            window.setContentSize(NSSize(width: 1440, height: 900))
            window.center()
            self.waitFor(timeout: 20, { self.state.analyzer.snapshot().level < 0.05 }) {
                self.after(1.5) {
                    self.save(self.capture(), "idle.png")
                    FileManager.default.createFile(atPath: self.dir.appendingPathComponent("ready").path, contents: nil)
                    self.waitFor(timeout: 60, { self.state.analyzer.snapshot().level > 0.3 }) {
                        self.after(4) {
                            self.save(self.capture(), "playing.png")
                            self.recordOrbit { NSApp.terminate(nil) }
                        }
                    }
                }
            }
        }
    }

    // MARK: - Capture

    private func mainWindow() -> NSWindow? {
        NSApp.windows.first { $0.contentView.flatMap(findScene) != nil }
    }

    private func findScene(_ view: NSView) -> OrbitSceneView? {
        if let v = view as? OrbitSceneView { return v }
        for sub in view.subviews { if let v = findScene(sub) { return v } }
        return nil
    }

    /// Window content as an image, with the Metal-rendered 3D view composited in.
    private func capture() -> CGImage? {
        guard let window = mainWindow(), let content = window.contentView, let scene = findScene(content) else { return nil }
        guard let rep = content.bitmapImageRepForCachingDisplay(in: content.bounds) else { return nil }
        content.cacheDisplay(in: content.bounds, to: rep)

        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
        let sceneRect = scene.convert(scene.bounds, to: nil) // window coords, bottom-left origin = content coords
        NSBezierPath(roundedRect: sceneRect, xRadius: 12, yRadius: 12).addClip()
        scene.snapshot().draw(in: sceneRect)
        NSGraphicsContext.restoreGraphicsState()
        return rep.cgImage
    }

    private func save(_ image: CGImage?, _ name: String) {
        guard let image else { return fail("capture failed for \(name)") }
        let rep = NSBitmapImageRep(cgImage: image)
        try? rep.representation(using: .png, properties: [:])?.write(to: dir.appendingPathComponent(name))
    }

    private func recordOrbit(done: @escaping () -> Void) {
        let url = dir.appendingPathComponent("orbit.mp4")
        try? FileManager.default.removeItem(at: url)
        guard let writer = try? AVAssetWriter(outputURL: url, fileType: .mp4),
              let window = mainWindow(), let controller = findScene(window.contentView!)?.controller else {
            return fail("could not start video writer")
        }
        let input = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: videoSize.width,
            AVVideoHeightKey: videoSize.height,
            AVVideoCompressionPropertiesKey: [AVVideoAverageBitRateKey: 12_000_000],
        ])
        input.expectsMediaDataInRealTime = false
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: videoSize.width,
            kCVPixelBufferHeightKey as String: videoSize.height,
        ])
        writer.add(input)
        writer.startWriting()
        writer.startSession(atSourceTime: .zero)

        let total = Int(orbitSeconds * Double(fps))
        let startYaw = controller.rig.eulerAngles.y
        var frame = 0
        Timer.scheduledTimer(withTimeInterval: 1.0 / Double(fps), repeats: true) { timer in
            guard frame < total else {
                timer.invalidate()
                controller.rig.eulerAngles.y = startYaw
                input.markAsFinished()
                writer.finishWriting { DispatchQueue.main.async(execute: done) }
                return
            }
            let p = Double(frame) / Double(total)
            controller.rig.eulerAngles.y = startYaw + CGFloat(p * 2 * .pi)
            controller.rig.eulerAngles.x = CGFloat(-0.5 + 0.12 * sin(p * 2 * .pi))
            if input.isReadyForMoreMediaData, let image = self.capture(), let buffer = self.pixelBuffer(image, pool: adaptor.pixelBufferPool) {
                adaptor.append(buffer, withPresentationTime: CMTime(value: CMTimeValue(frame), timescale: CMTimeScale(self.fps)))
            }
            frame += 1
        }
    }

    private func pixelBuffer(_ image: CGImage, pool: CVPixelBufferPool?) -> CVPixelBuffer? {
        guard let pool else { return nil }
        var buffer: CVPixelBuffer?
        CVPixelBufferPoolCreatePixelBuffer(nil, pool, &buffer)
        guard let buffer else { return nil }
        CVPixelBufferLockBaseAddress(buffer, [])
        defer { CVPixelBufferUnlockBaseAddress(buffer, []) }
        guard let ctx = CGContext(data: CVPixelBufferGetBaseAddress(buffer), width: Int(videoSize.width), height: Int(videoSize.height),
                                  bitsPerComponent: 8, bytesPerRow: CVPixelBufferGetBytesPerRow(buffer),
                                  space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)
        else { return nil }
        ctx.interpolationQuality = .high
        ctx.draw(image, in: CGRect(origin: .zero, size: videoSize))
        return buffer
    }

    // MARK: - Helpers

    private func after(_ seconds: Double, _ block: @escaping () -> Void) {
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: block)
    }

    private func waitFor(timeout: Double, _ condition: @escaping () -> Bool, then: @escaping () -> Void) {
        let deadline = Date().addingTimeInterval(timeout)
        Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { t in
            if condition() || Date() > deadline {
                t.invalidate()
                then()
            }
        }
    }

    private func fail(_ message: String) {
        try? message.write(to: dir.appendingPathComponent("error.txt"), atomically: true, encoding: .utf8)
        NSApp.terminate(nil)
    }
}
