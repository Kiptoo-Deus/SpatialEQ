#include "spatialeq_spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace {

constexpr int kFftSize = 4096;
constexpr float kMinFreq = 20.0f, kMaxFreq = 20000.0f;
constexpr double kPi = 3.14159265358979323846;

// In-place iterative radix-2 FFT.
void fft(std::vector<std::complex<float>>& a, const std::vector<std::complex<float>>& twiddle) {
    const int n = int(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const int step = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < len / 2; ++k) {
                const auto w = twiddle[k * step];
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

} // namespace

struct sq_spectrum {
    int bands;
    std::vector<float> ring = std::vector<float>(kFftSize, 0.0f);
    std::vector<float> scratch = std::vector<float>(16384, 0.0f);
    std::vector<float> window = std::vector<float>(kFftSize);
    std::vector<std::complex<float>> buf = std::vector<std::complex<float>>(kFftSize);
    std::vector<std::complex<float>> twiddle = std::vector<std::complex<float>>(kFftSize / 2);
    std::vector<float> smoothed;
    float level = 0;
};

extern "C" {

sq_spectrum* sq_spectrum_create(int numBands) {
    auto* s = new sq_spectrum();
    s->bands = std::max(1, numBands);
    s->smoothed.assign(s->bands, 0.0f);
    for (int i = 0; i < kFftSize; ++i) s->window[i] = float(0.5 - 0.5 * std::cos(2 * kPi * i / (kFftSize - 1)));
    for (int k = 0; k < kFftSize / 2; ++k) s->twiddle[k] = std::polar(1.0f, float(-2 * kPi * k / kFftSize));
    return s;
}

void sq_spectrum_destroy(sq_spectrum* s) { delete s; }

float sq_spectrum_band_frequency(const sq_spectrum* s, int i) {
    return kMinFreq * std::pow(kMaxFreq / kMinFreq, (float(i) + 0.5f) / float(s->bands));
}

float sq_spectrum_update(sq_spectrum* s, sq_engine* e, double sampleRate, float* out) {
    const int got = sq_engine_read_analysis(e, s->scratch.data(), int(s->scratch.size()));
    if (got <= 0) {
        for (int i = 0; i < s->bands; ++i) out[i] = (s->smoothed[i] *= 0.9f);
        s->level *= 0.8f;
        return s->level;
    }
    // Keep the newest kFftSize samples.
    const int take = std::min(got, kFftSize);
    std::move(s->ring.begin() + take, s->ring.end(), s->ring.begin());
    std::copy(s->scratch.begin() + (got - take), s->scratch.begin() + got, s->ring.end() - take);

    double ms = 0;
    for (int i = 0; i < kFftSize; ++i) {
        s->buf[i] = {s->ring[i] * s->window[i], 0.0f};
        ms += double(s->ring[i]) * s->ring[i];
    }
    fft(s->buf, s->twiddle);

    const float binHz = float(sampleRate) / kFftSize;
    const float norm = float(kFftSize) * kFftSize / 4.0f;
    for (int i = 0; i < s->bands; ++i) {
        const float lo = kMinFreq * std::pow(kMaxFreq / kMinFreq, float(i) / s->bands);
        const float hi = kMinFreq * std::pow(kMaxFreq / kMinFreq, float(i + 1) / s->bands);
        const int a = std::max(1, int(lo / binHz));
        const int b = std::min(kFftSize / 2 - 1, std::max(a + 1, int(hi / binHz)));
        float peak = 0;
        for (int k = a; k < b; ++k) peak = std::max(peak, std::norm(s->buf[k]));
        const float db = 10.0f * std::log10(peak / norm + 1e-12f);
        const float tilt = float(i) / s->bands * 12.0f; // lift highs so they stay visible
        const float v = std::clamp((db + tilt + 80.0f) / 80.0f, 0.0f, 1.0f);
        float& sm = s->smoothed[i];
        sm += (v - sm) * (v > sm ? 0.6f : 0.15f);
        out[i] = sm;
    }
    const float rms = float(std::sqrt(ms / kFftSize));
    const float lv = std::clamp((20.0f * std::log10(rms + 1e-9f) + 60.0f) / 60.0f, 0.0f, 1.0f);
    s->level = s->level * 0.7f + lv * 0.3f;
    return s->level;
}

} // extern "C"
