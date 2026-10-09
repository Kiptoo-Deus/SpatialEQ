import AVFoundation
import Foundation

/// Plays audio files through the SpatialEQ DSP engine.
///
///   decoder queue: AVAudioFile -> AVAudioConverter (device rate, stereo) -> sq_fifo
///   audio thread : AVAudioSourceNode pulls from sq_fifo -> sq_engine_process -> output
///
/// The audio thread never allocates or locks; it only touches the FIFO, the engine and one flag.
final class PlayerEngine {
    let dsp: DSPEngine

    /// Called on the main thread when the current file has played to the end.
    var onFinished: (() -> Void)?

    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode?
    private let fifo: OpaquePointer
    private let playingFlag: UnsafeMutablePointer<Int32>
    private let decodeQueue = DispatchQueue(label: "com.savannahdsp.spatialeq.decode", qos: .userInitiated)
    private var job: DecodeJob?
    private var endTimer: Timer?

    private(set) var sampleRate: Double = 48000
    private(set) var currentURL: URL?
    private(set) var duration: Double = 0
    /// Position (seconds) at which the current decode job started.
    private var baseTime: Double = 0

    init(dsp: DSPEngine) {
        self.dsp = dsp
        fifo = sq_fifo_create(1 << 17) // ~2.7 s at 48 kHz
        playingFlag = .allocate(capacity: 1)
        playingFlag.initialize(to: 0)
    }

    deinit {
        job?.cancel()
        engine.stop()
        sq_fifo_destroy(fifo)
        playingFlag.deallocate()
    }

    var isPlaying: Bool { playingFlag.pointee != 0 }

    var position: Double {
        baseTime + Double(sq_fifo_frames_consumed(fifo)) / sampleRate
    }

    // MARK: - Engine

    func start() throws {
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playback, mode: .default, options: [])
        try session.setActive(true)
        sampleRate = session.sampleRate > 0 ? session.sampleRate : 48000
        buildGraph()
        try engine.start()
    }

    /// Rebuilds the graph at the current hardware rate (after a route or configuration change).
    func restart() {
        let resumeAt = position
        let wasPlaying = isPlaying
        engine.stop()
        sampleRate = AVAudioSession.sharedInstance().sampleRate
        buildGraph()
        try? engine.start()
        if let url = currentURL { load(url, at: resumeAt, play: wasPlaying) }
    }

    private func buildGraph() {
        if let source { engine.detach(source) }
        dsp.prepare(sampleRate: sampleRate, maxFrames: 4096)

        let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
        let fifo = self.fifo, flag = self.playingFlag, handle = dsp.handle
        let node = AVAudioSourceNode(format: format) { _, _, frameCount, abl -> OSStatus in
            let buffers = UnsafeMutableAudioBufferListPointer(abl)
            guard buffers.count >= 2,
                  let l = buffers[0].mData?.assumingMemoryBound(to: Float.self),
                  let r = buffers[1].mData?.assumingMemoryBound(to: Float.self) else { return noErr }
            let n = Int(frameCount)
            // Reading 0 frames while paused still lets the FIFO service a pending flush.
            let got = Int(sq_fifo_read(fifo, l, r, flag.pointee != 0 ? Int32(n) : 0))
            if got < n {
                (l + got).update(repeating: 0, count: n - got)
                (r + got).update(repeating: 0, count: n - got)
            }
            sq_engine_process(handle, l, r, Int32(n))
            return noErr
        }
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        source = node
    }

    // MARK: - Transport

    func load(_ url: URL, at seconds: Double = 0, play: Bool) {
        playingFlag.pointee = 0
        job?.cancel()
        currentURL = url
        duration = (try? AVAudioFile(forReading: url)).map { Double($0.length) / $0.fileFormat.sampleRate } ?? 0
        baseTime = max(0, min(seconds, duration))

        let newJob = DecodeJob(url: url, startSeconds: baseTime, targetRate: sampleRate, fifo: fifo,
                               engineRunning: { [weak self] in self?.engine.isRunning ?? false })
        job = newJob
        decodeQueue.async { newJob.run() }
        if play { self.play() }
        watchForEnd()
    }

    func play() {
        guard currentURL != nil else { return }
        if !engine.isRunning { try? engine.start() }
        playingFlag.pointee = 1
    }

    func pause() {
        playingFlag.pointee = 0
    }

    func seek(to seconds: Double) {
        guard let url = currentURL else { return }
        load(url, at: seconds, play: isPlaying)
    }

    func stop() {
        playingFlag.pointee = 0
        job?.cancel()
        currentURL = nil
        endTimer?.invalidate()
    }

    private func watchForEnd() {
        endTimer?.invalidate()
        endTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            guard let self, let job = self.job else { return }
            if job.isFinished && sq_fifo_available_frames(self.fifo) == 0 && self.isPlaying {
                self.endTimer?.invalidate()
                self.playingFlag.pointee = 0
                self.onFinished?()
            }
        }
    }
}

/// Decodes one file into the FIFO on the decode queue until cancelled or finished.
private final class DecodeJob {
    let url: URL
    let startSeconds: Double
    let targetRate: Double
    let fifo: OpaquePointer
    let engineRunning: () -> Bool

    private let lock = NSLock()
    private var _cancelled = false
    private var _finished = false

    init(url: URL, startSeconds: Double, targetRate: Double, fifo: OpaquePointer, engineRunning: @escaping () -> Bool) {
        self.url = url
        self.startSeconds = startSeconds
        self.targetRate = targetRate
        self.fifo = fifo
        self.engineRunning = engineRunning
    }

    var isCancelled: Bool { lock.withLock { _cancelled } }
    var isFinished: Bool { lock.withLock { _finished } }
    func cancel() { lock.withLock { _cancelled = true } }

    func run() {
        // Any previous job has exited (serial queue); drop what it left in the FIFO.
        sq_fifo_request_flush(fifo)
        let deadline = Date().addingTimeInterval(0.3)
        while sq_fifo_flush_pending(fifo) != 0 {
            if Date() > deadline && !engineRunning() { sq_fifo_reset(fifo); break }
            if isCancelled { return }
            usleep(2000)
        }

        guard let file = try? AVAudioFile(forReading: url) else { return finish() }
        let inFormat = file.processingFormat
        let outChannels = AVAudioChannelCount(min(Int(inFormat.channelCount), 2))
        guard let outFormat = AVAudioFormat(standardFormatWithSampleRate: targetRate, channels: outChannels),
              let converter = AVAudioConverter(from: inFormat, to: outFormat) else { return finish() }
        converter.downmix = inFormat.channelCount > 2
        file.framePosition = AVAudioFramePosition(startSeconds * inFormat.sampleRate)

        let inChunk: AVAudioFrameCount = 4096
        let outChunk = AVAudioFrameCount(Double(inChunk) * targetRate / inFormat.sampleRate) + 64
        guard let inBuf = AVAudioPCMBuffer(pcmFormat: inFormat, frameCapacity: inChunk),
              let outBuf = AVAudioPCMBuffer(pcmFormat: outFormat, frameCapacity: outChunk) else { return finish() }

        var reachedEnd = false
        while !isCancelled {
            outBuf.frameLength = 0
            var error: NSError?
            let status = converter.convert(to: outBuf, error: &error) { _, inputStatus in
                if reachedEnd { inputStatus.pointee = .endOfStream; return nil }
                do {
                    try file.read(into: inBuf, frameCount: inChunk)
                } catch {
                    reachedEnd = true
                }
                if inBuf.frameLength == 0 {
                    reachedEnd = true
                    inputStatus.pointee = .endOfStream
                    return nil
                }
                inputStatus.pointee = .haveData
                return inBuf
            }
            if status == .error { break }
            if outBuf.frameLength > 0, !push(outBuf) { return }
            if status == .endOfStream || (reachedEnd && outBuf.frameLength == 0) { break }
        }
        finish()
    }

    /// Writes the whole buffer, waiting while the FIFO is full. Returns false if cancelled.
    private func push(_ buf: AVAudioPCMBuffer) -> Bool {
        guard let data = buf.floatChannelData else { return true }
        let l = data[0]
        let r: UnsafeMutablePointer<Float>? = buf.format.channelCount > 1 ? data[1] : nil
        var done = 0
        let total = Int(buf.frameLength)
        while done < total {
            if isCancelled { return false }
            let n = Int(sq_fifo_write(fifo, l + done, r.map { $0 + done }, Int32(total - done)))
            done += n
            if n == 0 { usleep(5000) }
        }
        return true
    }

    private func finish() {
        lock.withLock { _finished = true }
    }
}
