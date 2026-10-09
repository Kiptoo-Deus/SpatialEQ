// Engine: wires the processing chain together and exposes the C API.
//
// Chain: preamp -> dialogue -> bass -> [headphones: upmix + binaural | speakers: width + XTC]
//        -> room reverb -> EQ -> leveler -> output gain -> limiter -> (bypass crossfade)
//
// EQ sits after the spatial stage so headphone-correction profiles (AutoEq) correct the final
// signal that reaches the driver.
#include "spatialeq_dsp.h"

#include "Effects.hpp"
#include "Spatial.hpp"

#include <cstdint>
#if !defined(__aarch64__) && (defined(__SSE__) || defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#define SQ_USE_MXCSR 1
#endif

namespace sq {

namespace {

struct ScopedFlushDenormals {
#if defined(__aarch64__)
    uint64_t saved;
    ScopedFlushDenormals() {
        asm volatile("mrs %0, fpcr" : "=r"(saved));
        asm volatile("msr fpcr, %0" ::"r"(saved | (1ULL << 24)));
    }
    ~ScopedFlushDenormals() { asm volatile("msr fpcr, %0" ::"r"(saved)); }
#elif defined(SQ_USE_MXCSR)
    unsigned saved;
    ScopedFlushDenormals() {
        saved = _mm_getcsr();
        _mm_setcsr(saved | 0x8040); // FTZ | DAZ
    }
    ~ScopedFlushDenormals() { _mm_setcsr(saved); }
#endif
};

} // namespace

class Engine {
public:
    Engine() {
        sq_params_default(&params_);
        analysis_.allocate(1 << 15);
    }

    void prepare(double fs, int maxFrames) {
        fs_ = fs;
        maxFrames_ = std::max(64, maxFrames);
        for (auto* v : {&l_, &r_, &dryL_, &dryR_, &spatL_, &spatR_, &mono_}) v->assign(maxFrames_, 0.0f);

        upmix_.prepare(fs, maxFrames_);
        binaural_.prepare(fs);
        speakers_.prepare(fs);
        reverb_.prepare(fs);
        bass_.prepare(fs);
        dialogue_.prepare(fs);
        leveler_.prepare(fs);
        limiter_.prepare(fs);

        preamp_.setTime(fs, 0.02);
        output_.setTime(fs, 0.02);
        wet_.setTime(fs, 0.03);
        reverbWet_.setTime(fs, 0.05);
        dryDelay_[0].allocate(limiter_.latency() + 4);
        dryDelay_[1].allocate(limiter_.latency() + 4);

        mailbox_.consume(params_);
        applyParams(true);
        reset();
    }

    void reset() {
        for (auto& b : eq_) for (auto& f : b) f.reset();
        upmix_.reset();
        binaural_.reset();
        speakers_.reset();
        reverb_.reset();
        bass_.reset();
        leveler_.reset();
        limiter_.reset();
        dryDelay_[0].reset();
        dryDelay_[1].reset();
    }

    void setParams(const sq_params& p) { mailbox_.publish(p); }
    void setHeadYaw(float deg) { headYaw_.store(deg, std::memory_order_relaxed); }

    void process(float* l, float* r, int frames) {
        ScopedFlushDenormals ftz;
        while (frames > 0) {
            const int n = std::min(frames, maxFrames_);
            processBlock(l, r, n);
            l += n;
            r += n;
            frames -= n;
        }
    }

    void processInterleaved(const float* in, int inCh, float* out, int outCh, int frames) {
        ScopedFlushDenormals ftz;
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, maxFrames_);
            const float* src = in + size_t(done) * inCh;
            for (int i = 0; i < n; ++i) {
                l_[i] = src[size_t(i) * inCh];
                r_[i] = inCh > 1 ? src[size_t(i) * inCh + 1] : l_[i];
            }
            processBlock(l_.data(), r_.data(), n);
            float* dst = out + size_t(done) * outCh;
            for (int i = 0; i < n; ++i) {
                float* frame = dst + size_t(i) * outCh;
                frame[0] = l_[i];
                if (outCh > 1) frame[1] = r_[i];
                for (int c = 2; c < outCh; ++c) frame[c] = 0.0f;
            }
            done += n;
        }
    }

#ifdef SQ_HAS_COREAUDIO
    void processAbl(const AudioBufferList* in, int inOffset, AudioBufferList* out) {
        if (!out || out->mNumberBuffers == 0) return;
        ScopedFlushDenormals ftz;

        // Output frame count from the first output buffer.
        const AudioBuffer& ob0 = out->mBuffers[0];
        const int outCh0 = std::max<int>(1, ob0.mNumberChannels);
        const int total = int(ob0.mDataByteSize / (sizeof(float) * outCh0));

        for (int done = 0; done < total;) {
            const int n = std::min(total - done, maxFrames_);
            gather(in, inOffset, done, n);
            processBlock(l_.data(), r_.data(), n);
            scatter(out, done, n);
            done += n;
        }
    }

#endif // SQ_HAS_COREAUDIO

    int readAnalysis(float* dst, int n) { return analysis_.pop(dst, n); }

    void meters(sq_meters* m) const {
        m->peakL = peakL_.load(std::memory_order_relaxed);
        m->peakR = peakR_.load(std::memory_order_relaxed);
        m->limiterReductionDb = gr_.load(std::memory_order_relaxed);
        m->levelerGainDb = levelerGain_.load(std::memory_order_relaxed);
    }

private:
#ifdef SQ_HAS_COREAUDIO
    // Collects input channels [inOffset, inOffset+1] across buffers into l_/r_.
    void gather(const AudioBufferList* in, int inOffset, int start, int n) {
        const float* src[2] = {nullptr, nullptr};
        int stride[2] = {1, 1};
        if (in) {
            int ch = 0;
            for (UInt32 b = 0; b < in->mNumberBuffers; ++b) {
                const AudioBuffer& buf = in->mBuffers[b];
                const int nch = std::max<int>(1, buf.mNumberChannels);
                const int frames = int(buf.mDataByteSize / (sizeof(float) * nch));
                for (int c = 0; c < nch; ++c, ++ch) {
                    const int want = ch - inOffset;
                    if (want < 0 || want > 1 || !buf.mData || frames < start + n) continue;
                    src[want] = static_cast<const float*>(buf.mData) + size_t(start) * nch + c;
                    stride[want] = nch;
                }
            }
        }
        if (src[0] && !src[1]) { src[1] = src[0]; stride[1] = stride[0]; }
        for (int c = 0; c < 2; ++c) {
            float* dst = c == 0 ? l_.data() : r_.data();
            if (!src[c]) { std::fill(dst, dst + n, 0.0f); continue; }
            for (int i = 0; i < n; ++i) dst[i] = src[c][size_t(i) * stride[c]];
        }
    }

    void scatter(AudioBufferList* out, int start, int n) {
        int ch = 0;
        for (UInt32 b = 0; b < out->mNumberBuffers; ++b) {
            AudioBuffer& buf = out->mBuffers[b];
            if (!buf.mData) continue;
            const int nch = std::max<int>(1, buf.mNumberChannels);
            const int frames = int(buf.mDataByteSize / (sizeof(float) * nch));
            float* base = static_cast<float*>(buf.mData);
            for (int c = 0; c < nch; ++c, ++ch) {
                const float* src = ch == 0 ? l_.data() : ch == 1 ? r_.data() : nullptr;
                for (int i = 0; i < n && start + i < frames; ++i)
                    base[size_t(start + i) * nch + c] = src ? src[i] : 0.0f;
            }
        }
    }

#endif // SQ_HAS_COREAUDIO

    void applyParams(bool snap) {
        const sq_params& p = params_;
        numBands_ = std::clamp(p.numBands, 0, SQ_MAX_BANDS);
        for (int i = 0; i < numBands_; ++i) {
            const sq_band& b = p.bands[i];
            bandOn_[i] = p.eqEnabled && b.enabled;
            const auto c = designBiquad(BandType(std::clamp(b.type, 0, 4)), fs_, b.freq, b.gainDb, b.q);
            eq_[i][0].c = c;
            eq_[i][1].c = c;
        }

        upmixLayout_ = std::clamp(p.upmix, 0, 2);
        for (int i = 0; i < kMaxSources; ++i) {
            const sq_source& s = p.sources[i];
            binaural_.setPosition(i, {s.azimuthDeg, s.elevationDeg, s.distance, s.gain});
        }
        speakers_.configure(p.width, p.xtcEnabled, p.xtcStrength, p.speakerSpanDeg);
        reverb_.configure(p.roomSize);
        bass_.configure(p.bassBoostDb);
        dialogue_.configure(p.dialogueBoost);

        preamp_.target = dbToGain(p.preampDb);
        output_.target = dbToGain(p.outputGainDb);
        wet_.target = p.enabled ? 1.0f : 0.0f;
        reverbWet_.target = std::clamp(p.reverbMix, 0.0f, 1.0f) * 0.6f;
        if (snap) {
            preamp_.snap(preamp_.target);
            output_.snap(output_.target);
            wet_.snap(wet_.target);
            reverbWet_.snap(reverbWet_.target);
        }
    }

    void processBlock(float* l, float* r, int n) {
        if (mailbox_.consume(params_)) applyParams(false);
        binaural_.setHeadYaw(headYaw_.load(std::memory_order_relaxed));
        binaural_.update();
        const sq_params& p = params_;

        std::memcpy(dryL_.data(), l, sizeof(float) * n);
        std::memcpy(dryR_.data(), r, sizeof(float) * n);

        for (int i = 0; i < n; ++i) {
            const float g = preamp_.next();
            l[i] *= g;
            r[i] *= g;
        }
        dialogue_.process(l, r, n);
        bass_.process(l, r, n);

        // Reverb send is taken pre-spatial so the room surrounds the virtual sources.
        std::memcpy(spatL_.data(), l, sizeof(float) * n);
        std::memcpy(spatR_.data(), r, sizeof(float) * n);

        if (p.mode == SQ_MODE_HEADPHONES && p.spatialEnabled) {
            float* const* chans = nullptr;
            const int count = upmix_.process(upmixLayout_, l, r, n, &chans);
            std::fill(l, l + n, 0.0f);
            std::fill(r, r + n, 0.0f);
            binaural_.process(chans, count, l, r, n);
        } else if (p.mode == SQ_MODE_SPEAKERS) {
            speakers_.process(l, r, n);
        }

        // Wet amount is smoothed per block; the network keeps running at 0 so toggling is click-free.
        for (int i = 0; i < n; ++i) reverbWet_.next();
        reverb_.process(spatL_.data(), spatR_.data(), l, r, n, reverbWet_.value);

        for (int b = 0; b < numBands_; ++b) {
            if (!bandOn_[b]) continue;
            for (int i = 0; i < n; ++i) {
                l[i] = eq_[b][0].process(l[i]);
                r[i] = eq_[b][1].process(r[i]);
            }
        }

        leveler_.process(l, r, n, p.levelerTargetDb, p.levelerEnabled);
        for (int i = 0; i < n; ++i) {
            const float g = output_.next();
            l[i] *= g;
            r[i] *= g;
        }
        const float gr = limiter_.process(l, r, n, p.limiterCeilingDb, p.limiterEnabled);

        // Bypass crossfade against the dry signal, delayed by the limiter latency to stay aligned.
        float pl = 0, pr = 0;
        for (int i = 0; i < n; ++i) {
            dryDelay_[0].push(dryL_[i]);
            dryDelay_[1].push(dryR_[i]);
            const float w = wet_.next();
            const float dl = dryDelay_[0].readInt(limiter_.latency());
            const float dr = dryDelay_[1].readInt(limiter_.latency());
            l[i] = dl + w * (l[i] - dl);
            r[i] = dr + w * (r[i] - dr);
            pl = std::max(pl, std::fabs(l[i]));
            pr = std::max(pr, std::fabs(r[i]));
            mono_[i] = 0.5f * (l[i] + r[i]);
        }

        analysis_.push(mono_.data(), n);
        peakL_.store(pl, std::memory_order_relaxed);
        peakR_.store(pr, std::memory_order_relaxed);
        gr_.store(gr, std::memory_order_relaxed);
        levelerGain_.store(leveler_.currentGainDb(), std::memory_order_relaxed);
    }

    double fs_ = 48000;
    int maxFrames_ = 4096;
    sq_params params_{};
    TripleBuffer<sq_params> mailbox_;
    std::atomic<float> headYaw_{0};

    std::vector<float> l_, r_, dryL_, dryR_, spatL_, spatR_, mono_;
    Biquad eq_[SQ_MAX_BANDS][2];
    bool bandOn_[SQ_MAX_BANDS]{};
    int numBands_ = 0;
    int upmixLayout_ = 0;

    Upmixer upmix_;
    BinauralRenderer binaural_;
    SpeakerProcessor speakers_;
    RoomReverb reverb_;
    BassEnhancer bass_;
    DialogueEnhancer dialogue_;
    Leveler leveler_;
    Limiter limiter_;
    DelayLine dryDelay_[2];
    Smoother preamp_, output_, wet_, reverbWet_;

    SpscRing analysis_;
    std::atomic<float> peakL_{0}, peakR_{0}, gr_{0}, levelerGain_{0};
};

} // namespace sq

// -------------------------------------------------------------------------------------------------
// C API

struct sq_engine {
    sq::Engine impl;
};

extern "C" {

void sq_params_default(sq_params* p) {
    std::memset(p, 0, sizeof(*p));
    p->enabled = 1;
    p->eqEnabled = 1;
    static const float kFreqs[10] = {32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    p->numBands = 10;
    for (int i = 0; i < 10; ++i) {
        p->bands[i] = {1, i == 0 ? SQ_BAND_LOWSHELF : i == 9 ? SQ_BAND_HIGHSHELF : SQ_BAND_PEAK, kFreqs[i], 0.0f, 1.0f};
    }
    p->mode = SQ_MODE_HEADPHONES;
    p->spatialEnabled = 0;
    p->upmix = SQ_UPMIX_STEREO;
    static const float kAz[SQ_MAX_SOURCES] = {-30, 30, 0, -110, 110, -150, 150, 0};
    for (int i = 0; i < SQ_MAX_SOURCES; ++i) p->sources[i] = {kAz[i], 0.0f, 1.5f, 1.0f};
    p->width = 1.0f;
    p->xtcEnabled = 0;
    p->xtcStrength = 0.6f;
    p->speakerSpanDeg = 30.0f;
    p->roomSize = 0.35f;
    p->reverbMix = 0.0f;
    p->levelerTargetDb = -20.0f;
    p->limiterEnabled = 1;
    p->limiterCeilingDb = -1.0f;
}

sq_engine* sq_engine_create(void) { return new sq_engine(); }
void sq_engine_destroy(sq_engine* e) { delete e; }
void sq_engine_prepare(sq_engine* e, double fs, int maxFrames) { e->impl.prepare(fs, maxFrames); }
void sq_engine_reset(sq_engine* e) { e->impl.reset(); }
void sq_engine_set_params(sq_engine* e, const sq_params* p) { e->impl.setParams(*p); }
void sq_engine_set_head_yaw(sq_engine* e, float yawDeg) { e->impl.setHeadYaw(yawDeg); }
void sq_engine_process(sq_engine* e, float* l, float* r, int frames) { e->impl.process(l, r, frames); }
void sq_engine_process_interleaved(sq_engine* e, const float* in, int inCh, float* out, int outCh, int frames) {
    e->impl.processInterleaved(in, inCh, out, outCh, frames);
}
#ifdef SQ_HAS_COREAUDIO
void sq_engine_process_abl(sq_engine* e, const AudioBufferList* in, int inOffset, AudioBufferList* out) {
    e->impl.processAbl(in, inOffset, out);
}
#endif
int sq_engine_read_analysis(sq_engine* e, float* dst, int n) { return e->impl.readAnalysis(dst, n); }
void sq_engine_get_meters(sq_engine* e, sq_meters* out) { e->impl.meters(out); }

void sq_eq_response(const sq_params* p, double fs, const float* freqs, float* outDb, int n) {
    sq::BiquadCoeffs coeffs[SQ_MAX_BANDS];
    int count = 0;
    if (p->eqEnabled) {
        for (int i = 0; i < std::min(p->numBands, SQ_MAX_BANDS); ++i) {
            const sq_band& b = p->bands[i];
            if (!b.enabled) continue;
            coeffs[count++] = sq::designBiquad(sq::BandType(std::clamp(b.type, 0, 4)), fs, b.freq, b.gainDb, b.q);
        }
    }
    for (int k = 0; k < n; ++k) {
        double db = p->preampDb;
        for (int i = 0; i < count; ++i) db += sq::biquadMagnitudeDb(coeffs[i], fs, freqs[k]);
        outDb[k] = float(db);
    }
}

} // extern "C"
