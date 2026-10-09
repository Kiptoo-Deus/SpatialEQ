import Foundation

/// Swift owner of the C++ engine. `handle` is safe to use from the audio thread.
final class DSPEngine {
    let handle: OpaquePointer

    init() {
        handle = sq_engine_create()
    }

    deinit {
        sq_engine_destroy(handle)
    }

    /// Only while IO is stopped.
    func prepare(sampleRate: Double, maxFrames: Int = 4096) {
        sq_engine_prepare(handle, sampleRate, Int32(maxFrames))
    }

    func apply(_ settings: SoundSettings, enabled: Bool) {
        var p = settings.cParams(enabled: enabled)
        sq_engine_set_params(handle, &p)
    }

    func setHeadYaw(degrees: Double) {
        sq_engine_set_head_yaw(handle, Float(degrees))
    }

    func readAnalysis(into buffer: UnsafeMutablePointer<Float>, max: Int) -> Int {
        Int(sq_engine_read_analysis(handle, buffer, Int32(max)))
    }

    var meters: sq_meters {
        var m = sq_meters()
        sq_engine_get_meters(handle, &m)
        return m
    }

    static func eqResponse(_ settings: SoundSettings, sampleRate: Double, frequencies: [Float]) -> [Float] {
        var p = settings.cParams(enabled: true)
        var out = [Float](repeating: 0, count: frequencies.count)
        frequencies.withUnsafeBufferPointer { f in
            out.withUnsafeMutableBufferPointer { o in
                sq_eq_response(&p, sampleRate, f.baseAddress, o.baseAddress, Int32(frequencies.count))
            }
        }
        return out
    }
}
