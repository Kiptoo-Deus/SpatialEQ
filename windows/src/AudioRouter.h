// System audio routing on Windows:
//
//   apps --> source endpoint --(WASAPI loopback)--> DSP engine --(WASAPI render)--> output endpoint
//
// or, with the "All apps" source (Windows 10 2004+):
//
//   apps --(process loopback, everything but us)--> DSP engine --(WASAPI render)--> any output endpoint
//
// Windows has no public API that both captures and silences other apps' audio (the macOS process tap
// does both). Two ways round it:
//   - All apps: capture every other process and turn them down in the volume mixer (SessionDucker,
//     Ducker.h); works with a single device.
//   - A virtual cable such as VB-CABLE set as the default playback device: apps play into the cable,
//     nobody hears it directly, and SpatialEQ plays the processed signal on the real device.
// Any other playback device works as a source too, but its unprocessed sound then stays audible.
#pragma once

#include "Ducker.h"
#include "spatialeq_dsp.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sq::app {

// Single-threaded stereo ring buffer between the capture and render sides of the audio thread.
class StereoRing {
public:
    void allocate(int frames) {
        l_.assign(size_t(frames), 0.0f);
        r_.assign(size_t(frames), 0.0f);
        head_ = tail_ = 0;
    }
    int capacity() const { return int(l_.size()); }
    int available() const { return int(head_ - tail_); }
    int space() const { return capacity() - available(); }

    // Writes up to `n` frames; returns how many fit.
    int write(const float* l, const float* r, int n);
    // Reads up to `n` frames into `outCh`-channel interleaved `dst` (channels 2+ zeroed).
    int readInterleaved(float* dst, int outCh, int n);
    // Drops the oldest frames so that at most `keep` remain.
    void trimTo(int keep) {
        if (available() > keep) tail_ = head_ - uint64_t(keep);
    }
    void clear() { tail_ = head_; }

private:
    std::vector<float> l_, r_;
    uint64_t head_ = 0, tail_ = 0;
};

// Converts `frames` of interleaved capture audio to planar stereo float.
// Supports 32-bit float and 16/24/32-bit integer PCM, any channel count (first two channels used,
// mono is duplicated). Returns false for unsupported formats.
struct SampleFormat {
    int channels = 2;
    int bitsPerSample = 32;  // container size
    int validBits = 32;
    bool isFloat = true;
};
bool deinterleave(const void* src, const SampleFormat& fmt, int frames, float* l, float* r);

// Source id meaning "every app except SpatialEQ" (process loopback, Windows 10 2004+). Used with
// SessionDucker, so the output may be the very device the apps play on.
inline constexpr wchar_t kAllAppsSource[] = L"spatialeq:all-apps";
bool processLoopbackSupported();

// Gain for captured "all apps" audio: undoes the -80 dB mixer duck, but drops to unity for a while
// whenever full-level (un-ducked) audio shows up, so a newly started app is never boosted by 80 dB.
class DuckCompensation {
public:
    // Ducked audio peaks at about kDuckLevel (an app at full scale); allow some float overs.
    static constexpr float kOverThreshold = 4.0f * kDuckLevel;
    float gainFor(const float* l, const float* r, int frames, double rate);

private:
    int holdFrames_ = 0;
};

class AudioRouter {
public:
    enum class State { Stopped, Starting, Running, Failed };
    struct Status {
        State state = State::Stopped;
        std::string message;      // error text when Failed
        double sampleRate = 0;
        double latencyMs = 0;
        int outputChannels = 0;
    };

    explicit AudioRouter(sq_engine* engine) : engine_(engine) {}
    ~AudioRouter() { stop(); }
    AudioRouter(const AudioRouter&) = delete;
    AudioRouter& operator=(const AudioRouter&) = delete;

    // Starts routing on a new audio thread; returns immediately. Restarts if already running.
    void start(const std::wstring& sourceId, const std::wstring& outputId);
    void stop();

    Status status() const;
    // True once after the route died (device unplugged, format change, ...) and should be rebuilt.
    bool consumeNeedsRestart() { return needsRestart_.exchange(false); }
    bool isActive() const { return thread_.joinable(); }

private:
    void run(std::wstring sourceId, std::wstring outputId);
    void setStatus(const Status& s);
    void fail(const std::string& message, bool restart);

    sq_engine* engine_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> needsRestart_{false};
    mutable std::mutex statusMutex_;
    Status status_;
};

} // namespace sq::app
