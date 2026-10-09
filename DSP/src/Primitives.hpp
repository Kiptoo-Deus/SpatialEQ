// Small real-time building blocks. Everything here is allocation-free after prepare().
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sq {

constexpr double kPi = 3.14159265358979323846;

inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1e-9f)); }

// ---------------------------------------------------------------------------------------------
// Biquad (RBJ cookbook), transposed direct form II with double state.

enum class BandType { Peak = 0, LowShelf, HighShelf, LowPass, HighPass };

struct BiquadCoeffs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

inline BiquadCoeffs designBiquad(BandType type, double fs, double freq, double gainDb, double q) {
    freq = std::clamp(freq, 10.0, fs * 0.49);
    q = std::max(q, 0.05);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double sqA2a = 2.0 * std::sqrt(A) * alpha;

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type) {
    case BandType::Peak:
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
        break;
    case BandType::LowShelf:
        b0 = A * ((A + 1) - (A - 1) * cw + sqA2a);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - sqA2a);
        a0 = (A + 1) + (A - 1) * cw + sqA2a;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - sqA2a;
        break;
    case BandType::HighShelf:
        b0 = A * ((A + 1) + (A - 1) * cw + sqA2a);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - sqA2a);
        a0 = (A + 1) - (A - 1) * cw + sqA2a;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - sqA2a;
        break;
    case BandType::LowPass:
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    case BandType::HighPass:
        b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

inline double biquadMagnitudeDb(const BiquadCoeffs& c, double fs, double freq) {
    const double w = 2.0 * kPi * freq / fs;
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = z1 * z1;
    const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10(std::max(std::abs(num / den), 1e-12));
}

struct Biquad {
    BiquadCoeffs c;
    double z1 = 0, z2 = 0;

    inline float process(float x) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return static_cast<float>(y);
    }
    void reset() { z1 = z2 = 0; }
};

// ---------------------------------------------------------------------------------------------
// First-order section: y = b0 x + b1 x[n-1] - a1 y[n-1]

struct OnePole {
    float b0 = 1, b1 = 0, a1 = 0;
    float x1 = 0, y1 = 0;

    inline float process(float x) {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x;
        y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0; }

    void setLowPass(double fs, double fc) {
        const double k = std::tan(kPi * std::clamp(fc, 1.0, fs * 0.49) / fs);
        b0 = b1 = float(k / (1 + k));
        a1 = float((k - 1) / (k + 1));
    }
    void setHighPass(double fs, double fc) {
        const double k = std::tan(kPi * std::clamp(fc, 1.0, fs * 0.49) / fs);
        b0 = float(1 / (1 + k));
        b1 = -b0;
        a1 = float((k - 1) / (k + 1));
    }
};

// ---------------------------------------------------------------------------------------------
// Per-sample exponential smoother.

struct Smoother {
    float value = 0, target = 0, coeff = 0;

    void setTime(double fs, double seconds) {
        coeff = seconds <= 0 ? 0.0f : float(std::exp(-1.0 / (fs * seconds)));
    }
    void snap(float v) { value = target = v; }
    inline float next() {
        value = target + coeff * (value - target);
        return value;
    }
};

// ---------------------------------------------------------------------------------------------
// Power-of-two delay line with linearly interpolated fractional reads.

class DelayLine {
public:
    void allocate(int minSize) {
        int size = 1;
        while (size < minSize) size <<= 1;
        buf_.assign(size, 0.0f);
        mask_ = size - 1;
        pos_ = 0;
    }
    void reset(float value = 0.0f) { std::fill(buf_.begin(), buf_.end(), value); pos_ = 0; }
    int capacity() const { return mask_; }

    inline void push(float x) {
        pos_ = (pos_ + 1) & mask_;
        buf_[pos_] = x;
    }
    // delay 0 returns the most recently pushed sample.
    inline float read(float delay) const {
        delay = std::clamp(delay, 0.0f, float(mask_ - 1));
        const int whole = int(delay);
        const float frac = delay - float(whole);
        const float a = buf_[(pos_ - whole) & mask_];
        const float b = buf_[(pos_ - whole - 1) & mask_];
        return a + frac * (b - a);
    }
    inline float readInt(int delay) const { return buf_[(pos_ - delay) & mask_]; }

private:
    std::vector<float> buf_;
    int mask_ = 0;
    int pos_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Lock-free triple buffer: one writer publishes whole values, one reader picks up the latest.

template <typename T>
class TripleBuffer {
public:
    // Writer side.
    void publish(const T& v) {
        slots_[write_] = v;
        write_ = middle_.exchange(write_ | kDirty, std::memory_order_acq_rel) & kIndexMask;
    }
    // Reader side. Returns true and fills `out` if a new value arrived.
    bool consume(T& out) {
        if (!(middle_.load(std::memory_order_acquire) & kDirty)) return false;
        read_ = middle_.exchange(read_, std::memory_order_acq_rel) & kIndexMask;
        out = slots_[read_];
        return true;
    }

private:
    static constexpr int kDirty = 4;
    static constexpr int kIndexMask = 3;
    T slots_[3]{};
    int write_ = 0;
    int read_ = 2;
    std::atomic<int> middle_{1};
};

// ---------------------------------------------------------------------------------------------
// Single-producer / single-consumer float ring. Producer drops samples when full.

class SpscRing {
public:
    void allocate(int minSize) {
        int size = 1;
        while (size < minSize) size <<= 1;
        buf_.assign(size, 0.0f);
        mask_ = size - 1;
        head_.store(0);
        tail_.store(0);
    }

    void push(const float* src, int n) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        const uint32_t free = uint32_t(mask_ + 1) - (head - tail);
        n = std::min<uint32_t>(n, free);
        for (int i = 0; i < n; ++i) buf_[(head + i) & mask_] = src[i];
        head_.store(head + n, std::memory_order_release);
    }

    int pop(float* dst, int n) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        n = std::min<uint32_t>(n, head - tail);
        for (int i = 0; i < n; ++i) dst[i] = buf_[(tail + i) & mask_];
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

private:
    std::vector<float> buf_;
    uint32_t mask_ = 0;
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
};

} // namespace sq
