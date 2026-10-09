// DSP core tests: run with `cmake -S DSP -B build/dsp && cmake --build build/dsp && build/dsp/dsp_tests`
#include "spatialeq_dsp.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond, ...)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            ++failures;                                        \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> sine(double freq, double amp, int n) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i) v[i] = float(amp * std::sin(2 * kPi * freq * i / kFs));
    return v;
}

double rms(const std::vector<float>& v, int from) {
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}

float peak(const std::vector<float>& v, int from) {
    float p = 0;
    for (size_t i = from; i < v.size(); ++i) p = std::max(p, std::fabs(v[i]));
    return p;
}

// Neutral settings: everything off except what a test switches on.
sq_params neutral() {
    sq_params p;
    sq_params_default(&p);
    p.limiterEnabled = 0;
    return p;
}

void run(sq_engine* e, std::vector<float>& l, std::vector<float>& r, int block = 512) {
    for (size_t i = 0; i < l.size(); i += block) {
        const int n = int(std::min<size_t>(block, l.size() - i));
        sq_engine_process(e, l.data() + i, r.data() + i, n);
    }
}

sq_engine* makeEngine(const sq_params& p) {
    sq_engine* e = sq_engine_create();
    sq_engine_set_params(e, &p);
    sq_engine_prepare(e, kFs, 1024);
    return e;
}

void testEqResponse() {
    sq_params p = neutral();
    p.bands[5].gainDb = 6.0f; // 1 kHz peak
    float f[3] = {1000.0f, 100.0f, 10000.0f}, db[3];
    sq_eq_response(&p, kFs, f, db, 3);
    CHECK(std::fabs(db[0] - 6.0f) < 0.05f, "peak at 1k = %.3f dB", db[0]);
    CHECK(std::fabs(db[1]) < 0.3f, "100 Hz untouched: %.3f dB", db[1]);

    p.preampDb = -3.0f;
    sq_eq_response(&p, kFs, f, db, 1);
    CHECK(std::fabs(db[0] - 3.0f) < 0.05f, "preamp included: %.3f dB", db[0]);
}

void testTransparentWhenFlat() {
    sq_engine* e = makeEngine(neutral());
    auto l = sine(440, 0.5, 48000), r = sine(440, 0.5, 48000);
    const double in = rms(l, 0);
    run(e, l, r);
    CHECK(std::fabs(rms(l, 4800) - in) < 0.005, "flat L rms %.4f vs %.4f", rms(l, 4800), in);
    CHECK(std::fabs(rms(r, 4800) - in) < 0.005, "flat R rms %.4f vs %.4f", rms(r, 4800), in);
    sq_engine_destroy(e);
}

void testEqApplied() {
    sq_params p = neutral();
    p.bands[5].gainDb = 12.0f;
    sq_engine* e = makeEngine(p);
    auto l = sine(1000, 0.1, 48000), r = sine(1000, 0.1, 48000);
    const double in = rms(l, 0);
    run(e, l, r);
    const double gainDb = 20 * std::log10(rms(l, 4800) / in);
    CHECK(std::fabs(gainDb - 12.0) < 0.3, "EQ +12 dB measured %.2f dB", gainDb);
    sq_engine_destroy(e);
}

void testBinauralLocalisation() {
    sq_params p = neutral();
    p.spatialEnabled = 1;
    p.upmix = SQ_UPMIX_STEREO;
    p.sources[0] = {90.0f, 0.0f, 1.0f, 1.0f}; // L input placed hard right
    p.sources[1] = {90.0f, 0.0f, 1.0f, 0.0f}; // mute R input
    sq_engine* e = makeEngine(p);

    // Noise burst in the left input only.
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    std::vector<float> l(48000), r(48000, 0.0f);
    for (auto& x : l) x = u(rng);
    run(e, l, r);
    const double rl = rms(l, 4800), rr = rms(r, 4800);
    CHECK(rr > rl * 1.5, "source at +90 should be louder in right ear (L %.4f, R %.4f)", rl, rr);

    // Interaural delay: find lag of max cross-correlation; left ear should lag the right.
    int bestLag = 0;
    double best = -1e9;
    for (int lag = -60; lag <= 60; ++lag) {
        double s = 0;
        for (int i = 8000; i < 40000; ++i) s += double(r[i]) * l[i + lag];
        if (s > best) { best = s; bestLag = lag; }
    }
    CHECK(bestLag > 10 && bestLag < 40, "left ear lag = %d samples (expected ~30)", bestLag);
    sq_engine_destroy(e);
}

void testHeadYawMovesImage() {
    sq_params p = neutral();
    p.spatialEnabled = 1;
    p.sources[0] = {0.0f, 0.0f, 1.0f, 1.0f};
    p.sources[1] = {0.0f, 0.0f, 1.0f, 1.0f};
    sq_engine* e = makeEngine(p);
    sq_engine_set_head_yaw(e, 90.0f); // head turned right -> front source is now on the left
    std::mt19937 rng(2);
    std::uniform_real_distribution<float> u(-0.3f, 0.3f);
    std::vector<float> l(48000), r(48000);
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = u(rng);
    run(e, l, r);
    CHECK(rms(l, 4800) > rms(r, 4800) * 1.5, "yaw +90: L %.4f should exceed R %.4f", rms(l, 4800), rms(r, 4800));
    sq_engine_destroy(e);
}

void testLimiterCeiling() {
    sq_params p = neutral();
    p.limiterEnabled = 1;
    p.limiterCeilingDb = -1.0f;
    p.outputGainDb = 12.0f;
    sq_engine* e = makeEngine(p);
    auto l = sine(200, 0.9, 48000), r = sine(300, 0.9, 48000);
    run(e, l, r);
    const float ceiling = std::pow(10.0f, -1.0f / 20.0f);
    CHECK(peak(l, 0) <= ceiling + 1e-4f, "limiter L peak %.4f > %.4f", peak(l, 0), ceiling);
    CHECK(peak(r, 0) <= ceiling + 1e-4f, "limiter R peak %.4f > %.4f", peak(r, 0), ceiling);
    sq_meters m;
    sq_engine_get_meters(e, &m);
    CHECK(m.limiterReductionDb < -6.0f, "limiter reduction reported %.2f dB", m.limiterReductionDb);
    sq_engine_destroy(e);
}

void testBypassIsDry() {
    sq_params p = neutral();
    p.bands[5].gainDb = 12.0f;
    p.enabled = 0;
    sq_engine* e = makeEngine(p);
    auto l = sine(1000, 0.2, 24000), r = sine(1000, 0.2, 24000);
    const double in = rms(l, 0);
    run(e, l, r);
    CHECK(std::fabs(rms(l, 4800) - in) < 0.002, "bypass rms %.4f vs %.4f", rms(l, 4800), in);
    sq_engine_destroy(e);
}

void testAudioBufferListInterleaved() {
    sq_engine* e = makeEngine(neutral());
    const int frames = 256;
    // Input: 2 device-input channels to skip, then a stereo tap (interleaved).
    std::vector<float> devIn(frames * 2, 9.0f), tap(frames * 2), out(frames * 4, -1.0f);
    for (int i = 0; i < frames; ++i) { tap[i * 2] = 0.25f; tap[i * 2 + 1] = -0.25f; }

    alignas(AudioBufferList) unsigned char inStorage[sizeof(AudioBufferList) + sizeof(AudioBuffer)];
    auto* in = reinterpret_cast<AudioBufferList*>(inStorage);
    in->mNumberBuffers = 2;
    in->mBuffers[0] = {2, UInt32(devIn.size() * 4), devIn.data()};
    in->mBuffers[1] = {2, UInt32(tap.size() * 4), tap.data()};
    AudioBufferList outList;
    outList.mNumberBuffers = 1;
    outList.mBuffers[0] = {4, UInt32(out.size() * 4), out.data()};

    for (int k = 0; k < 40; ++k) sq_engine_process_abl(e, in, 2, &outList); // let smoothers settle
    CHECK(std::fabs(out[(frames - 1) * 4 + 0] - 0.25f) < 1e-3f, "abl L = %f", out[(frames - 1) * 4]);
    CHECK(std::fabs(out[(frames - 1) * 4 + 1] + 0.25f) < 1e-3f, "abl R = %f", out[(frames - 1) * 4 + 1]);
    CHECK(out[(frames - 1) * 4 + 2] == 0.0f && out[(frames - 1) * 4 + 3] == 0.0f, "extra outputs zeroed");
    sq_engine_destroy(e);
}

void testStabilityAllFeatures() {
    sq_params p;
    sq_params_default(&p);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int mode = 0; mode < 2; ++mode) {
        for (int upmix = 0; upmix < 3; ++upmix) {
            p.mode = mode;
            p.spatialEnabled = 1;
            p.upmix = upmix;
            p.xtcEnabled = 1;
            p.xtcStrength = 1.0f;
            p.width = 2.5f;
            p.reverbMix = 1.0f;
            p.roomSize = 1.0f;
            p.bassBoostDb = 12.0f;
            p.dialogueBoost = 1.0f;
            p.levelerEnabled = 1;
            for (int b = 0; b < p.numBands; ++b) p.bands[b].gainDb = u(rng) * 12.0f;
            sq_engine* e = makeEngine(p);
            std::vector<float> l(96000), r(96000);
            for (size_t i = 0; i < l.size(); ++i) { l[i] = u(rng); r[i] = u(rng); }
            run(e, l, r, 333);
            bool finite = true;
            for (size_t i = 0; i < l.size(); ++i) finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
            CHECK(finite, "non-finite output mode %d upmix %d", mode, upmix);
            CHECK(peak(l, 0) <= 0.9f && peak(r, 0) <= 0.9f, "limiter holds (mode %d upmix %d)", mode, upmix);
            sq_engine_destroy(e);
        }
    }
}

void benchmark() {
    sq_params p;
    sq_params_default(&p);
    p.spatialEnabled = 1;
    p.upmix = SQ_UPMIX_7_1;
    p.reverbMix = 0.5f;
    p.bassBoostDb = 6;
    p.dialogueBoost = 0.5f;
    p.levelerEnabled = 1;
    sq_engine* e = makeEngine(p);
    const int seconds = 20, block = 512;
    std::vector<float> l(block, 0.1f), r(block, -0.1f);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < seconds * int(kFs) / block; ++i) sq_engine_process(e, l.data(), r.data(), block);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("benchmark: 7.1 virtual + all extras, %d s of 48 kHz audio in %.3f s (%.0fx real time)\n",
                seconds, secs, seconds / secs);
    sq_engine_destroy(e);
}

} // namespace

int main() {
    testEqResponse();
    testTransparentWhenFlat();
    testEqApplied();
    testBinauralLocalisation();
    testHeadYawMovesImage();
    testLimiterCeiling();
    testBypassIsDry();
    testAudioBufferListInterleaved();
    testStabilityAllFeatures();
    benchmark();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all DSP tests passed\n");
    return 0;
}
