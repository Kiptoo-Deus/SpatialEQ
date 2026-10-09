package com.savannahdsp.spatialeq.audio

/** Thin JNI wrapper over the shared C++ DSP core (DSP/ in the repository root). */
class NativeEngine : AutoCloseable {
    private val handle: Long = nativeCreate()

    fun prepare(sampleRate: Int, maxFrames: Int) = nativePrepare(handle, sampleRate, maxFrames)
    fun setParams(packed: FloatArray) = nativeSetParams(handle, packed)
    fun setHeadYaw(degrees: Float) = nativeSetHeadYaw(handle, degrees)

    /** Interleaved stereo float, processed in place. Capture thread only. */
    fun process(buffer: FloatArray, frames: Int) = nativeProcess(handle, buffer, frames)

    /** Fills [bands] with the live spectrum (0..1) and returns the overall level. UI thread only. */
    fun spectrum(bands: FloatArray): Float = nativeSpectrum(handle, bands)

    /** peakL, peakR, limiter reduction dB, leveler gain dB. */
    fun meters(out: FloatArray) = nativeMeters(handle, out)

    override fun close() = nativeDestroy(handle)

    companion object {
        init { System.loadLibrary("spatialeq") }

        fun eqResponse(packed: FloatArray, sampleRate: Float, freqs: FloatArray): FloatArray =
            FloatArray(freqs.size).also { nativeEqResponse(packed, sampleRate, freqs, it) }

        @JvmStatic private external fun nativeCreate(): Long
        @JvmStatic private external fun nativeDestroy(h: Long)
        @JvmStatic private external fun nativePrepare(h: Long, sampleRate: Int, maxFrames: Int)
        @JvmStatic private external fun nativeSetParams(h: Long, packed: FloatArray)
        @JvmStatic private external fun nativeSetHeadYaw(h: Long, yaw: Float)
        @JvmStatic private external fun nativeProcess(h: Long, buffer: FloatArray, frames: Int)
        @JvmStatic private external fun nativeSpectrum(h: Long, bands: FloatArray): Float
        @JvmStatic private external fun nativeMeters(h: Long, out: FloatArray)
        @JvmStatic private external fun nativeEqResponse(packed: FloatArray, sampleRate: Float, freqs: FloatArray, outDb: FloatArray)
    }
}
