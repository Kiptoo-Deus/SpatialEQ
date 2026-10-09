import Accelerate
import Foundation
import os

/// Pulls post-processing audio from the engine on a background timer and turns it into
/// log-spaced spectrum bands plus a short history for the 3D terrain. Readers take snapshots.
final class Analyzer {
    struct Snapshot {
        var bands: [Float]          // 0...1, low to high frequency
        var history: [[Float]]      // newest first
        var level: Float            // 0...1 overall loudness
    }

    static let bandCount = 64
    static let historyLength = 48
    static let minFreq: Float = 20
    static let maxFreq: Float = 20000

    private let dsp: DSPEngine
    private let fftSize = 4096
    private let log2n: vDSP_Length = 12
    private let fft: vDSP.FFT<DSPSplitComplex>
    private let window: [Float]
    private var ring: [Float]
    private var scratch: [Float]
    private var smoothed: [Float]
    private var binRanges: [Range<Int>] = []
    private var sampleRate: Double = 48000

    private var lock = os_unfair_lock()
    private var latest: Snapshot
    private var timer: DispatchSourceTimer?
    private let queue = DispatchQueue(label: "com.spatialeq.analyzer", qos: .userInitiated)

    init(dsp: DSPEngine) {
        self.dsp = dsp
        fft = vDSP.FFT(log2n: log2n, radix: .radix2, ofType: DSPSplitComplex.self)!
        window = vDSP.window(ofType: Float.self, usingSequence: .hanningDenormalized, count: fftSize, isHalfWindow: false)
        ring = [Float](repeating: 0, count: fftSize)
        scratch = [Float](repeating: 0, count: 8192)
        smoothed = [Float](repeating: 0, count: Self.bandCount)
        latest = Snapshot(bands: smoothed, history: Array(repeating: smoothed, count: Self.historyLength), level: 0)
        computeBins()
    }

    func setSampleRate(_ rate: Double) {
        queue.async {
            self.sampleRate = rate
            self.computeBins()
        }
    }

    func start() {
        guard timer == nil else { return }
        let t = DispatchSource.makeTimerSource(queue: queue)
        t.schedule(deadline: .now(), repeating: .milliseconds(16), leeway: .milliseconds(2))
        t.setEventHandler { [weak self] in self?.tick() }
        t.resume()
        timer = t
    }

    func stop() {
        timer?.cancel()
        timer = nil
    }

    func snapshot() -> Snapshot {
        os_unfair_lock_lock(&lock)
        defer { os_unfair_lock_unlock(&lock) }
        return latest
    }

    /// Centre frequency of band i, for labelling.
    static func frequency(ofBand i: Int) -> Float {
        minFreq * pow(maxFreq / minFreq, (Float(i) + 0.5) / Float(bandCount))
    }

    private func computeBins() {
        let binHz = Float(sampleRate) / Float(fftSize)
        binRanges = (0..<Self.bandCount).map { i in
            let lo = Self.minFreq * pow(Self.maxFreq / Self.minFreq, Float(i) / Float(Self.bandCount))
            let hi = Self.minFreq * pow(Self.maxFreq / Self.minFreq, Float(i + 1) / Float(Self.bandCount))
            let a = max(1, Int(lo / binHz))
            let b = min(fftSize / 2 - 1, max(a + 1, Int(hi / binHz)))
            return a..<b
        }
    }

    private func tick() {
        // Drain everything available, keeping the newest fftSize samples.
        var got = 0
        scratch.withUnsafeMutableBufferPointer { buf in
            got = dsp.readAnalysis(into: buf.baseAddress!, max: buf.count)
        }
        guard got > 0 else { return decayToSilence() }
        let take = min(got, fftSize)
        ring.removeFirst(take)
        ring.append(contentsOf: scratch[(got - take)..<got])

        let windowed = vDSP.multiply(ring, window)
        var real = [Float](repeating: 0, count: fftSize / 2)
        var imag = [Float](repeating: 0, count: fftSize / 2)
        var mags = [Float](repeating: 0, count: fftSize / 2)
        real.withUnsafeMutableBufferPointer { rp in
            imag.withUnsafeMutableBufferPointer { ip in
                var split = DSPSplitComplex(realp: rp.baseAddress!, imagp: ip.baseAddress!)
                windowed.withUnsafeBufferPointer { wp in
                    wp.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: fftSize / 2) {
                        vDSP_ctoz($0, 2, &split, 1, vDSP_Length(fftSize / 2))
                    }
                }
                fft.forward(input: split, output: &split)
                mags.withUnsafeMutableBufferPointer { mp in
                    vDSP_zvmags(&split, 1, mp.baseAddress!, 1, vDSP_Length(fftSize / 2))
                }
            }
        }

        let norm = Float(fftSize * fftSize) / 4
        var bands = [Float](repeating: 0, count: Self.bandCount)
        for (i, range) in binRanges.enumerated() {
            var peak: Float = 0
            for k in range { peak = max(peak, mags[k]) }
            let db = 10 * log10(peak / norm + 1e-12)
            // Map -80...0 dBFS to 0...1 with a slight tilt so highs are visible.
            let tilt = Float(i) / Float(Self.bandCount) * 12
            let v = min(1, max(0, (db + tilt + 80) / 80))
            let s = smoothed[i]
            smoothed[i] = v > s ? s + (v - s) * 0.6 : s + (v - s) * 0.15
            bands[i] = smoothed[i]
        }
        let rms = sqrt(vDSP.meanSquare(ring))
        publish(bands: smoothed, level: min(1, max(0, (20 * log10(rms + 1e-9) + 60) / 60)))
    }

    private func decayToSilence() {
        for i in smoothed.indices { smoothed[i] *= 0.9 }
        publish(bands: smoothed, level: 0)
    }

    private func publish(bands: [Float], level: Float) {
        os_unfair_lock_lock(&lock)
        latest.bands = bands
        latest.history.insert(bands, at: 0)
        if latest.history.count > Self.historyLength { latest.history.removeLast() }
        latest.level = latest.level * 0.7 + level * 0.3
        os_unfair_lock_unlock(&lock)
    }
}
